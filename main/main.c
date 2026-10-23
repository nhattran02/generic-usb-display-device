/*
 * GUD (Generic USB Display) and HID multitouch support for the
 * LCDWiki ES3C28P.
 *
 * The Linux host exposes a DRM display (drm/gud) and a touchscreen
 * (hid-multitouch).
 *
 * Author: Nhat Tran <nhat.tran.zg@bp.renesas.com>
 */
#include <string.h>
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "lcd.h"
#include "touch.h"
#include "usb_glue.h"
#include "screen.h"
#include "gud.h"
#include "gud_driver.h"

static const char *TAG = "main";

#define FB_BYTES (BOARD_LCD_W * BOARD_LCD_H * 2)

static const uint8_t s_formats[] = { GUD_PIXEL_FORMAT_RGB565 };

/*
 * No real video timings on an SPI panel. 60 Hz is what the host advertises to compositors; when they draw
 * faster than USB + SPI can carry, the gud driver merges the damage, so nothing queues up.
 */
static struct gud_display_timings s_timings = {
    .hfront = 8, .hsync = 4, .hback = 8,
    .vfront = 8, .vsync = 4, .vback = 8,
    .framerate = 60,
};

static struct gud_display_edid s_edid = {
    .name = "ES3C28P GUD",
    .pnp = "GUD",
    .product_code = 1,
    .week = 1,
    .year = 2026,
#if BOARD_LCD_W > BOARD_LCD_H
    .width_mm = BOARD_PANEL_H_MM,
    .height_mm = BOARD_PANEL_W_MM,
#else
    .width_mm = BOARD_PANEL_W_MM,
    .height_mm = BOARD_PANEL_H_MM,
#endif
    .bit_depth = 8,
    .gamma = 220,
    .chromaticity = NULL,
    .timings = &s_timings,
    .get_serial_number = NULL,
};

/* Host unbound/stopped the display pipeline */
static int controller_enable(const struct gud_display *disp, uint8_t enable)
{
    (void) disp;
    if (!enable) {
        gud_driver_discard();               /* pictures still queued must not land on "<NO SIGNAL>" */
        screen_signal_lost();
    }
    return 0;
}

#if BOARD_BL_PWM_GPIO >= 0
/* Backlight brightness 0..100; the value is what the host reads as the initial brightness */
static struct gud_property_req s_connector_props[] = {
    { .prop = GUD_PROPERTY_BACKLIGHT_BRIGHTNESS, .val = BOARD_BL_DEFAULT_PERCENT },
};

/*
 * Brightness is kept in NVS. A slider drag sends many commits, so it is written once the value has been
 * stable for BL_SAVE_DELAY_MS (NVS also skips writes of an unchanged value). 0 is not stored: it means
 * "off", and booting dark would hide "<NO SIGNAL>"; the last non-zero level is kept instead.
 */
#define BL_NVS_NAMESPACE   "display"
#define BL_NVS_KEY         "brightness"
#define BL_SAVE_DELAY_MS   2000

static esp_timer_handle_t s_bl_save_timer;
static uint8_t s_bl_saved_level = BOARD_BL_DEFAULT_PERCENT;

static uint8_t bl_load(void)
{
    nvs_handle_t h;
    uint8_t v = BOARD_BL_DEFAULT_PERCENT;

    if (nvs_open(BL_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        nvs_get_u8(h, BL_NVS_KEY, &v);
        nvs_close(h);
    }
    return (v >= 1 && v <= 100) ? v : BOARD_BL_DEFAULT_PERCENT;
}

static void bl_save_cb(void *arg)
{
    (void) arg;
    nvs_handle_t h;

    if (nvs_open(BL_NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;
    if (nvs_set_u8(h, BL_NVS_KEY, s_bl_saved_level) == ESP_OK)
        nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "brightness %u%% saved", s_bl_saved_level);
}

static void bl_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    if (err == ESP_OK)
        s_bl_saved_level = bl_load();
    else
        ESP_LOGW(TAG, "NVS unavailable (%s), brightness won't be saved", esp_err_to_name(err));

    const esp_timer_create_args_t args = { .callback = bl_save_cb, .name = "bl_save" };
    if (err == ESP_OK)
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_bl_save_timer));

    s_connector_props[0].val = s_bl_saved_level;
    board_backlight_brightness(s_bl_saved_level);
}
#endif

