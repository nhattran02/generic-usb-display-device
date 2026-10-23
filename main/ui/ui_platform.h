#pragma once
/*
 * What the standalone UI needs from the device. The firmware implements it in ui_platform_esp.c; keeping
 * the UI on this narrow interface lets the same UI code be rendered on a PC for previews.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

float ui_plat_chip_temp(void);              /* °C, NAN if unavailable */
uint32_t ui_plat_uptime_s(void);
size_t ui_plat_free_internal(void);         /* bytes */
size_t ui_plat_free_psram(void);
const char *ui_plat_fw_version(void);
bool ui_plat_usb_connected(void);           /* enumerated by a host (the host isn't showing a picture yet) */

uint8_t ui_plat_brightness(void);           /* 1..100 */
void ui_plat_set_brightness(uint8_t level);
uint16_t ui_plat_screen_timeout(void);      /* seconds, 0 = never */
void ui_plat_set_screen_timeout(uint16_t seconds);
