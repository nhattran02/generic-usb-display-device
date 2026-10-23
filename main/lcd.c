/*
 * ILI9341V over 4-wire SPI (esp_lcd panel IO only; the few MIPI DCS commands are sent by hand).
 *
 * Pixels are streamed: lcd_stream_write() swaps RGB565 to the panel's big-endian byte order into one of
 * two DMA bounce buffers in internal SRAM; a full buffer is queued and the caller goes on filling the other
 * one while it is on the wire. The first transfer of a rectangle is RAMWR, the following ones RAMWRC, so
 * the panel doesn't depend on CS staying low between transfers.
 *
 * Buffer reuse needs no completion callback: queuing a transfer with a command waits for the transfers
 * already queued, so once buffer B has been queued, buffer A (queued before it) is free again.
 */
#include <string.h>
#include "lcd.h"
#include "board.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_io_spi.h"
#include "driver/spi_master.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

static const char *TAG = "lcd";

#define FB_BYTES        (BOARD_LCD_W * BOARD_LCD_H * 2)
/* 2 bounce buffers of 16 KB: 3.3 ms on the wire at 40 MHz each, so the fixed cost per transfer stays small */
#define BOUNCE_BYTES    (16 * 1024)
#define BOUNCE_PX       (BOUNCE_BYTES / 2)

/* MIPI DCS / ILI9341 commands */
#define CMD_SWRESET     0x01
#define CMD_SLPIN       0x10
#define CMD_SLPOUT      0x11
#define CMD_INVOFF      0x20
#define CMD_INVON       0x21
#define CMD_DISPOFF     0x28
#define CMD_DISPON      0x29
#define CMD_CASET       0x2A
#define CMD_RASET       0x2B
#define CMD_RAMWR       0x2C
#define CMD_MADCTL      0x36
#define CMD_COLMOD      0x3A
#define CMD_RAMWRC      0x3C    /* write memory continue */

#define MADCTL_MY       0x80
#define MADCTL_MX       0x40
#define MADCTL_MV       0x20
#define MADCTL_BGR      0x08

static esp_lcd_panel_io_handle_t s_io;
static uint8_t *s_fb;
static SemaphoreHandle_t s_lock;

/* stream state, owned by the lock holder */
static uint16_t *s_bounce[2];
static int s_cur;               /* buffer being filled */
static size_t s_fill;           /* pixels in it */
static bool s_first;            /* the next transfer opens the rectangle (RAMWR) */

typedef struct {
    uint8_t cmd;
    uint8_t len;
    uint8_t data[15];
} ili_cmd_t;

/* Power, VCOM and gamma settings of the common ILI9341 reference init */
static const ili_cmd_t s_init_cmds[] = {
    { 0xEF, 3,  { 0x03, 0x80, 0x02 } },
    { 0xCF, 3,  { 0x00, 0xC1, 0x30 } },                     /* power control B */
    { 0xED, 4,  { 0x64, 0x03, 0x12, 0x81 } },               /* power on sequence */
    { 0xE8, 3,  { 0x85, 0x00, 0x78 } },                     /* driver timing A */
    { 0xCB, 5,  { 0x39, 0x2C, 0x00, 0x34, 0x02 } },         /* power control A */
    { 0xF7, 1,  { 0x20 } },                                 /* pump ratio */
    { 0xEA, 2,  { 0x00, 0x00 } },                           /* driver timing B */
    { 0xC0, 1,  { 0x23 } },                                 /* power control 1 */
    { 0xC1, 1,  { 0x10 } },                                 /* power control 2 */
    { 0xC5, 2,  { 0x3E, 0x28 } },                           /* VCOM 1 */
    { 0xC7, 1,  { 0x86 } },                                 /* VCOM 2 */
    { 0x37, 1,  { 0x00 } },                                 /* vertical scroll start = 0 */
    { CMD_COLMOD, 1, { 0x55 } },                            /* 16 bpp */
    { 0xB1, 2,  { 0x00, 0x18 } },                           /* frame rate 79 Hz */
    { 0xB6, 3,  { 0x08, 0x82, 0x27 } },                     /* display function control */
    { 0xF2, 1,  { 0x00 } },                                 /* 3-gamma off */
    { 0x26, 1,  { 0x01 } },                                 /* gamma curve 1 */
    { 0xE0, 15, { 0x0F, 0x31, 0x2B, 0x0C, 0x0E, 0x08, 0x4E, 0xF1, 0x37, 0x07, 0x10, 0x03, 0x0E, 0x09, 0x00 } },
    { 0xE1, 15, { 0x00, 0x0E, 0x14, 0x03, 0x11, 0x07, 0x31, 0xC1, 0x48, 0x08, 0x0F, 0x0C, 0x31, 0x36, 0x0F } },
};

