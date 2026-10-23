#pragma once
#include <stdbool.h>
#include "esp_err.h"
#include "touch.h"

/*
 * LVGL on the ES3C28P: the standalone UI shown while no host is drawing. Renders through lcd_stream_*
 * (only the areas that changed), reads touch from touch.c, runs in its own task on core 0.
 */
esp_err_t ui_port_start(void);

/*
 * false: stop drawing (returns once LVGL has finished the frame in progress), the host gets the panel;
 * true: take the panel back and redraw everything.
 */
void ui_port_set_active(bool active);

/* Touch contacts from the touch task (only the first one is used) */
void ui_port_touch(const touch_point_t *pts, int n);
