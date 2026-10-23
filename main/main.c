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
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include "board.h"
#include "lcd.h"
#include "touch.h"
#include "usb_glue.h"
#include "screen.h"
#include "settings.h"
#include "ui_port.h"
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
        gud_driver_discard();               /* pictures still queued must not land on the standalone UI */
        screen_signal_lost();
    }
    return 0;
}

#if BOARD_BL_PWM_GPIO >= 0
/* Backlight brightness 0..100; the value is what the host reads as the initial brightness */
static struct gud_property_req s_connector_props[] = {
    { .prop = GUD_PROPERTY_BACKLIGHT_BRIGHTNESS, .val = BOARD_BL_DEFAULT_PERCENT },
};

/* The standalone UI changed the brightness: keep what the host reads in step */
static void brightness_changed(uint8_t level)
{
    s_connector_props[0].val = level;
}
#endif

/* The host sends connector properties (backlight brightness) with every state commit */
static int state_commit(const struct gud_display *disp, const struct gud_state_req *state, uint8_t num_properties)
{
    (void) disp;
#if BOARD_BL_PWM_GPIO >= 0
    for (unsigned i = 0; i < num_properties; i++) {
        if (state->properties[i].prop == GUD_PROPERTY_BACKLIGHT_BRIGHTNESS && state->properties[i].val <= 100) {
            s_connector_props[0].val = state->properties[i].val;
            settings_set_brightness((uint8_t) state->properties[i].val, true);
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

/* Touch goes to whoever owns the panel: the standalone UI, or the host as HID reports */
static void touch_route(const touch_point_t *pts, int n)
{
    if (screen_is_local())
        ui_port_touch(pts, n);
    else
        usb_glue_touch(pts, n);
}

void app_main(void)
{
    ESP_LOGI(TAG, "GUD display firmware starting");

    ESP_ERROR_CHECK(board_init());          /* I2C bus, backlight PWM (dark) */
    ESP_ERROR_CHECK(lcd_init());            /* ILI9341 over SPI, GRAM cleared */
    settings_init();                        /* saved brightness and screen timeout (NVS) */
#if BOARD_BL_PWM_GPIO >= 0
    s_connector_props[0].val = settings_brightness();
    settings_on_brightness(brightness_changed);
#endif

    /* Bulk data is written to the receive buffers at USB speed (~1 MB/s), so PSRAM is fine; LZ4 back-references
     * are served from an internal-SRAM window (see gud_driver.c). Allocated before the UI takes its share. */
    void *rx[GUD_RX_BUFS];
    for (int i = 0; i < GUD_RX_BUFS; i++)
        rx[i] = heap_caps_malloc(FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *window = heap_caps_malloc(GUD_LZ4_WINDOW_SIZE, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!rx[0] || !rx[1] || !window) {
        ESP_LOGE(TAG, "out of memory for GUD buffers");
        abort();
    }
    /* Render task on core 0, next to the UI task (parked while the host draws); the USB stack runs on core 1 */
    ESP_ERROR_CHECK(gud_driver_setup(&s_display, rx, FB_BYTES, window, 0));

    screen_init();                          /* standalone UI until the host sends a picture */

    if (touch_init() == ESP_OK) {
        touch_start(touch_route);
    } else {
        ESP_LOGW(TAG, "touch disabled");
    }

    usb_glue_start();                       /* USB task + ISR on core 1 */
    ESP_LOGI(TAG, "ready; free internal %u, PSRAM %u", (unsigned) heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
             (unsigned) heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
}
