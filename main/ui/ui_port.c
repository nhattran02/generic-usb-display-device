#include "ui_port.h"
#include "ui_app.h"

#include "board.h"
#include "lcd.h"
#include "screen.h"
#include "settings.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

static const char *TAG = "ui";

/* Two partial render buffers of 24 full rows (15 KB each) in internal SRAM */
#define DRAW_ROWS       24
#define TASK_CORE       0
#define TASK_PRIO       3       /* below the GUD render task: the host always wins */

static SemaphoreHandle_t s_parked, s_resume;
static volatile bool s_active;

static portMUX_TYPE s_touch_lock = portMUX_INITIALIZER_UNLOCKED;
static bool s_pressed;
static int16_t s_tx, s_ty;
static volatile int64_t s_last_touch_us;

static bool s_asleep, s_swallow;

/* ------------------------------- display ---------------------------------- */

static void flush_cb(lv_display_t *disp, const lv_area_t *a, uint8_t *px)
{
    const unsigned w = (unsigned) lv_area_get_width(a), h = (unsigned) lv_area_get_height(a);
    /* RGB565 little endian in, swapped into the DMA bounce buffers on the way out; the buffer is free
     * again when this returns, the SPI transfer of its tail continues in the background */
    lcd_stream_begin((unsigned) a->x1, (unsigned) a->y1, w, h);
    lcd_stream_write(px, (size_t) w * h * 2);
    lcd_stream_end();
    lv_display_flush_ready(disp);
}

static uint32_t tick_ms(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000);
}

/* -------------------------------- touch ----------------------------------- */

void ui_port_touch(const touch_point_t *pts, int n)
{
    portENTER_CRITICAL(&s_touch_lock);
    s_pressed = n > 0;
    if (n > 0) {
        s_tx = (int16_t) pts[0].x;
        s_ty = (int16_t) pts[0].y;
    }
    portEXIT_CRITICAL(&s_touch_lock);
    if (n > 0)
        s_last_touch_us = esp_timer_get_time();
}

static void read_cb(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void) indev;
    portENTER_CRITICAL(&s_touch_lock);
    const bool pressed = s_pressed;
    data->point.x = s_tx;
    data->point.y = s_ty;
    portEXIT_CRITICAL(&s_touch_lock);

    /* a touch that wakes the screen is not passed on: it shouldn't press whatever is under the finger */
    if (s_asleep) {
        if (pressed) {
            s_asleep = false;
            s_swallow = true;
            screen_local_sleep(false);
        }
    }
    if (s_swallow) {
        if (!pressed)
            s_swallow = false;
        data->state = LV_INDEV_STATE_RELEASED;
        return;
    }
    data->state = pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

/* --------------------------------- task ----------------------------------- */

static void check_idle(void)
{
    const uint16_t timeout = settings_screen_timeout();
    if (s_asleep || !timeout)
        return;
    if (esp_timer_get_time() - s_last_touch_us > (int64_t) timeout * 1000000) {
        s_asleep = true;
        screen_local_sleep(true);
    }
}

static void ui_task(void *arg)
{
    (void) arg;
    for (;;) {
        if (!s_active) {
            xSemaphoreGive(s_parked);
            xSemaphoreTake(s_resume, portMAX_DELAY);
            /* back from the host: the panel holds its picture, so redraw everything */
            s_last_touch_us = esp_timer_get_time();
            s_asleep = false;
            s_swallow = s_pressed;
            lv_obj_invalidate(lv_screen_active());
            continue;
        }
        uint32_t next = lv_timer_handler();
        check_idle();
        if (next < 2)
            next = 2;
        if (next > 10)
            next = 10;
        vTaskDelay(pdMS_TO_TICKS(next));
    }
}

void ui_port_set_active(bool active)
{
    if (active == s_active)
        return;
    s_active = active;
    if (active)
        xSemaphoreGive(s_resume);
    else
        xSemaphoreTake(s_parked, pdMS_TO_TICKS(500));    /* a frame takes at most ~35 ms */
}

esp_err_t ui_port_start(void)
{
    s_parked = xSemaphoreCreateBinary();
    s_resume = xSemaphoreCreateBinary();
    if (!s_parked || !s_resume)
        return ESP_ERR_NO_MEM;

    lv_init();
    lv_tick_set_cb(tick_ms);

    const size_t buf_bytes = (size_t) BOARD_LCD_W * DRAW_ROWS * 2;
    void *buf1 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    void *buf2 = heap_caps_malloc(buf_bytes, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);
    if (!buf1 || !buf2)
        return ESP_ERR_NO_MEM;

    lv_display_t *disp = lv_display_create(BOARD_LCD_W, BOARD_LCD_H);
    lv_display_set_color_format(disp, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(disp, buf1, buf2, (uint32_t) buf_bytes, LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(disp, flush_cb);

    lv_indev_t *indev = lv_indev_create();
    lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(indev, read_cb);

    ui_app_create();
    lv_refr_now(disp);              /* first frame on the panel before the backlight comes on */

    s_last_touch_us = esp_timer_get_time();
    s_active = true;
    if (xTaskCreatePinnedToCore(ui_task, "ui", 8192, NULL, TASK_PRIO, NULL, TASK_CORE) != pdPASS)
        return ESP_ERR_NO_MEM;
    ESP_LOGI(TAG, "LVGL %d.%d.%d standalone UI up", LVGL_VERSION_MAJOR, LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
    return ESP_OK;
}
