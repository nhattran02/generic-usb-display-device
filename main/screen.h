#pragma once
#include <stdbool.h>

/*
 * Who owns the panel. Without a host picture the standalone UI (LVGL) runs; the first picture from the
 * host (GUD SET_BUFFER) hands the panel to the host, and losing the host hands it back.
 */
void screen_init(void);

/* true while the standalone UI owns the panel (touch goes to it instead of the host) */
bool screen_is_local(void);

/* The host is sending a picture (GUD SET_BUFFER). */
void screen_picture(void);

/* Host display enable/disable (DPMS). */
void screen_dpms(bool on);

/* The host stopped driving the display (USB deconfigured, gud controller disabled). */
void screen_signal_lost(void);

/* Standalone UI: screen timeout reached (sleep = true) or a touch woke it up */
void screen_local_sleep(bool sleep);
