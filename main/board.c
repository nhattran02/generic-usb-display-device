#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

static const char *TAG = "board";

static i2c_master_bus_handle_t s_bus;

i2c_master_bus_handle_t board_i2c(void) { return s_bus; }

/* ------------------------------- backlight -------------------------------- */

#define BL_PWM_HZ        20000
#define BL_PWM_BITS      LEDC_TIMER_11_BIT
#define BL_PWM_MAX       ((1u << 11) - 1)
/* Below this duty the LEDs are practically dark: map 1% to a visible level */
#define BL_PWM_MIN       (BL_PWM_MAX / 50)

static bool s_bl_on;
static uint8_t s_bl_level = BOARD_BL_DEFAULT_PERCENT;

static void backlight_apply(void)
{
#if BOARD_BL_PWM_GPIO >= 0
    uint32_t duty = 0;
    if (s_bl_on && s_bl_level >= 100)
        duty = BL_PWM_MAX + 1;              /* constantly high */
    else if (s_bl_on && s_bl_level)
        duty = BL_PWM_MIN + (BL_PWM_MAX - BL_PWM_MIN) * (s_bl_level - 1) / 98;
    ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
    ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
#endif
}

void board_backlight(bool on)
{
    s_bl_on = on;
    backlight_apply();
}

void board_backlight_brightness(uint8_t percent)
{
    s_bl_level = percent > 100 ? 100 : percent;
    backlight_apply();
}

static esp_err_t backlight_init(void)
{
#if BOARD_BL_PWM_GPIO >= 0
    const ledc_timer_config_t timer = {
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .duty_resolution = BL_PWM_BITS,
        .timer_num = LEDC_TIMER_0,
        .freq_hz = BL_PWM_HZ,
        .clk_cfg = LEDC_AUTO_CLK,
    };
    ESP_RETURN_ON_ERROR(ledc_timer_config(&timer), TAG, "LEDC timer");
    const ledc_channel_config_t ch = {
        .gpio_num = BOARD_BL_PWM_GPIO,
        .speed_mode = LEDC_LOW_SPEED_MODE,
        .channel = LEDC_CHANNEL_0,
        .timer_sel = LEDC_TIMER_0,
        .duty = 0,
    };
    ESP_RETURN_ON_ERROR(ledc_channel_config(&ch), TAG, "LEDC channel");
#endif
    return ESP_OK;
}

esp_err_t board_init(void)
{
    ESP_RETURN_ON_ERROR(backlight_init(), TAG, "backlight");   /* dark until the panel holds a picture */

    i2c_master_bus_config_t bus_cfg = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = BOARD_I2C_SDA,
        .scl_io_num = BOARD_I2C_SCL,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true,
    };
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_cfg, &s_bus), TAG, "I2C bus");
    ESP_LOGI(TAG, "I2C ready (SDA %d, SCL %d)", BOARD_I2C_SDA, BOARD_I2C_SCL);
    return ESP_OK;
}
