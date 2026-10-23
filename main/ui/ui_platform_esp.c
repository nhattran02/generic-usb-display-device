/* ui_platform.h on the ESP32-S3 */
#include <math.h>
#include "ui_platform.h"
#include "settings.h"

#include "driver/temperature_sensor.h"
#include "esp_app_desc.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "tusb.h"

float ui_plat_chip_temp(void)
{
    static temperature_sensor_handle_t s_ts;
    static bool s_failed;

    if (!s_ts && !s_failed) {
        temperature_sensor_config_t cfg = TEMPERATURE_SENSOR_CONFIG_DEFAULT(10, 80);
        s_failed = temperature_sensor_install(&cfg, &s_ts) != ESP_OK || temperature_sensor_enable(s_ts) != ESP_OK;
    }
    float t;
    if (s_failed || temperature_sensor_get_celsius(s_ts, &t) != ESP_OK)
        return NAN;
    return t;
}

uint32_t ui_plat_uptime_s(void)
{
    return (uint32_t) (esp_timer_get_time() / 1000000);
}

size_t ui_plat_free_internal(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
}

size_t ui_plat_free_psram(void)
{
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
}

const char *ui_plat_fw_version(void)
{
    return esp_app_get_description()->version;
}

bool ui_plat_usb_connected(void)
{
    return tud_mounted();
}

uint8_t ui_plat_brightness(void)
{
    return settings_brightness();
}

void ui_plat_set_brightness(uint8_t level)
{
    settings_set_brightness(level, false);
}

uint16_t ui_plat_screen_timeout(void)
{
    return settings_screen_timeout();
}

void ui_plat_set_screen_timeout(uint16_t seconds)
{
    settings_set_screen_timeout(seconds);
}
