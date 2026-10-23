/*
 * ILI9341V over 4-wire SPI (esp_lcd panel IO only; the few MIPI DCS commands are sent by hand).
 *
 * The host writes rectangles into a PSRAM shadow framebuffer; lcd_flush() then copies them row band by
 * row band into two DMA bounce buffers in internal SRAM, swapping RGB565 to the panel's big-endian byte
 * order on the way. While one band is on the wire the next one is being filled.
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
/* One bounce buffer = 16 full rows (10 KB); 2 of them */
#define BOUNCE_PX       (BOARD_LCD_W * 16)

/* MIPI DCS / ILI9341 commands */
#define CMD_NOP         0x00
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

#define MADCTL_MY       0x80
#define MADCTL_MX       0x40
#define MADCTL_MV       0x20
#define MADCTL_BGR      0x08

static esp_lcd_panel_io_handle_t s_io;
static uint8_t *s_fb;
static uint16_t *s_bounce[2];
static SemaphoreHandle_t s_lock;

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
        s_bounce[i] = heap_caps_malloc(BOUNCE_PX * 2, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
    ESP_RETURN_ON_FALSE(s_fb && s_bounce[0] && s_bounce[1], ESP_ERR_NO_MEM, TAG, "out of memory");

    const spi_bus_config_t bus = {
        .sclk_io_num = BOARD_LCD_SCK,
        .mosi_io_num = BOARD_LCD_MOSI,
        .miso_io_num = BOARD_LCD_MISO,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = BOUNCE_PX * 2,
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

    ESP_LOGI(TAG, "ILI9341 %ux%u (rotation %d) @ %d MHz, framebuffer %p", BOARD_LCD_W, BOARD_LCD_H,
             BOARD_LCD_ROTATION, BOARD_LCD_SPI_HZ / 1000000, s_fb);
    return ESP_OK;
}

uint8_t *lcd_framebuffer(void) { return s_fb; }

static void set_window(unsigned x0, unsigned y0, unsigned x1, unsigned y1)
{
    uint8_t c[4] = { (uint8_t) (x0 >> 8), (uint8_t) x0, (uint8_t) (x1 >> 8), (uint8_t) x1 };
    uint8_t r[4] = { (uint8_t) (y0 >> 8), (uint8_t) y0, (uint8_t) (y1 >> 8), (uint8_t) y1 };
    tx(CMD_CASET, c, 4);
    tx(CMD_RASET, r, 4);
}

void lcd_flush(unsigned x, unsigned y, unsigned w, unsigned h)
{
    if (!s_io || !w || !h || x >= BOARD_LCD_W || y >= BOARD_LCD_H)
        return;
    if (x + w > BOARD_LCD_W) w = BOARD_LCD_W - x;
    if (y + h > BOARD_LCD_H) h = BOARD_LCD_H - y;

    const unsigned band = BOUNCE_PX / w;    /* rows per bounce buffer, >= 1 */
    int cur = 0;

    xSemaphoreTake(s_lock, portMAX_DELAY);
    for (unsigned row = y; row < y + h; row += band, cur ^= 1) {
        unsigned n = (y + h - row) < band ? (y + h - row) : band;
        uint16_t *dst = s_bounce[cur];

        /* Filling this buffer overlaps with the other one being sent: every tx below first waits for
         * the queued colour transfer, so the buffer filled two bands ago is free again here */
        for (unsigned r = 0; r < n; r++) {
            const uint16_t *src = (const uint16_t *) s_fb + (size_t) (row + r) * BOARD_LCD_W + x;
            for (unsigned i = 0; i < w; i++)
                *dst++ = __builtin_bswap16(src[i]);
        }

        set_window(x, row, x + w - 1, row + n - 1);
        esp_lcd_panel_io_tx_color(s_io, CMD_RAMWR, s_bounce[cur], (size_t) w * n * 2);
    }
    tx(CMD_NOP, NULL, 0);                   /* waits for the last band: the buffers are free on return */
    xSemaphoreGive(s_lock);
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