/* The host sends connector properties (backlight brightness) with every state commit */
static int state_commit(const struct gud_display *disp, const struct gud_state_req *state, uint8_t num_properties)
{
    (void) disp;
#if BOARD_BL_PWM_GPIO >= 0
    for (unsigned i = 0; i < num_properties; i++) {
        if (state->properties[i].prop == GUD_PROPERTY_BACKLIGHT_BRIGHTNESS && state->properties[i].val <= 100) {
            uint8_t level = (uint8_t) state->properties[i].val;
            s_connector_props[0].val = level;
            board_backlight_brightness(level);
            if (level && level != s_bl_saved_level && s_bl_save_timer) {
                s_bl_saved_level = level;
                esp_timer_stop(s_bl_save_timer);
                esp_timer_start_once(s_bl_save_timer, BL_SAVE_DELAY_MS * 1000);
            }
        }
    }
#else
    (void) state; (void) num_properties;
#endif
    return 0;
}

/* Every rectangle starts with SET_BUFFER: the host is showing a picture */
static int set_buffer(const struct gud_display *disp, const struct gud_set_buffer_req *req)
{
    (void) disp; (void) req;
    screen_picture();
    return 0;
}

/* Decoded rectangles are streamed straight to the ILI9341 (gud_driver.c render task) */
static void rect_begin(const struct gud_display *disp, const struct gud_set_buffer_req *req)
{
    (void) disp;
    lcd_stream_begin(req->x, req->y, req->width, req->height);
}

static void rect_write(const struct gud_display *disp, const void *pixels, size_t len)
{
    (void) disp;
    lcd_stream_write(pixels, len);
}

static void rect_end(const struct gud_display *disp)
{
    (void) disp;
    lcd_stream_end();
}

static int display_enable(const struct gud_display *disp, uint8_t enable)
{
    (void) disp;
    screen_dpms(enable);
    return 0;
}

static struct gud_display s_display = {
    .width = BOARD_LCD_W,
    .height = BOARD_LCD_H,
    .flags = 0,
    .compression = GUD_COMPRESSION_LZ4,
    .max_buffer_size = FB_BYTES,         /* whole updates in one transfer: they appear at once */
    .formats = s_formats,
    .num_formats = sizeof(s_formats),
    .edid = &s_edid,
    .controller_enable = controller_enable,
    .display_enable = display_enable,
    .set_buffer = set_buffer,
    .rect_begin = rect_begin,
    .rect_write = rect_write,
    .rect_end = rect_end,
    .state_commit = state_commit,
#if BOARD_BL_PWM_GPIO >= 0
    .connector_properties = s_connector_props,
    .num_connector_properties = sizeof(s_connector_props) / sizeof(s_connector_props[0]),
#endif
};

void app_main(void)
{
    ESP_LOGI(TAG, "GUD display firmware starting");

    ESP_ERROR_CHECK(board_init());          /* I2C bus, backlight PWM (dark) */
    ESP_ERROR_CHECK(lcd_init());            /* ILI9341 over SPI, framebuffer for the firmware's own screens */
#if BOARD_BL_PWM_GPIO >= 0
    bl_init();                              /* saved brightness, before the backlight first comes on */
#endif
    screen_init();                          /* "<NO SIGNAL>" until the host sends a picture, sleep after 60 s */

    /* Bulk data is written to the receive buffers at USB speed (~1 MB/s), so PSRAM is fine; LZ4 back-references
     * are served from an internal-SRAM window (see gud_driver.c) */
    void *rx[GUD_RX_BUFS];
    for (int i = 0; i < GUD_RX_BUFS; i++)
        rx[i] = heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *window = heap_caps_malloc(GUD_LZ4_WINDOW_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!rx[0] || !rx[1] || !window) {
        ESP_LOGE(TAG, "out of memory for GUD buffers");
        abort();
    }
    /* Render task on core 0, next to nothing busy; the USB stack runs on core 1 */
    ESP_ERROR_CHECK(gud_driver_setup(&s_display, rx, FB_BYTES, window, 0));

    if (touch_init() == ESP_OK) {
        touch_start(usb_glue_touch);
    } else {
        ESP_LOGW(TAG, "touch disabled");
    }

    usb_glue_start();                       /* USB task + ISR on core 1 */
    ESP_LOGI(TAG, "ready; free internal %u, PSRAM %u", (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
