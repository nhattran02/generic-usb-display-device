/* Monitor page: three sweeping waveforms and the numeric tiles, like a bedside monitor (demo data) */
#include <stdio.h>
#include "ui_internal.h"

#define WAVE_W      196
#define PX_PER_MM   (320.0f / 57.6f)        /* 2.8" panel, 57.6 mm wide */

typedef struct {
    lv_obj_t *value, *extra;
} tile_t;

static ui_vitals_t s_v;
static lv_obj_t *s_wave[3];
static tile_t s_hr, s_spo2, s_nibp, s_temp;
static lv_obj_t *s_heart;
static float s_px_per_s = 12.5f * PX_PER_MM;
static float s_pending;
static uint32_t s_last_ms, s_beat_ms, s_numbers_ms;

void ui_monitor_set_speed(float mm_per_s)
{
    s_px_per_s = mm_per_s * PX_PER_MM;
}

static tile_t tile(lv_obj_t *parent, int32_t y, int32_t h, const char *name, const char *unit, lv_color_t color,
                   const lv_font_t *font)
{
    tile_t t;
    lv_obj_t *box = ui_box(parent, 0, y, 124, h, UI_PANEL);
    if (y) {
        lv_obj_t *sep = ui_box(box, 4, 0, 116, 1, UI_LINE);
        (void) sep;
    }
    lv_obj_t *l = ui_label(box, name, UI_FONT_S, color);
    lv_obj_set_pos(l, 6, 3);
    l = ui_label(box, unit, UI_FONT_S, UI_MUTED);
    lv_obj_align(l, LV_ALIGN_TOP_RIGHT, -6, 3);
    t.value = ui_label(box, "--", font, color);
    lv_obj_align(t.value, LV_ALIGN_BOTTOM_RIGHT, -6, -2);
    t.extra = ui_label(box, "", UI_FONT_S, UI_MUTED);
    lv_obj_align(t.extra, LV_ALIGN_BOTTOM_LEFT, 6, -4);
    return t;
}

static void update_numbers(void)
{
    char s[24];
    lv_label_set_text_fmt(s_hr.value, "%d", (int) (s_v.hr + 0.5f));
    lv_label_set_text_fmt(s_spo2.value, "%d", (int) (s_v.spo2 + 0.5f));
    lv_label_set_text_fmt(s_spo2.extra, "RR %d", (int) (s_v.rr + 0.5f));
    lv_label_set_text_fmt(s_nibp.value, "%d/%d", s_v.sys, s_v.dia);
    lv_label_set_text_fmt(s_nibp.extra, "(%d)", (s_v.sys + 2 * s_v.dia) / 3);
    snprintf(s, sizeof(s), "%.1f", (double) s_v.temp);
    lv_label_set_text(s_temp.value, s);
}

static void tick(lv_timer_t *t)
{
    (void) t;
    const uint32_t now = lv_tick_get();
    s_pending += (float) (now - s_last_ms) / 1000.0f * s_px_per_s;
    s_last_ms = now;
    if (s_pending > WAVE_W)
        s_pending = WAVE_W;

    const double dt = 1.0 / s_px_per_s;
    while (s_pending >= 1.0f) {
        s_pending -= 1.0f;
        ui_vitals_advance(&s_v, dt);
        if (s_v.beat) {
            s_v.beat = false;
            s_beat_ms = now;
            lv_obj_set_style_bg_color(s_heart, UI_ECG, 0);
        }
        ui_wave_push(s_wave[0], s_v.ecg);
        ui_wave_push(s_wave[1], s_v.pleth);
        ui_wave_push(s_wave[2], s_v.resp);
    }
    if ((now - s_beat_ms) > 150)
        lv_obj_set_style_bg_color(s_heart, UI_LINE, 0);
    if ((now - s_numbers_ms) >= 1000) {
        s_numbers_ms = now;
        update_numbers();
    }
}

lv_obj_t *ui_monitor_create(lv_obj_t *parent)
{
    lv_obj_t *page = ui_box(parent, 0, 0, UI_W, UI_CONTENT_H, UI_BG);

    s_wave[0] = ui_wave_create(page, 0, 0, WAVE_W, 70, UI_ECG, "ECG  II", -0.45f, 1.1f);
    s_wave[1] = ui_wave_create(page, 0, 70, WAVE_W, 60, UI_SPO2, "PLETH", -0.55f, 0.65f);
    s_wave[2] = ui_wave_create(page, 0, 130, WAVE_W, 56, UI_RESP, "RESP", -1.0f, 1.0f);

    lv_obj_t *col = ui_box(page, WAVE_W, 0, UI_W - WAVE_W, UI_CONTENT_H, UI_PANEL);
    lv_obj_set_style_border_side(col, LV_BORDER_SIDE_LEFT, 0);
    lv_obj_set_style_border_width(col, 1, 0);
    lv_obj_set_style_border_color(col, UI_LINE, 0);
    s_hr = tile(col, 0, 46, "HR", "bpm", UI_ECG, UI_FONT_XL);
    s_spo2 = tile(col, 46, 46, "SpO2", "%", UI_SPO2, UI_FONT_XL);
    s_nibp = tile(col, 92, 47, "NIBP", "mmHg", UI_NIBP, UI_FONT_L);
    s_temp = tile(col, 139, 47, "TEMP", "\xc2\xb0" "C", UI_TEMP, UI_FONT_XL);

    s_heart = ui_box(lv_obj_get_parent(s_hr.value), 30, 6, 7, 7, UI_LINE);
    lv_obj_set_style_radius(s_heart, LV_RADIUS_CIRCLE, 0);

    ui_vitals_init(&s_v);
    update_numbers();
    s_last_ms = lv_tick_get();
    lv_timer_create(tick, 16, NULL);
    return page;
}