static uint8_t madctl(void)
{
    static const uint8_t rot[4] = {
        MADCTL_MX,                              /* 0: portrait */
        MADCTL_MV,                              /* 1: landscape */
        MADCTL_MY,                              /* 2: portrait, upside down */
        MADCTL_MX | MADCTL_MY | MADCTL_MV,      /* 3: landscape, upside down */
    };
    return (uint8_t) (rot[BOARD_LCD_ROTATION & 3] | (BOARD_LCD_BGR ? MADCTL_BGR : 0));
}

static esp_err_t tx(uint8_t cmd, const void *data, size_t len)
{
    return esp_lcd_panel_io_tx_param(s_io, cmd, data, len);
}

esp_err_t lcd_init(void)
{
    s_lock = xSemaphoreCreateMutex();

    s_fb = heap_caps_calloc(1, FB_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    for (int i = 0; i < 2; i++)
        s_bounce[i] = heap_caps_malloc(BOUNCE_BYTES, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_lock && s_fb && s_bounce[0] && s_bounce[1], ESP_ERR_NO_MEM, TAG, "out of memory");

    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOUNCE_BYTES,
    };
    ESP_RETURN_ON_ERROR(spi_bus_initialize(BOARD_LCD_SPI_HOST, &bus, SPI_DMA_CH_AUTO), TAG, "SPI bus");

    const esp_lcd_panel_io_spi_config_t io_cfg = {
        .cs_gpio_num = BOARD_LCD_CS,
        .dc_gpio_num = BOARD_LCD_DC,
        .spi_mode = 0,
        .pclk_hz = BOARD_LCD_SPI_HZ,
        .trans_queue_depth = 4,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t) BOARD_LCD_SPI_HOST, &io_cfg, &s_io),
                        TAG, "panel IO");

    /* RST is tied to EN: after esp_restart() the panel was not reset, so start from a software reset */
    ESP_RETURN_ON_ERROR(tx(CMD_SWRESET, NULL, 0), TAG, "panel not responding");
    vTaskDelay(pdMS_TO_TICKS(150));

    for (size_t i = 0; i < sizeof(s_init_cmds) / sizeof(s_init_cmds[0]); i++)
        ESP_RETURN_ON_ERROR(tx(s_init_cmds[i].cmd, s_init_cmds[i].data, s_init_cmds[i].len), TAG, "init");

    uint8_t m = madctl();
    tx(CMD_MADCTL, &m, 1);
    tx(BOARD_LCD_INVERT ? CMD_INVON : CMD_INVOFF, NULL, 0);

    /* Clear GRAM before the display is switched on so no power-up garbage is ever visible */
    lcd_flush_all();

    tx(CMD_SLPOUT, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));
    tx(CMD_DISPON, NULL, 0);

    ESP_LOGI(TAG, "ILI9341 %ux%u (rotation %d), SPI %d MHz, framebuffer %p", BOARD_LCD_W, BOARD_LCD_H,
             BOARD_LCD_ROTATION, BOARD_LCD_SPI_HZ / 1000000, s_fb);
    return ESP_OK;
}

uint8_t *lcd_framebuffer(void) { return s_fb; }

/* ------------------------------ streaming ------------------------------ */

/* Queue the filled bounce buffer and switch to the other one (which is free once this returns) */
static void stream_send(void)
{
    if (!s_fill)
        return;
    esp_lcd_panel_io_tx_color(s_io, s_first ? CMD_RAMWR : CMD_RAMWRC, s_bounce[s_cur], s_fill * 2);
    s_first = false;
    s_cur ^= 1;
    s_fill = 0;
}

/* Copy n pixels, swapping the bytes of each one (little endian host -> big endian panel) */
static void swap_copy(uint16_t *dst, const uint16_t *src, size_t n)
{
    if (n && (((uintptr_t) dst ^ (uintptr_t) src) & 2) == 0) {
        if ((uintptr_t) dst & 2) {
            *dst++ = __builtin_bswap16(*src++);
            n--;
        }
        uint32_t *d = (uint32_t *) dst;
        const uint32_t *s = (const uint32_t *) src;
        for (; n >= 8; n -= 8, d += 4, s += 4) {
            uint32_t a = s[0], b = s[1], c = s[2], e = s[3];
            d[0] = ((a & 0x00FF00FF) << 8) | ((a >> 8) & 0x00FF00FF);
            d[1] = ((b & 0x00FF00FF) << 8) | ((b >> 8) & 0x00FF00FF);
            d[2] = ((c & 0x00FF00FF) << 8) | ((c >> 8) & 0x00FF00FF);
            d[3] = ((e & 0x00FF00FF) << 8) | ((e >> 8) & 0x00FF00FF);
        }
        for (; n >= 2; n -= 2, d++, s++) {
            uint32_t a = *s;
            *d = ((a & 0x00FF00FF) << 8) | ((a >> 8) & 0x00FF00FF);
        }
        dst = (uint16_t *) d;
        src = (const uint16_t *) s;
    }
    while (n--)
        *dst++ = __builtin_bswap16(*src++);
}

