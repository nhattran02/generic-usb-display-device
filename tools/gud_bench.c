/*
 * Throughput benchmark for the GUD display.
 *
 * The Linux gud driver flushes asynchronously and merges damage when the application draws faster than
 * the device can take it, so the frame rate is measured on the device: the firmware counts every shown
 * rectangle and how long each stage took (USB_REQ_VENDOR_STATS, see main/gud/gud_driver.h).
 *
 *   sudo apt install gcc libdrm-dev
 *   gcc -O2 gud_bench.c -o gud_bench $(pkg-config --cflags --libs libdrm)
 *   sudo ./gud_bench                      all synthetic tests, 8 s each
 *   sudo ./gud_bench -t 15 noise ui       selected tests, 15 s each
 *   ffmpeg -i clip.mp4 -vf scale=320:240 -pix_fmt bgr0 -f rawvideo - | sudo ./gud_bench video
 *       (add -re before -i for real-time playback instead of "as fast as possible")
 *
 * Tests: solid (full screen, best-case compression), ui (scrolling flat-colour page),
 *        gradient (moving smooth gradients), noise (incompressible, worst case),
 *        square (40x40 moving square, partial updates), video (raw XRGB frames on stdin)
 * Options: -d /dev/dri/cardN, -t seconds
 */
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <time.h>
#include <unistd.h>
#include <linux/usbdevice_fs.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define GUD_VID              0x1d50
#define GUD_PID              0x614d
#define USB_REQ_VENDOR_STATS 0xB1

struct __attribute__((packed)) dev_stats {
    uint32_t version;
    uint32_t rects;
    uint64_t pixels, raw_bytes, wire_bytes, usb_us, decode_us, flush_us, uptime_us;
};

static int drm_fd, usb_fd = -1;
static uint32_t *px, W, H, stride, fb_id;
static int seconds = 8;

/* ------------------------------- helpers ------------------------------- */

static double now(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static uint32_t rng = 0x12345678;
static inline uint32_t xorshift(void)
{
    rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
    return rng;
}

static int read_sysfs(const char *dir, const char *name, int base)
{
    char path[512], buf[16] = "";
    snprintf(path, sizeof(path), "/sys/bus/usb/devices/%s/%s", dir, name);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    if (!fgets(buf, sizeof(buf), f)) buf[0] = 0;
    fclose(f);
    return (int) strtol(buf, NULL, base);
}

/* usbfs node of the display; control requests to the device need no claimed interface */
static int open_usb(void)
{
    DIR *d = opendir("/sys/bus/usb/devices");
    struct dirent *e;
    int fd = -1;
    while (d && (e = readdir(d))) {
        if (e->d_name[0] == '.' || strchr(e->d_name, ':'))
            continue;
        if (read_sysfs(e->d_name, "idVendor", 16) != GUD_VID || read_sysfs(e->d_name, "idProduct", 16) != GUD_PID)
            continue;
        char path[64];
        snprintf(path, sizeof(path), "/dev/bus/usb/%03d/%03d",
                 read_sysfs(e->d_name, "busnum", 10), read_sysfs(e->d_name, "devnum", 10));
        fd = open(path, O_RDWR);
        break;
    }
    if (d) closedir(d);
    return fd;
}

static int get_stats(struct dev_stats *s)
{
    memset(s, 0, sizeof(*s));
    if (usb_fd < 0)
        return -1;
    struct usbdevfs_ctrltransfer ct = {
        .bRequestType = 0xC0, .bRequest = USB_REQ_VENDOR_STATS,
        .wValue = 0, .wIndex = 0, .wLength = sizeof(*s), .timeout = 1000, .data = s,
    };
    return ioctl(usb_fd, USBDEVFS_CONTROL, &ct) == (int) sizeof(*s) && s->version == 1 ? 0 : -1;
}

static void dirty(uint32_t x, uint32_t y, uint32_t w, uint32_t h)
{
    drmModeClip c = { (uint16_t) x, (uint16_t) y, (uint16_t) (x + w), (uint16_t) (y + h) };
    drmModeDirtyFB(drm_fd, fb_id, &c, 1);
}

/* -------------------------------- tests -------------------------------- */

static void draw_solid(int f)
{
    static const uint32_t c[6] = { 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0x00FFFF, 0xFFFFFF };
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            px[y * stride + x] = c[f % 6];
    dirty(0, 0, W, H);
}

/* A "page" of flat-colour rows, buttons and text-like dashes, scrolled up 3 px per frame */
static uint32_t ui_pixel(uint32_t x, uint32_t y)
{
    uint32_t block = y / 40, in = y % 40;
    if (in < 2) return 0xC0C0C0;                                    /* separator */
    if (in >= 12 && in < 28 && x >= 16 && x < 16 + 24) return 0x3060C0 + block * 0x0F0703;   /* icon */
    if (in >= 14 && in < 18 && x >= 52 && x < W - 40 && ((x / 6 + block) % 7) < 5) return 0x202020; /* text */
    if (in >= 22 && in < 25 && x >= 52 && x < W - 90 && ((x / 5 + block * 3) % 9) < 6) return 0x707070;
    return block & 1 ? 0xF4F4F4 : 0xFFFFFF;
}

static void draw_ui(int f)
{
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            px[y * stride + x] = ui_pixel(x, y + (uint32_t) f * 3);
    dirty(0, 0, W, H);
}

static void draw_gradient(int f)
{
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++) {
            uint8_t r = (uint8_t) (x + f * 2), g = (uint8_t) (y + f), b = (uint8_t) ((x + y) / 2 - f * 3);
            px[y * stride + x] = (uint32_t) r << 16 | (uint32_t) g << 8 | b;
        }
    dirty(0, 0, W, H);
}

