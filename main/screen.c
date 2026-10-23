/*
 * Panel ownership and backlight policy:
 *  - boot: the standalone UI (LVGL, main/ui) owns the panel, backlight on; it sleeps after the screen
 *    timeout set in its Settings page and a touch wakes it;
 *  - a picture from the host (GUD SET_BUFFER) parks the UI (after the frame it is drawing) and gives the
 *    panel to the host; host DPMS then switches the backlight and panel sleep;
 *  - the host goes away (disconnect, reboot, gud unbound): the UI takes the panel back and redraws.
 * Called from the USB task and the UI task. The lock is never held while waiting for the UI to park,
 * since the UI task itself calls screen_local_sleep().
 */
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include "board.h"
#include "lcd.h"
#include "screen.h"
#include "ui_port.h"

static SemaphoreHandle_t s_lock;
static volatile bool s_local = true;
static bool s_lit, s_dpms_off, s_local_sleep;

/* Dark screen = backlight off and the ILI9341 in sleep mode; only switch on an actual change */
static void set_lit(bool on)
{
    if (on == s_lit)
        return;
    s_lit = on;
    if (on) {
        lcd_sleep(false);
        board_backlight(true);
    } else {
        board_backlight(false);
        lcd_sleep(true);
    }
}

static void apply(void)
{
    set_lit(s_local ? !s_local_sleep : !s_dpms_off);
}

void screen_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    ESP_ERROR_CHECK(ui_port_start());       /* draws its first frame */
    board_backlight(true);                  /* the panel is already awake after lcd_init() */
    s_lit = true;
}

bool screen_is_local(void)
{
    return s_local;
}

void screen_picture(void)
{
    bool take_over = false;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (s_local) {
        s_local = false;                    /* touch goes to the host from now on */
        s_dpms_off = false;
        take_over = true;
    }
    xSemaphoreGive(s_lock);

    if (take_over)
        ui_port_set_active(false);          /* waits for the UI's frame in progress */

    xSemaphoreTake(s_lock, portMAX_DELAY);
    apply();
    xSemaphoreGive(s_lock);
}

void screen_dpms(bool on)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_dpms_off = !on;
    if (!s_local)
        apply();
    xSemaphoreGive(s_lock);
}

void screen_signal_lost(void)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    const bool give_back = !s_local;
    s_local = true;
    s_local_sleep = false;
    s_dpms_off = false;
    xSemaphoreGive(s_lock);

    if (give_back)
        ui_port_set_active(true);           /* redraws the whole screen */

    xSemaphoreTake(s_lock, portMAX_DELAY);
    apply();
    xSemaphoreGive(s_lock);
}

void screen_local_sleep(bool sleep)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    s_local_sleep = sleep;
    if (s_local)
        apply();
    xSemaphoreGive(s_lock);
}
