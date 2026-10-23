#include <stdlib.h>
#include <string.h>
#include "ui_internal.h"

#define WAVE_GAP    10          /* blank columns ahead of the cursor */
#define WAVE_TOP    14          /* room for the label */

typedef struct {
    int32_t w, h;
    int16_t *y;                 /* trace y per column, relative to the widget */
    uint8_t *valid;
    int32_t cursor;             /* next column to write */
    float lo, hi;
    lv_color_t color;
} wave_t;

static void wave_draw(lv_event_t *e)
{
    lv_obj_t *obj = lv_event_get_target(e);
    wave_t *wv = lv_obj_get_user_data(obj);
    lv_layer_t *layer = lv_event_get_layer(e);
    lv_area_t a;
    lv_obj_get_coords(obj, &a);

    /* only the columns inside the area being redrawn (plus one to join the neighbours) */
    int32_t x0 = layer->_clip_area.x1 - a.x1, x1 = layer->_clip_area.x2 - a.x1 + 1;
    if (x0 < 1) x0 = 1;
    if (x1 > wv->w - 1) x1 = wv->w - 1;

    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = wv->color;
    d.width = 2;
    d.round_start = 1;
    d.round_end = 1;
    for (int32_t x = x0; x <= x1; x++) {
        if (!wv->valid[x] || !wv->valid[x - 1])
            continue;
        d.p1.x = a.x1 + x - 1;
        d.p1.y = a.y1 + wv->y[x - 1];
        d.p2.x = a.x1 + x;
        d.p2.y = a.y1 + wv->y[x];
        lv_draw_line(layer, &d);
    }
}

static void wave_delete(lv_event_t *e)
{
    wave_t *wv = lv_obj_get_user_data(lv_event_get_target(e));
    free(wv->y);
    free(wv->valid);
    free(wv);
}

lv_obj_t *ui_wave_create(lv_obj_t *parent, int32_t x, int32_t y, int32_t w, int32_t h, lv_color_t color,
                         const char *label, float lo, float hi)
{
    lv_obj_t *obj = ui_box(parent, x, y, w, h, UI_BG);
    lv_obj_set_style_border_side(obj, LV_BORDER_SIDE_BOTTOM, 0);
    lv_obj_set_style_border_width(obj, 1, 0);
    lv_obj_set_style_border_color(obj, UI_LINE, 0);

    wave_t *wv = calloc(1, sizeof(*wv));
    wv->w = w;
    wv->h = h;
    wv->y = calloc((size_t) w, sizeof(int16_t));
    wv->valid = calloc((size_t) w, 1);
    wv->lo = lo;
    wv->hi = hi;
    wv->color = color;
    lv_obj_set_user_data(obj, wv);
    lv_obj_add_event_cb(obj, wave_draw, LV_EVENT_DRAW_MAIN, NULL);
    lv_obj_add_event_cb(obj, wave_delete, LV_EVENT_DELETE, NULL);

    lv_obj_t *l = ui_label(obj, label, UI_FONT_S, color);
    lv_obj_set_pos(l, 5, 1);
    return obj;
}

static void invalidate_cols(lv_obj_t *obj, int32_t from, int32_t to)
{
    lv_area_t a;
    lv_obj_get_coords(obj, &a);
    lv_area_t r = { a.x1 + from, a.y1 + WAVE_TOP - 2, a.x1 + to, a.y2 - 1 };
    lv_obj_invalidate_area(obj, &r);
}

void ui_wave_push(lv_obj_t *obj, float sample)
{
    wave_t *wv = lv_obj_get_user_data(obj);
    const int32_t top = WAVE_TOP, bottom = wv->h - 4;
    float n = (sample - wv->lo) / (wv->hi - wv->lo);
    int32_t y = bottom - (int32_t) (n * (float) (bottom - top) + 0.5f);
    if (y < top) y = top;
    if (y > bottom) y = bottom;

    const int32_t x = wv->cursor;
    wv->y[x] = (int16_t) y;
    wv->valid[x] = 1;
    if (x == 0)
        wv->valid[wv->w - 1] = 0;           /* no line back across the wrap */
    for (int g = 1; g <= WAVE_GAP; g++)
        wv->valid[(x + g) % wv->w] = 0;

    /* the new segment and the gap ahead of it; two strips when the gap wraps */
    const int32_t end = x + WAVE_GAP + 1;
    if (end < wv->w) {
        invalidate_cols(obj, x > 0 ? x - 1 : 0, end);
    } else {
        invalidate_cols(obj, x > 0 ? x - 1 : 0, wv->w - 1);
        invalidate_cols(obj, 0, end - wv->w);
    }
    wv->cursor = (x + 1) % wv->w;
}
