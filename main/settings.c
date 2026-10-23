#include <stdbool.h>
#include "settings.h"
#include "board.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "settings";

#define NVS_NAMESPACE       "display"
#define KEY_BRIGHTNESS      "brightness"
#define KEY_TIMEOUT         "timeout"
#define SAVE_DELAY_MS       2000
#define DEFAULT_TIMEOUT_S   300

static bool s_nvs_ok;
static esp_timer_handle_t s_save_timer;
static volatile uint8_t s_brightness = BOARD_BL_DEFAULT_PERCENT;
static volatile uint16_t s_timeout = DEFAULT_TIMEOUT_S;
static void (*s_brightness_cb)(uint8_t);

static void save_cb(void *arg)
{
    (void) arg;
    nvs_handle_t h;

    if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK)
        return;
    nvs_set_u8(h, KEY_BRIGHTNESS, s_brightness);    /* NVS skips unchanged values */
    nvs_set_u16(h, KEY_TIMEOUT, s_timeout);
    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved: brightness %u%%, screen timeout %u s", s_brightness, s_timeout);
}

static void schedule_save(void)
{
    if (!s_save_timer)
        return;
    esp_timer_stop(s_save_timer);
    esp_timer_start_once(s_save_timer, SAVE_DELAY_MS * 1000);
}

void settings_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        err = nvs_flash_init();
    }
    s_nvs_ok = err == ESP_OK;

    if (s_nvs_ok) {
        nvs_handle_t h;
        if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
            uint8_t b;
            uint16_t t;
            if (nvs_get_u8(h, KEY_BRIGHTNESS, &b) == ESP_OK && b >= 1 && b <= 100)
                s_brightness = b;
            if (nvs_get_u16(h, KEY_TIMEOUT, &t) == ESP_OK)
                s_timeout = t;
            nvs_close(h);
        }
        const esp_timer_create_args_t args = { .callback = save_cb, .name = "settings_save" };
        ESP_ERROR_CHECK(esp_timer_create(&args, &s_save_timer));
    } else {
        ESP_LOGW(TAG, "NVS unavailable (%s), settings won't be saved", esp_err_to_name(err));
    }
    board_backlight_brightness(s_brightness);
}

uint8_t settings_brightness(void)
{
    return s_brightness;
}

void settings_set_brightness(uint8_t level, bool from_host)
{
    if (level > 100)
        level = 100;
    board_backlight_brightness(level);
    /* 0 means "off" (host only); booting dark would hide the UI, so the last non-zero level is kept */
    if (level && level != s_brightness) {
        s_brightness = level;
        schedule_save();
    }
    if (s_brightness_cb && !from_host)
        s_brightness_cb(level);
}

void settings_on_brightness(void (*cb)(uint8_t level))
{
    s_brightness_cb = cb;
}

uint16_t settings_screen_timeout(void)
{
    return s_timeout;
}

void settings_set_screen_timeout(uint16_t seconds)
{
    if (seconds != s_timeout) {
        s_timeout = seconds;
        schedule_save();
    }
}
