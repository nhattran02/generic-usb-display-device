#include <string.h>
#include "touch.h"
#include "board.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "ft6336";

/*
 * Fixes for the raw controller coordinates (portrait, 0..239 x 0..319) before they are turned with
 * BOARD_LCD_ROTATION; use them if touches land mirrored in portrait (rotation 0).
 */
#define TOUCH_SWAP_XY   0
#define TOUCH_MIRROR_X  0
#define TOUCH_MIRROR_Y  0
#define TOUCH_POLL_MS   5

#define FT_ADDR            0x38
#define FT_REG_TD_STATUS   0x02     /* number of points, followed by 6 bytes per point */
#define FT_REG_G_MODE      0xA4     /* 0 = polling (INT low while touched), 1 = trigger */
#define FT_REG_CHIP_ID     0xA3
#define FT_REG_FIRMID      0xA6

#define FT_EVENT_DOWN      0
#define FT_EVENT_UP        1
#define FT_EVENT_CONTACT   2

static i2c_master_dev_handle_t s_dev;
static touch_cb_t s_cb;

static esp_err_t ft_read(uint8_t reg, uint8_t *buf, size_t len)
{
    return i2c_master_transmit_receive(s_dev, &reg, 1, buf, len, 50);
}

static esp_err_t ft_write8(uint8_t reg, uint8_t v)
{
    uint8_t d[2] = { reg, v };
    return i2c_master_transmit(s_dev, d, 2, 50);
}

esp_err_t touch_init(void)
{
    const gpio_config_t rst = {
        .pin_bit_mask = 1ULL << BOARD_TP_RST,
        .mode = GPIO_MODE_OUTPUT,
    };
    gpio_config(&rst);
    const gpio_config_t irq = {
        .pin_bit_mask = 1ULL << BOARD_TP_INT,
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
    };
    gpio_config(&irq);

    gpio_set_level(BOARD_TP_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(BOARD_TP_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(300));          /* FT6x36 needs ~300 ms after reset before it answers */

    if (i2c_master_probe(board_i2c(), FT_ADDR, 50) != ESP_OK) {
        ESP_LOGE(TAG, "FT6336 not found on 0x%02x", FT_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    const i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = FT_ADDR,
        .scl_speed_hz = 400000,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(board_i2c(), &dev_cfg, &s_dev));

    uint8_t chip = 0, fw = 0;
    ft_read(FT_REG_CHIP_ID, &chip, 1);
    ft_read(FT_REG_FIRMID, &fw, 1);
    ESP_LOGI(TAG, "FT6336 @0x%02x chip id 0x%02x firmware 0x%02x", FT_ADDR, chip, fw);
    ft_write8(FT_REG_G_MODE, 0);
    return ESP_OK;
}

/* Raw portrait panel coordinates -> the orientation the host sees (matches MADCTL in lcd.c) */
static void orient(uint32_t rx, uint32_t ry, uint16_t *x, uint16_t *y)
{
    const uint32_t pw = BOARD_PANEL_W - 1, ph = BOARD_PANEL_H - 1;

    if (TOUCH_SWAP_XY) { uint32_t t = rx; rx = ry; ry = t; }
    if (rx > pw) rx = pw;
    if (ry > ph) ry = ph;
    if (TOUCH_MIRROR_X) rx = pw - rx;
    if (TOUCH_MIRROR_Y) ry = ph - ry;

    switch (BOARD_LCD_ROTATION & 3) {
    case 1:  *x = (uint16_t) ry;        *y = (uint16_t) (pw - rx); break;
    case 2:  *x = (uint16_t) (pw - rx); *y = (uint16_t) (ph - ry); break;
    case 3:  *x = (uint16_t) (ph - ry); *y = (uint16_t) rx;        break;
    default: *x = (uint16_t) rx;        *y = (uint16_t) ry;        break;
    }
}

static void touch_task(void *arg)
{
    (void) arg;
    touch_point_t pts[TOUCH_MAX_POINTS];
    int last_n = 0;

    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(TOUCH_POLL_MS));

        /* Plain polling: a 13-byte read takes ~0.4 ms at 400 kHz, and it doesn't depend on how INT behaves */
        uint8_t raw[1 + 6 * TOUCH_MAX_POINTS];
        if (ft_read(FT_REG_TD_STATUS, raw, sizeof(raw)) != ESP_OK)
            continue;

        int cnt = raw[0] & 0x0F;
        if (cnt > TOUCH_MAX_POINTS)
            cnt = 0;                            /* 0x0F after reset / invalid */

        int n = 0;
        for (int i = 0; i < cnt; i++) {
            const uint8_t *p = &raw[1 + i * 6];
            uint8_t event = p[0] >> 6;
            uint8_t id = p[2] >> 4;
            if (event == FT_EVENT_UP || id >= 0x0F)
                continue;
            uint32_t x = (uint32_t) (((p[0] & 0x0F) << 8) | p[1]);
            uint32_t y = (uint32_t) (((p[2] & 0x0F) << 8) | p[3]);
            pts[n].id = id;
            orient(x, y, &pts[n].x, &pts[n].y);
            n++;
        }

        if (s_cb && (n > 0 || last_n > 0))
            s_cb(pts, n);
        last_n = n;
    }
}

void touch_start(touch_cb_t cb)
{
    s_cb = cb;
    xTaskCreatePinnedToCore(touch_task, "touch", 4096, NULL, 4, NULL, 1);
}
