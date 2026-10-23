#pragma once
/* Same language as apps/vitalview: black background, one fixed colour per parameter */
#include "lvgl.h"

#define UI_W            320
#define UI_H            240
#define UI_STATUS_H     18
#define UI_NAV_H        36
#define UI_CONTENT_H    (UI_H - UI_STATUS_H - UI_NAV_H)     /* 186 */

#define UI_BG           lv_color_hex(0x000000)
#define UI_PANEL        lv_color_hex(0x0b0e13)
#define UI_CARD         lv_color_hex(0x11161d)
#define UI_LINE         lv_color_hex(0x1c2129)
#define UI_TEXT         lv_color_hex(0xe6edf3)
#define UI_MUTED        lv_color_hex(0x8b949e)
#define UI_DIM          lv_color_hex(0x57606a)
#define UI_ACCENT       lv_color_hex(0x33d6f0)
#define UI_ECG          lv_color_hex(0x3ce65a)
#define UI_SPO2         lv_color_hex(0x33d6f0)
#define UI_RESP         lv_color_hex(0xf5d33a)
#define UI_NIBP         lv_color_hex(0xe6edf3)
#define UI_TEMP         lv_color_hex(0xff9f43)
#define UI_AMBER        lv_color_hex(0xf5b841)

#define UI_FONT_S       (&lv_font_montserrat_12)
#define UI_FONT_M       (&lv_font_montserrat_14)
#define UI_FONT_L       (&lv_font_montserrat_20)
#define UI_FONT_XL      (&lv_font_montserrat_28)

/* A plain container: no padding, border, scrolling or theme decoration */
static inline lv_obj_t *ui_box(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t bg)
{
    lv_obj_t *o = lv_obj_create(parent);
    lv_obj_remove_style_all(o);
    lv_obj_set_pos(o, x, y);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, bg, 0);
    lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
    lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
    return o;
}

static inline lv_obj_t *ui_label(lv_obj_t *parent, const char *text, const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *l = lv_label_create(parent);
    lv_label_set_text(l, text);
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, color, 0);
    return l;
}
