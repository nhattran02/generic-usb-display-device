/* Home, Touch and Settings pages of the standalone UI */
#include <math.h>
#include <stdio.h>
#include "ui_internal.h"
#include "ui_platform.h"

/* ---------------------------------- Home ---------------------------------- */

static lv_obj_t *s_usb_dot, *s_usb_title, *s_usb_text;
static lv_obj_t *s_info_val[4];

static lv_obj_t *card(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h)
{
    lv_obj_t *c = ui_box(parent, x, y, w, h, UI_CARD);
    lv_obj_set_style_radius(c, 6, 0);
    lv_obj_set_style_border_width(c, 1, 0);
    lv_obj_set_style_border_color(c, UI_LINE, 0);
    return c;
}

static void dot_opa_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa(obj, (lv_opa_t) v, 0);
}

static void home_refresh(lv_timer_t *t)
{
    (void) t;
    static int s_usb = -1;              /* only touch the card when the state changes */
    const bool usb = ui_plat_usb_connected();
    if (usb != s_usb) {
        s_usb = usb;
        lv_obj_set_style_bg_color(s_usb_dot, usb ? UI_ECG : UI_AMBER, 0);
        lv_label_set_text(s_usb_title, usb ? "Host connected" : "Standalone mode");
        lv_label_set_text(s_usb_text, usb ? "Waiting for the host to draw: load the gud driver or start the desktop."
                                          : "Plug the Type-C port into a Linux host and this screen becomes its display.");
    }

    char s[32];
    const float temp = ui_plat_chip_temp();
    if (isnan(temp))
        snprintf(s, sizeof(s), "n/a");
    else
        snprintf(s, sizeof(s), "%.1f \xc2\xb0" "C", (double) temp);
    lv_label_set_text(s_info_val[0], s);

    const uint32_t up = ui_plat_uptime_s();
    lv_label_set_text_fmt(s_info_val[1], "%02u:%02u:%02u", (unsigned) (up / 3600), (unsigned) (up / 60 % 60),
                          (unsigned) (up % 60));
    lv_label_set_text_fmt(s_info_val[2], "%u KB", (unsigned) (ui_plat_free_internal() / 1024));
    snprintf(s, sizeof(s), "%.1f MB", (double) ui_plat_free_psram() / (1024.0 * 1024.0));
    lv_label_set_text(s_info_val[3], s);
}

lv_obj_t *ui_home_create(lv_obj_t *parent)
{
    lv_obj_t *page = ui_box(parent, 0, 0, UI_W, UI_CONTENT_H, UI_BG);

    /* connection card */
    lv_obj_t *c = card(page, 8, 8, UI_W - 16, 70);
    lv_obj_t *icon = ui_label(c, LV_SYMBOL_USB, UI_FONT_L, UI_ACCENT);
    lv_obj_set_pos(icon, 10, 9);
    s_usb_dot = ui_box(c, 30, 8, 8, 8, UI_AMBER);
    lv_obj_set_style_radius(s_usb_dot, LV_RADIUS_CIRCLE, 0);
    s_usb_title = ui_label(c, "", UI_FONT_M, UI_TEXT);
    lv_obj_set_pos(s_usb_title, 48, 8);
    s_usb_text = ui_label(c, "", UI_FONT_S, UI_MUTED);
    lv_obj_set_pos(s_usb_text, 48, 28);
    lv_obj_set_width(s_usb_text, UI_W - 16 - 48 - 12);
    lv_label_set_long_mode(s_usb_text, LV_LABEL_LONG_WRAP);

    /* a slow "breathing" status dot: small area, cheap to animate */
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, s_usb_dot);
    lv_anim_set_exec_cb(&a, dot_opa_cb);
    lv_anim_set_values(&a, LV_OPA_20, LV_OPA_COVER);
    lv_anim_set_duration(&a, 900);
    lv_anim_set_playback_duration(&a, 900);
    lv_anim_set_repeat_count(&a, LV_ANIM_REPEAT_INFINITE);
    lv_anim_start(&a);

    /* device info, 2 x 2 */
    static const char *const names[4] = { "CHIP TEMP", "UPTIME", "FREE SRAM", "FREE PSRAM" };
    const lv_color_t cols[4] = { UI_TEMP, UI_TEXT, UI_ACCENT, UI_ACCENT };
    const int32_t cw = (UI_W - 24) / 2, ch = 46;
    for (int i = 0; i < 4; i++) {
        lv_obj_t *ic = card(page, 8 + (i % 2) * (cw + 8), 86 + (i / 2) * (ch + 6), cw, ch);
        lv_obj_t *l = ui_label(ic, names[i], UI_FONT_S, UI_MUTED);
        lv_obj_set_pos(l, 10, 6);
        s_info_val[i] = ui_label(ic, "", UI_FONT_L, cols[i]);
        lv_obj_set_pos(s_info_val[i], 10, 20);
    }

    home_refresh(NULL);
    lv_timer_create(home_refresh, 1000, NULL);
    return page;
}

