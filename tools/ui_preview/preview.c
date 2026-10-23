/* PC preview of the standalone UI (tools/ui_preview/build.sh): renders pages to PPM and reports how much
 * of the screen is flushed per second; ui_platform.h is stubbed with fixed values */
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "lvgl.h"
#include "ui_app.h"
#include "ui_platform.h"

static uint32_t s_ms;
static uint16_t s_fb[240][320];
static uint64_t s_flushed_px;
static int s_flushes;
static bool s_pressed;
static int s_px, s_py;

float ui_plat_chip_temp(void) { return 41.5f; }
uint32_t ui_plat_uptime_s(void) { return 754 + s_ms / 1000; }
size_t ui_plat_free_internal(void) { return 141 * 1024; }
size_t ui_plat_free_psram(void) { return 7782 * 1024; }
const char *ui_plat_fw_version(void) { return "v2.1.0"; }
bool ui_plat_usb_connected(void) { return false; }
static uint8_t s_bright = 60;
uint8_t ui_plat_brightness(void) { return s_bright; }
void ui_plat_set_brightness(uint8_t l) { s_bright = l; }
static uint16_t s_timeout = 300;
uint16_t ui_plat_screen_timeout(void) { return s_timeout; }
void ui_plat_set_screen_timeout(uint16_t s) { s_timeout = s; }

static uint32_t tick(void) { return s_ms; }

static void flush(lv_display_t *d, const lv_area_t *a, uint8_t *px)
{
    const uint16_t *p = (const uint16_t *) px;
    for (int y = a->y1; y <= a->y2; y++)
        for (int x = a->x1; x <= a->x2; x++)
            s_fb[y][x] = *p++;
    s_flushed_px += (uint64_t) lv_area_get_size(a);
    s_flushes++;
    lv_display_flush_ready(d);
}

static void read(lv_indev_t *i, lv_indev_data_t *d)
{
    (void) i;
    d->point.x = s_px;
    d->point.y = s_py;
    d->state = s_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
}

static void run(uint32_t ms)
{
    for (uint32_t t = 0; t < ms; t += 5) {
        s_ms += 5;
        lv_timer_handler();
    }
}

static void tap(int x, int y)
{
    s_px = x; s_py = y; s_pressed = true; run(60);
    s_pressed = false; run(60);
}

static void shot(const char *dir, const char *name)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s.ppm", dir, name);
    FILE *f = fopen(path, "wb");
    fprintf(f, "P6\n320 240\n255\n");
    for (int y = 0; y < 240; y++)
        for (int x = 0; x < 320; x++) {
            uint16_t c = s_fb[y][x];
            unsigned char rgb[3] = { (unsigned char) ((c >> 11) << 3 | (c >> 13)),
                                     (unsigned char) (((c >> 5) & 63) << 2 | ((c >> 9) & 3)),
                                     (unsigned char) ((c & 31) << 3 | ((c >> 2) & 7)) };
            fwrite(rgb, 1, 3, f);
        }
    fclose(f);
}

/* Average flushed area per refresh while a page just runs (steady state) */
static void measure(const char *name, uint32_t ms)
{
    s_flushed_px = 0;
    s_flushes = 0;
    const uint32_t refreshes_before = s_ms;
    run(ms);
    (void) refreshes_before;
    const double per_s = (double) s_flushed_px / (ms / 1000.0);
    printf("%-9s %6.0f px/s flushed = %5.1f%% of the screen per second, SPI busy ~%4.1f%%\n", name, per_s,
           per_s * 100.0 / (320.0 * 240.0), per_s * 2 * 8 / 40e6 * 100.0);
}

int main(int argc, char **argv)
{
    const char *dir = argc > 1 ? argv[1] : ".";
    lv_init();
    lv_tick_set_cb(tick);
    lv_display_t *d = lv_display_create(320, 240);
    static uint16_t buf[320 * 24];
    lv_display_set_buffers(d, buf, NULL, sizeof(buf), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush);
    lv_indev_t *in = lv_indev_create();
    lv_indev_set_type(in, LV_INDEV_TYPE_POINTER);
    lv_indev_set_read_cb(in, read);

    ui_app_create();
    run(1200);
    shot(dir, "home");
    measure("home", 3000);

    tap(120, 222);              /* MONITOR */
    run(6000);
    shot(dir, "monitor");
    measure("monitor", 4000);

    tap(200, 222);              /* TOUCH */
    run(300);
    s_pressed = true;
    for (int i = 0; i <= 40; i++) {
        s_px = 60 + i * 5;
        s_py = 60 + (int) (40 * sin(i / 6.0));
        run(16);
    }
    shot(dir, "touch");
    s_pressed = false;
    run(800);

    tap(280, 222);              /* SETTINGS */
    run(300);
    shot(dir, "settings");
    return 0;
}
