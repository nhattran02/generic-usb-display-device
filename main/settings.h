#pragma once
#include <stdbool.h>
#include <stdint.h>

/*
 * Settings shared by the host path and the standalone UI, kept in NVS. A slider drag (from the host or the
 * UI) makes many changes, so a value is written once it has been stable for a moment.
 */
void settings_init(void);

/* Backlight brightness 1..100. Applied right away; from_host = true for GUD state commits. */
uint8_t settings_brightness(void);
void settings_set_brightness(uint8_t level, bool from_host);
/* Called after every brightness change (e.g. to keep the GUD connector property in sync); may be NULL */
void settings_on_brightness(void (*cb)(uint8_t level));

/* Standalone UI: seconds without touch before the screen sleeps, 0 = never */
uint16_t settings_screen_timeout(void);
void settings_set_screen_timeout(uint16_t seconds);