/* ---------------------------------- Touch --------------------------------- */

#define TRAIL_N     20

static lv_obj_t *s_touch_page, *s_hline, *s_vline, *s_coords, *s_hint;
static lv_obj_t *s_trail[TRAIL_N];
static int s_trail_next;
static bool s_was_pressed;

static void fade_out_cb(void *obj, int32_t v)
{
    lv_obj_set_style_bg_opa(obj, (lv_opa_t) v, 0);
}

static void ripple_cb(void *obj, int32_t v)
{
    /* v: 0..100, the ring grows and fades */
    const int32_t r = 6 + v * 34 / 100;
    const int32_t cx = (int32_t) (intptr_t) lv_obj_get_user_data(obj) >> 16;
    const int32_t cy = (int32_t) (intptr_t) lv_obj_get_user_data(obj) & 0xFFFF;
    lv_obj_set_size(obj, 2 * r, 2 * r);
    lv_obj_set_pos(obj, cx - r, cy - r);
    lv_obj_set_style_border_opa(obj, (lv_opa_t) (LV_OPA_COVER - v * 255 / 100), 0);
}

static void ripple_done(lv_anim_t *a)
{
    lv_obj_delete(a->var);
}

static void ripple(int32_t x, int32_t y)
{
    lv_obj_t *r = lv_obj_create(s_touch_page);
    lv_obj_remove_style_all(r);
    lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_style_radius(r, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_border_width(r, 2, 0);
    lv_obj_set_style_border_color(r, UI_ACCENT, 0);
    lv_obj_set_user_data(r, (void *) (intptr_t) ((x << 16) | (y & 0xFFFF)));

    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, r);
    lv_anim_set_exec_cb(&a, ripple_cb);
    lv_anim_set_values(&a, 0, 100);
    lv_anim_set_duration(&a, 450);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    lv_anim_set_completed_cb(&a, ripple_done);
    lv_anim_start(&a);
}

static void touch_event(lv_event_t *e)
{
    const lv_event_code_t code = lv_event_get_code(e);
    lv_indev_t *indev = lv_indev_active();
    if (!indev)
        return;
    lv_point_t p;
    lv_indev_get_point(indev, &p);
    lv_area_t a;
    lv_obj_get_coords(s_touch_page, &a);
    const int32_t x = p.x - a.x1, y = p.y - a.y1;

    if (code == LV_EVENT_PRESSED || code == LV_EVENT_PRESSING) {
        if (!s_was_pressed)
            ripple(x, y);
        s_was_pressed = true;
        lv_obj_add_flag(s_hint, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_hline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_remove_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_y(s_hline, y);
        lv_obj_set_x(s_vline, x);
        lv_label_set_text_fmt(s_coords, "X %3d   Y %3d", (int) p.x, (int) p.y);

        /* trail: reuse a small pool of dots that fade out */
        lv_obj_t *d = s_trail[s_trail_next];
        s_trail_next = (s_trail_next + 1) % TRAIL_N;
        lv_obj_set_pos(d, x - 4, y - 4);
        lv_anim_t an;
        lv_anim_init(&an);
        lv_anim_set_var(&an, d);
        lv_anim_set_exec_cb(&an, fade_out_cb);
        lv_anim_set_values(&an, LV_OPA_COVER, LV_OPA_TRANSP);
        lv_anim_set_duration(&an, 600);
        lv_anim_start(&an);
    } else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST) {
        s_was_pressed = false;
        lv_obj_add_flag(s_hline, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);
    }
}

