/*
 * "<NO SIGNAL>" screen: dark grey background, light grey TV icon and caption.
 * Drawn procedurally (no image assets) into the framebuffer and pushed to the panel; when it is shown and
 * the backlight are up to screen.c.
 */
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "board.h"
#include "lcd.h"
#include "ui.h"

#define COLOR_BG   0x2104   /* #202020 */
#define COLOR_FG   0xB596   /* #B0B0B0 */

/* 5x7 glyphs, one byte per row, bit 4 = leftmost column */
static const struct { char c; uint8_t rows[7]; } s_font[] = {
    { '<', { 0x02, 0x04, 0x08, 0x10, 0x08, 0x04, 0x02 } },
    { '>', { 0x08, 0x04, 0x02, 0x01, 0x02, 0x04, 0x08 } },
    { 'A', { 0x0E, 0x11, 0x11, 0x1F, 0x11, 0x11, 0x11 } },
    { 'G', { 0x0E, 0x11, 0x10, 0x17, 0x11, 0x11, 0x0F } },
    { 'I', { 0x0E, 0x04, 0x04, 0x04, 0x04, 0x04, 0x0E } },
    { 'L', { 0x10, 0x10, 0x10, 0x10, 0x10, 0x10, 0x1F } },
    { 'N', { 0x11, 0x19, 0x15, 0x13, 0x11, 0x11, 0x11 } },
    { 'O', { 0x0E, 0x11, 0x11, 0x11, 0x11, 0x11, 0x0E } },
    { 'S', { 0x0F, 0x10, 0x10, 0x0E, 0x01, 0x01, 0x1E } },
};

static const char s_caption[] = "<NO SIGNAL>";

#define TEXT_SCALE   3
#define TEXT_ADV     (6 * TEXT_SCALE)                 /* 5 columns + 1 spacing */
#define TEXT_H       (7 * TEXT_SCALE)

#define TV_W         120
#define TV_H         84
#define TV_R         11
#define TV_LINE      6
#define ANT_H        33
#define ANT_DX       27
#define ANT_HALF_W   3
#define LEG_W        12
#define LEG_H        8
#define LEG_INSET    24
#define GAP          20

/* Layout in framebuffer coordinates (the panel is already turned by MADCTL) */
static int s_tv_x, s_tv_y, s_text_x, s_text_y;

static const uint8_t *glyph(char c)
{
    for (unsigned i = 0; i < sizeof(s_font) / sizeof(s_font[0]); i++)
        if (s_font[i].c == c)
            return s_font[i].rows;
    return NULL;    /* space */
}

static bool in_rrect(int x, int y, int x0, int y0, int w, int h, int r)
{
    if (x < x0 || y < y0 || x >= x0 + w || y >= y0 + h)
        return false;
    int cx = x < x0 + r ? x0 + r : (x >= x0 + w - r ? x0 + w - r - 1 : x);
    int cy = y < y0 + r ? y0 + r : (y >= y0 + h - r ? y0 + h - r - 1 : y);
    int dx = x - cx, dy = y - cy;
    return dx * dx + dy * dy <= r * r;
}

static bool near_segment(int x, int y, int ax, int ay, int bx, int by, int half_w)
{
    int vx = bx - ax, vy = by - ay, wx = x - ax, wy = y - ay;
    int len2 = vx * vx + vy * vy;
    int t = wx * vx + wy * vy;                      /* projection, scaled by len2 */
    if (t < 0) t = 0;
    if (t > len2) t = len2;
    /* distance^2 from (x,y) to the closest point, in len2-scaled fixed point */
    int64_t px = (int64_t) ax * len2 + (int64_t) vx * t - (int64_t) x * len2;
    int64_t py = (int64_t) ay * len2 + (int64_t) vy * t - (int64_t) y * len2;
    return px * px + py * py <= (int64_t) half_w * half_w * len2 * len2;
}

static bool is_fg(int x, int y)
{
    /* TV body outline */
    if (in_rrect(x, y, s_tv_x, s_tv_y, TV_W, TV_H, TV_R) &&
        !in_rrect(x, y, s_tv_x + TV_LINE, s_tv_y + TV_LINE, TV_W - 2 * TV_LINE, TV_H - 2 * TV_LINE, TV_R - TV_LINE))
        return true;

    /* antennas */
    int ax = s_tv_x + TV_W / 2, ay = s_tv_y;
    if (y < ay && (near_segment(x, y, ax, ay, ax - ANT_DX, ay - ANT_H, ANT_HALF_W) ||
                   near_segment(x, y, ax, ay, ax + ANT_DX, ay - ANT_H, ANT_HALF_W)))
        return true;

    /* legs */
    int ly = s_tv_y + TV_H;
    if (y >= ly && y < ly + LEG_H &&
        ((x >= s_tv_x + LEG_INSET && x < s_tv_x + LEG_INSET + LEG_W) ||
         (x >= s_tv_x + TV_W - LEG_INSET - LEG_W && x < s_tv_x + TV_W - LEG_INSET)))
        return true;

    /* caption */
    if (y >= s_text_y && y < s_text_y + TEXT_H && x >= s_text_x) {
        int i = (x - s_text_x) / TEXT_ADV;
        int col = (x - s_text_x) % TEXT_ADV / TEXT_SCALE;
        if (i < (int) sizeof(s_caption) - 1 && col < 5) {
            const uint8_t *g = glyph(s_caption[i]);
            int row = (y - s_text_y) / TEXT_SCALE;
            if (g && (g[row] & (0x10 >> col)))
                return true;
        }
    }
    return false;
}

void ui_show_no_signal(void)
{
    uint16_t *fb = (uint16_t *) lcd_framebuffer();

    int text_w = (int) (sizeof(s_caption) - 1) * TEXT_ADV - TEXT_SCALE;
    int total_h = ANT_H + TV_H + LEG_H + GAP + TEXT_H;
    s_tv_x = (BOARD_LCD_W - TV_W) / 2;
    s_tv_y = (BOARD_LCD_H - total_h) / 2 + ANT_H;
    s_text_x = (BOARD_LCD_W - text_w) / 2;
    s_text_y = s_tv_y + TV_H + LEG_H + GAP;

    for (int y = 0; y < BOARD_LCD_H; y++)
        for (int x = 0; x < BOARD_LCD_W; x++)
            *fb++ = is_fg(x, y) ? COLOR_FG : COLOR_BG;
    lcd_flush_all();
}