static void draw_noise(int f)
{
    (void) f;
    for (uint32_t y = 0; y < H; y++)
        for (uint32_t x = 0; x < W; x++)
            px[y * stride + x] = xorshift() & 0xFFFFFF;
    dirty(0, 0, W, H);
}

static void draw_square(int f)
{
    static int x, y, dx = 3, dy = 2;
    const int sq = 40;
    if (f == 0) {
        memset(px, 0, (size_t) stride * H * 4);
        dirty(0, 0, W, H);
    }
    for (int j = 0; j < sq; j++)                    /* erase */
        memset(&px[(y + j) * stride + x], 0, sq * 4);
    int ox = x, oy = y;
    x += dx; y += dy;
    if (x < 0 || x + sq > (int) W) { dx = -dx; x += 2 * dx; }
    if (y < 0 || y + sq > (int) H) { dy = -dy; y += 2 * dy; }
    for (int j = 0; j < sq; j++)
        for (int i = 0; i < sq; i++)
            px[(y + j) * stride + x + i] = 0x00FF00 + (uint32_t) (f & 0xFF);
    /* one bounding rectangle, as a compositor would send */
    int x0 = ox < x ? ox : x, y0 = oy < y ? oy : y;
    dirty((uint32_t) x0, (uint32_t) y0, (uint32_t) (abs(x - ox) + sq), (uint32_t) (abs(y - oy) + sq));
}

static uint8_t *video_frame;
static int draw_video(int f)
{
    (void) f;
    size_t need = (size_t) W * H * 4, got = 0;
    while (got < need) {
        ssize_t n = read(0, video_frame + got, need - got);
        if (n <= 0) return -1;
        got += (size_t) n;
    }
    for (uint32_t y = 0; y < H; y++)
        memcpy(&px[y * stride], video_frame + (size_t) y * W * 4, W * 4);
    dirty(0, 0, W, H);
    return 0;
}

/* ------------------------------- runner -------------------------------- */

static void report(const char *name, int frames, double t, const struct dev_stats *a, const struct dev_stats *b)
{
    uint32_t n = b->rects - a->rects;
    printf("%-9s host %6.1f fps", name, frames / t);
    if (b->version == 0) {
        printf("   (no device stats: old firmware or no access to /dev/bus/usb)\n");
        return;
    }
    double wire = (double) (b->wire_bytes - a->wire_bytes), raw = (double) (b->raw_bytes - a->raw_bytes);
    printf(" | device %6.1f fps  %6.1f Mpx/s  USB %6.0f KB/s  LZ4 %4.1fx",
           n / t, (b->pixels - a->pixels) / t / 1e6, wire / t / 1024, wire ? raw / wire : 0.0);
    if (n)
        printf(" | per frame: usb %5.1f  decode %5.1f  lcd %5.1f ms",
               (b->usb_us - a->usb_us) / 1e3 / n, (b->decode_us - a->decode_us) / 1e3 / n,
               (b->flush_us - a->flush_us) / 1e3 / n);
    printf("\n");
}