lv_obj_t *ui_touch_create(lv_obj_t *parent)
{
    s_touch_page = ui_box(parent, 0, 0, UI_W, UI_CONTENT_H, UI_BG);
    lv_obj_add_flag(s_touch_page, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_touch_page, touch_event, LV_EVENT_ALL, NULL);

    /* faint 20 px grid */
    for (int32_t x = 20; x < UI_W; x += 20)
        ui_box(s_touch_page, x, 0, 1, UI_CONTENT_H, lv_color_hex(0x0d1117));
    for (int32_t y = 20; y < UI_CONTENT_H; y += 20)
        ui_box(s_touch_page, 0, y, UI_W, 1, lv_color_hex(0x0d1117));

    for (int i = 0; i < TRAIL_N; i++) {
        s_trail[i] = ui_box(s_touch_page, -20, -20, 8, 8, UI_ACCENT);
        lv_obj_set_style_radius(s_trail[i], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_opa(s_trail[i], LV_OPA_TRANSP, 0);
    }
    s_hline = ui_box(s_touch_page, 0, 0, UI_W, 1, UI_ACCENT);
    s_vline = ui_box(s_touch_page, 0, 0, 1, UI_CONTENT_H, UI_ACCENT);
    lv_obj_set_style_bg_opa(s_hline, LV_OPA_60, 0);
    lv_obj_set_style_bg_opa(s_vline, LV_OPA_60, 0);
    lv_obj_add_flag(s_hline, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(s_vline, LV_OBJ_FLAG_HIDDEN);

    s_coords = ui_label(s_touch_page, "X  --   Y  --", UI_FONT_M, UI_TEXT);
    lv_obj_align(s_coords, LV_ALIGN_TOP_RIGHT, -8, 6);
    lv_obj_t *t = ui_label(s_touch_page, "TOUCH TEST", UI_FONT_S, UI_MUTED);
    lv_obj_set_pos(t, 8, 8);
    s_hint = ui_label(s_touch_page, "Touch and drag anywhere", UI_FONT_L, UI_DIM);
    lv_obj_center(s_hint);

    /* children must not swallow the presses */
    for (uint32_t i = 0; i < lv_obj_get_child_count(s_touch_page); i++)
        lv_obj_remove_flag(lv_obj_get_child(s_touch_page, (int32_t) i), LV_OBJ_FLAG_CLICKABLE);
    return s_touch_page;
}

/* -------------------------------- Settings -------------------------------- */

static lv_obj_t *s_bright_val;

static void style_button(lv_obj_t *b, bool checkable)
{
    lv_obj_remove_style_all(b);
    lv_obj_set_style_radius(b, 6, 0);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x151a21), 0);
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(0x232a33), 0);
    lv_obj_set_style_text_color(b, lv_color_hex(0xc9d1d9), 0);
    lv_obj_set_style_text_font(b, UI_FONT_M, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x1f2833), LV_STATE_PRESSED);
    if (checkable) {
        lv_obj_add_flag(b, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_set_style_bg_color(b, lv_color_hex(0x0b2f38), LV_STATE_CHECKED);
        lv_obj_set_style_border_color(b, UI_ACCENT, LV_STATE_CHECKED);
        lv_obj_set_style_text_color(b, UI_ACCENT, LV_STATE_CHECKED);
    }
}

static void bright_cb(lv_event_t *e)
{
    lv_obj_t *s = lv_event_get_target(e);
    const int32_t v = lv_slider_get_value(s);
    lv_label_set_text_fmt(s_bright_val, "%d %%", (int) v);
    ui_plat_set_brightness((uint8_t) v);
}

/* A row of mutually exclusive buttons; user_data of each button carries its value */
static void choice_cb(lv_event_t *e)
{
    lv_obj_t *b = lv_event_get_target(e);
    lv_obj_t *row = lv_obj_get_parent(b);
    for (uint32_t i = 0; i < lv_obj_get_child_count(row); i++)
        lv_obj_set_state(lv_obj_get_child(row, (int32_t) i), LV_STATE_CHECKED, lv_obj_get_child(row, (int32_t) i) == b);
    void (*apply)(intptr_t) = lv_event_get_user_data(e);
    apply((intptr_t) lv_obj_get_user_data(b));
}

