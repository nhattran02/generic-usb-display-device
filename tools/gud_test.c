/*
 * Minimal KMS test for the GUD display (works where the distro's modetest is too old to know "gud").
 * Shows colour bars, then moves a square for a while so partial (damage) updates are exercised too.
 *
 *   sudo apt install gcc libdrm-dev
 *   gcc gud_test.c -o gud_test $(pkg-config --cflags --libs libdrm)
 *   sudo ./gud_test [/dev/dri/card1] [seconds]
 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define SQ 40

static uint32_t *px;
static uint32_t W, H, stride;

/* white, red, green, blue, yellow, magenta, cyan, black */
static const uint32_t bars[8] = { 0xFFFFFF, 0xFF0000, 0x00FF00, 0x0000FF, 0xFFFF00, 0xFF00FF, 0x00FFFF, 0x000000 };

static void fill(uint32_t x0, uint32_t y0, uint32_t w, uint32_t h, int square)
{
    for (uint32_t y = y0; y < y0 + h && y < H; y++)
        for (uint32_t x = x0; x < x0 + w && x < W; x++)
            px[y * stride + x] = square ? 0xFFFFFF : bars[x * 8 / W];
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "/dev/dri/card1";
    int seconds = argc > 2 ? atoi(argv[2]) : 20;

    int fd = open(path, O_RDWR | O_CLOEXEC);
    if (fd < 0) { perror(path); return 1; }

    drmVersionPtr ver = drmGetVersion(fd);
    printf("%s: driver %s\n", path, ver ? ver->name : "?");

    drmModeRes *res = drmModeGetResources(fd);
    if (!res) { perror("drmModeGetResources"); return 1; }

    drmModeConnector *conn = NULL;
    for (int i = 0; i < res->count_connectors && !conn; i++) {
        drmModeConnector *c = drmModeGetConnector(fd, res->connectors[i]);
        if (c && c->connection == DRM_MODE_CONNECTED && c->count_modes)
            conn = c;
        else
            drmModeFreeConnector(c);
    }
    if (!conn) { fprintf(stderr, "no connected connector\n"); return 1; }

    drmModeModeInfo mode = conn->modes[0];
    uint32_t crtc = res->crtcs[0];
    W = mode.hdisplay;
    H = mode.vdisplay;
    printf("connector %u, mode %s (%ux%u), crtc %u\n", conn->connector_id, mode.name, W, H, crtc);

    struct drm_mode_create_dumb cd = { .width = W, .height = H, .bpp = 32 };
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &cd)) { perror("CREATE_DUMB"); return 1; }
    uint32_t fb;
    if (drmModeAddFB(fd, W, H, 24, 32, cd.pitch, cd.handle, &fb)) { perror("drmModeAddFB"); return 1; }
    struct drm_mode_map_dumb md = { .handle = cd.handle };
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &md)) { perror("MAP_DUMB"); return 1; }
    px = mmap(NULL, cd.size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, md.offset);
    if (px == MAP_FAILED) { perror("mmap"); return 1; }
    stride = cd.pitch / 4;

    fill(0, 0, W, H, 0);
    if (drmModeSetCrtc(fd, crtc, fb, 0, 0, &conn->connector_id, 1, &mode)) {
        fprintf(stderr, "drmModeSetCrtc: %s (another program, e.g. Xorg, may own the display)\n", strerror(errno));
        return 1;
    }
    printf("colour bars: white red green blue yellow magenta cyan black (left to right)\n");
    sleep(3);

    printf("moving square for %d s\n", seconds);
    int x = 0, y = 0, dx = 4, dy = 3;
    for (int i = 0; i < seconds * 30; i++) {
        fill(x, y, SQ, SQ, 0);                          /* erase */
        drmModeClip clip = { x, y, x + SQ, y + SQ };
        x += dx; y += dy;
        if (x < 0 || x + SQ > (int) W) { dx = -dx; x += 2 * dx; }
        if (y < 0 || y + SQ > (int) H) { dy = -dy; y += 2 * dy; }
        fill(x, y, SQ, SQ, 1);                          /* draw */
        drmModeClip clips[2] = { clip, { x, y, x + SQ, y + SQ } };
        drmModeDirtyFB(fd, fb, clips, 2);
        usleep(33000);
    }
    return 0;
}