static void run(const char *name)
{
    struct dev_stats a, b;
    int is_video = !strcmp(name, "video");
    void (*fn)(int) = !strcmp(name, "solid") ? draw_solid : !strcmp(name, "ui") ? draw_ui :
                      !strcmp(name, "gradient") ? draw_gradient : !strcmp(name, "noise") ? draw_noise :
                      !strcmp(name, "square") ? draw_square : NULL;
    if (!fn && !is_video) {
        fprintf(stderr, "unknown test '%s'\n", name);
        return;
    }
    if (is_video && !video_frame)
        video_frame = malloc((size_t) W * H * 4);

    /* warm-up so the previous test's queued damage doesn't count */
    if (fn) { fn(0); usleep(300000); }
    get_stats(&a);
    double t0 = now(), last = t0, t;
    int frames = 0;
    for (;;) {
        if (is_video) {
            if (draw_video(frames) < 0) break;
        } else {
            fn(frames);
            usleep(2000);   /* stays far above what USB full speed can carry: the device sets the pace */
        }
        frames++;
        t = now();
        if (fn && t - t0 >= seconds) break;
        if (is_video && t - last >= 2.0) {          /* running figures while the video plays */
            get_stats(&b);
            report("video", frames, t - t0, &a, &b);
            last = t;
        }
    }
    usleep(200000);                                 /* let the last flush land */
    get_stats(&b);
    report(name, frames, now() - t0, &a, &b);
}

int main(int argc, char **argv)
{
    const char *card = "/dev/dri/card1";
    int opt;
    while ((opt = getopt(argc, argv, "d:t:")) != -1) {
        if (opt == 'd') card = optarg;
        else if (opt == 't') seconds = atoi(optarg);
        else { fprintf(stderr, "usage: %s [-d card] [-t s] [test...]\n", argv[0]); return 1; }
    }

    drm_fd = open(card, O_RDWR | O_CLOEXEC);
    if (drm_fd < 0) { perror(card); return 1; }
    drmModeRes *res = drmModeGetResources(drm_fd);
    if (!res) { perror("drmModeGetResources"); return 1; }
    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(drm_fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes) conn = c;
        else drmModeFreeConnector(c);
    }
    if (!conn) { fprintf(stderr, "no connected connector\n"); return 1; }
    drmModeModeInfo mode = conn->modes[0];
    W = mode.hdisplay;
    H = mode.vdisplay;

    struct drm_mode_create_dumb cd = { .width = W, .height = H, .bpp = 32 };
    struct drm_mode_map_dumb md = { 0 };
    if (drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd) ||
        drmModeAddFB(drm_fd, W, H, 24, 32, cd.pitch, cd.handle, &fb_id) ||
        (md.handle = cd.handle, drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &md))) {
        perror("dumb buffer");
        return 1;
    }
    px = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd, md.offset);
    if (px == MAP_FAILED) { perror("mmap"); return 1; }
    stride = cd.pitch / 4;
    memset(px, 0, cd.size);
    if (drmModeSetCrtc(drm_fd, res->crtcs[0], fb_id, 0, 0, &conn->connector_id, 1, &mode)) {
        fprintf(stderr, "drmModeSetCrtc: %s (stop Xorg/gdm first)\n", strerror(errno));
        return 1;
    }

    usb_fd = open_usb();
    struct dev_stats s;
    if (get_stats(&s))
        fprintf(stderr, "warning: device stats unavailable, only host fps is shown\n");
    printf("%ux%u, XRGB8888 -> RGB565 on the wire, %d s per test\n", W, H, seconds);

    if (optind < argc) {
        for (int i = optind; i < argc; i++)
            run(argv[i]);
    } else {
        const char *all[] = { "solid", "ui", "gradient", "noise", "square" };
        for (unsigned i = 0; i < sizeof(all) / sizeof(all[0]); i++)
            run(all[i]);
    }
    return 0;
}