static void apply_timeout(intptr_t s) { ui_plat_set_screen_timeout((uint16_t) s); }
static void apply_speed(intptr_t tenths) { ui_monitor_set_speed((float) tenths / 100.0f); }

static void choice_row(lv_obj_t *page, int32_t y, const char *const *labels, const intptr_t *values, int n,
                       intptr_t current, void (*apply)(intptr_t))
{
    lv_obj_t *row = ui_box(page, 12, y, UI_W - 24, 30, UI_BG);
    const int32_t w = (UI_W - 24 - (n - 1) * 8) / n;
    for (int i = 0; i < n; i++) {
        lv_obj_t *b = lv_button_create(row);
        style_button(b, true);
        lv_obj_set_pos(b, i * (w + 8), 0);
        lv_obj_set_size(b, w, 30);
        lv_obj_set_user_data(b, (void *) values[i]);
        if (values[i] == current)
            lv_obj_add_state(b, LV_STATE_CHECKED);
        lv_obj_remove_flag(b, LV_OBJ_FLAG_CHECKABLE);    /* choice_cb sets the states */
        lv_obj_add_event_cb(b, choice_cb, LV_EVENT_CLICKED, (void *) apply);
        lv_obj_t *l = lv_label_create(b);
        lv_label_set_text(l, labels[i]);
        lv_obj_center(l);
    }
}

static lv_obj_t *caption(lv_obj_t *page, const char *text, int32_t y)
{
    lv_obj_t *l = ui_label(page, text, UI_FONT_S, UI_MUTED);
    lv_obj_set_pos(l, 14, y);
    return l;
}

lv_obj_t *ui_settings_create(lv_obj_t *parent)
{
    lv_obj_t *page = ui_box(parent, 0, 0, UI_W, UI_CONTENT_H, UI_BG);

    caption(page, "DISPLAY BRIGHTNESS", 8);
    s_bright_val = ui_label(page, "", UI_FONT_S, UI_TEXT);
    lv_obj_align(s_bright_val, LV_ALIGN_TOP_RIGHT, -14, 8);
    lv_obj_t *sl = lv_slider_create(page);
    lv_obj_set_pos(sl, 22, 32);
    lv_obj_set_size(sl, UI_W - 44, 6);
    lv_slider_set_range(sl, 1, 100);
    lv_slider_set_value(sl, ui_plat_brightness(), LV_ANIM_OFF);
    lv_label_set_text_fmt(s_bright_val, "%d %%", (int) ui_plat_brightness());
    lv_obj_set_style_bg_color(sl, lv_color_hex(0x232a33), LV_PART_MAIN);
    lv_obj_set_style_bg_color(sl, UI_ACCENT, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(sl, UI_TEXT, LV_PART_KNOB);
    lv_obj_set_style_pad_all(sl, 8, LV_PART_KNOB);
    lv_obj_set_ext_click_area(sl, 14);
    lv_obj_add_event_cb(sl, bright_cb, LV_EVENT_VALUE_CHANGED, NULL);

    static const char *const tlabels[3] = { "1 min", "5 min", "Never" };
    static const intptr_t tvalues[3] = { 60, 300, 0 };
    caption(page, "SCREEN TIMEOUT", 54);
    choice_row(page, 70, tlabels, tvalues, 3, ui_plat_screen_timeout(), apply_timeout);

    static const char *const slabels[3] = { "6.25", "12.5", "25 mm/s" };
    static const intptr_t svalues[3] = { 625, 1250, 2500 };
    caption(page, "WAVEFORM SPEED", 110);
    choice_row(page, 126, slabels, svalues, 3, 1250, apply_speed);

    lv_obj_t *about = ui_label(page, "", UI_FONT_S, UI_DIM);
    lv_label_set_text_fmt(about, "Firmware %s  " LV_SYMBOL_BULLET "  USB display 1d50:614d", ui_plat_fw_version());
    lv_obj_set_pos(about, 14, 166);
    return page;
}
