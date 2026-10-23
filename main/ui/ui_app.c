/* Shell of the standalone UI: status bar, the pages, and the navigation bar along the bottom edge */
#include "ui_app.h"
#include "ui_internal.h"
#include "ui_platform.h"

#define PAGES 4

static lv_obj_t *s_pages[PAGES], *s_nav[PAGES];
static lv_obj_t *s_clock;

static void show_page(int index)
{
    for (int i = 0; i < PAGES; i++) {
        if (i == index)
            lv_obj_remove_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        else
            lv_obj_add_flag(s_pages[i], LV_OBJ_FLAG_HIDDEN);
        lv_obj_set_state(s_nav[i], LV_STATE_CHECKED, i == index);
    }
}

static void nav_cb(lv_event_t *e)
{
    show_page((int) (intptr_t) lv_event_get_user_data(e));
}

static void clock_cb(lv_timer_t *t)
{
    (void) t;
    const uint32_t up = ui_plat_uptime_s();
    lv_label_set_text_fmt(s_clock, "%02u:%02u", (unsigned) (up / 3600), (unsigned) (up / 60 % 60));
}

static lv_obj_t *nav_button(lv_obj_t *bar, int i, const char *text)
{
    const int32_t w = UI_W / PAGES;
    lv_obj_t *b = lv_button_create(bar);
    lv_obj_remove_style_all(b);
    lv_obj_set_pos(b, i * w, 0);
    lv_obj_set_size(b, i == PAGES - 1 ? UI_W - i * w : w - 1, UI_NAV_H);
    lv_obj_set_style_bg_opa(b, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(b, UI_PANEL, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x11161d), LV_STATE_CHECKED);
    lv_obj_set_style_bg_color(b, lv_color_hex(0x161c24), LV_STATE_PRESSED);
    lv_obj_set_style_border_side(b, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(b, 3, 0);
    lv_obj_set_style_border_color(b, UI_PANEL, 0);
    lv_obj_set_style_border_color(b, UI_ACCENT, LV_STATE_CHECKED);
    lv_obj_set_style_text_color(b, UI_MUTED, 0);
    lv_obj_set_style_text_color(b, lv_color_white(), LV_STATE_CHECKED);
    lv_obj_set_style_text_font(b, UI_FONT_S, 0);
    lv_obj_set_style_text_letter_space(b, 1, 0);
    lv_obj_add_event_cb(b, nav_cb, LV_EVENT_CLICKED, (void *) (intptr_t) i);
    lv_obj_t *l = lv_label_create(b);
    lv_label_set_text(l, text);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 1);
    return b;
}

void ui_app_create(void)
{
    lv_obj_t *scr = lv_screen_active();
    lv_obj_remove_style_all(scr);
    lv_obj_set_style_bg_color(scr, UI_BG, 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);
    lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

    /* status bar */
    lv_obj_t *status = ui_box(scr, 0, 0, UI_W, UI_STATUS_H, UI_PANEL);
    lv_obj_t *l = ui_label(status, "ES3C28P", UI_FONT_S, UI_MUTED);
    lv_obj_align(l, LV_ALIGN_LEFT_MID, 6, 0);
    l = ui_label(status, "STANDALONE  " LV_SYMBOL_BULLET "  DEMO", UI_FONT_S, UI_AMBER);
    lv_obj_align(l, LV_ALIGN_CENTER, 0, 0);
    s_clock = ui_label(status, "", UI_FONT_S, UI_TEXT);
    lv_obj_align(s_clock, LV_ALIGN_RIGHT_MID, -6, 0);
    clock_cb(NULL);
    lv_timer_create(clock_cb, 1000, NULL);

    /* pages */
    lv_obj_t *content = ui_box(scr, 0, UI_STATUS_H, UI_W, UI_CONTENT_H, UI_BG);
    s_pages[0] = ui_home_create(content);
    s_pages[1] = ui_monitor_create(content);
    s_pages[2] = ui_touch_create(content);
    s_pages[3] = ui_settings_create(content);

    /* navigation */
    lv_obj_t *nav = ui_box(scr, 0, UI_H - UI_NAV_H, UI_W, UI_NAV_H, UI_LINE);
    static const char *const names[PAGES] = { "HOME", "MONITOR", "TOUCH", "SETTINGS" };
    for (int i = 0; i < PAGES; i++)
        s_nav[i] = nav_button(nav, i, names[i]);

    show_page(0);
}
