#pragma once
/* Pieces of the standalone UI (ui_app.c builds the shell and owns the pages) */
#include "ui_theme.h"

/* Sweeping waveform: a cursor moves left to right and overwrites the old trace behind a short gap, so
 * each new sample only invalidates a strip a few pixels wide */
lv_obj_t *ui_wave_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color,
                         const char *label, float lo, float hi);
void ui_wave_push(lv_obj_t *wave, float sample);

/* Simulated patient (demo data) */
typedef struct {
    float ecg, pleth, resp;             /* current waveform samples */
    float hr, spo2, rr, temp;
    int sys, dia;
    bool beat;                          /* set at each R wave, cleared by the reader */
    double beat_phase, breath_phase, numbers_t;
    uint32_t rng;
} ui_vitals_t;
void ui_vitals_init(ui_vitals_t *v);
void ui_vitals_advance(ui_vitals_t *v, double dt);

/* Pages, each UI_W x UI_CONTENT_H */
lv_obj_t *ui_home_create(lv_obj_t *parent);
lv_obj_t *ui_monitor_create(lv_obj_t *parent);
void ui_monitor_set_speed(float mm_per_s);
lv_obj_t *ui_touch_create(lv_obj_t *parent);
lv_obj_t *ui_settings_create(lv_obj_t *parent);