void lcd_stream_begin(unsigned x, unsigned y, unsigned w, unsigned h)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);

    unsigned x1 = x + w - 1, y1 = y + h - 1;
    uint8_t c[4] = { (uint8_t) (x >> 8), (uint8_t) x, (uint8_t) (x1 >> 8), (uint8_t) x1 };
    uint8_t r[4] = { (uint8_t) (y >> 8), (uint8_t) y, (uint8_t) (y1 >> 8), (uint8_t) y1 };
    tx(CMD_CASET, c, 4);                    /* waits for the previous rectangle's last transfer */
    tx(CMD_RASET, r, 4);
    s_first = true;
    s_fill = 0;
}

void lcd_stream_write(const void *pixels, size_t bytes)
{
    const uint16_t *p = pixels;
    size_t n = bytes / 2;

    while (n) {
        size_t c = BOUNCE_PX - s_fill;
        if (c > n)
            c = n;
        swap_copy(s_bounce[s_cur] + s_fill, p, c);
        s_fill += c;
        p += c;
        n -= c;
        if (s_fill == BOUNCE_PX)
            stream_send();
    }
}

void lcd_stream_end(void)
{
    stream_send();
    xSemaphoreGive(s_lock);
}

/* ---------------------------- framebuffer ------------------------------ */

void lcd_flush(unsigned x, unsigned y, unsigned w, unsigned h)
{
    if (!s_io || !w || !h || x >= BOARD_LCD_W || y >= BOARD_LCD_H)
        return;
    if (x + w > BOARD_LCD_W) w = BOARD_LCD_W - x;
    if (y + h > BOARD_LCD_H) h = BOARD_LCD_H - y;

    lcd_stream_begin(x, y, w, h);
    if (x == 0 && w == BOARD_LCD_W) {
        lcd_stream_write(s_fb + (size_t) y * BOARD_LCD_W * 2, (size_t) w * h * 2);
    } else {
        for (unsigned row = y; row < y + h; row++)
            lcd_stream_write(s_fb + ((size_t) row * BOARD_LCD_W + x) * 2, (size_t) w * 2);
    }
    lcd_stream_end();
}

void lcd_flush_all(void)
{
    lcd_flush(0, 0, BOARD_LCD_W, BOARD_LCD_H);
}

void lcd_sleep(bool sleep)
{
    xSemaphoreTake(s_lock, portMAX_DELAY);
    if (sleep) {
        tx(CMD_DISPOFF, NULL, 0);
        tx(CMD_SLPIN, NULL, 0);
    } else {
        tx(CMD_SLPOUT, NULL, 0);
        vTaskDelay(pdMS_TO_TICKS(120));     /* also covers the 120 ms SLPOUT -> SLPIN rule */
        tx(CMD_DISPON, NULL, 0);
    }
    xSemaphoreGive(s_lock);
}

void lcd_draw_test_pattern(void)
{
    static const uint16_t bars[8] = { 0xFFFF, 0xFFE0, 0x07FF, 0x07E0, 0xF81F, 0xF800, 0x001F, 0x0000 };
    uint16_t *px = (uint16_t *) s_fb;
    for (int y = 0; y < BOARD_LCD_H; y++) {
        for (int x = 0; x < BOARD_LCD_W; x++) {
            uint16_t c = bars[x * 8 / BOARD_LCD_W];
            if (y >= BOARD_LCD_H * 3 / 4) {          /* bottom quarter: grey ramp */
                uint16_t g = (uint16_t) (x * 32 / BOARD_LCD_W);
                c = (uint16_t) ((g << 11) | ((g * 2) << 5) | g);
            }
            if (x == 0 || y == 0 || x == BOARD_LCD_W - 1 || y == BOARD_LCD_H - 1)
                c = 0xFFFF;                             /* 1px border: shows any cropping */
            px[y * BOARD_LCD_W + x] = c;
        }
    }
    lcd_flush_all();
}
