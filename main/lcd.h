#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t lcd_init(void);

/*
 * Streaming writes: open a rectangle, feed its RGB565 pixels (host byte order = little endian, row-major,
 * any number of calls, each an even number of bytes), close it. Pixels are byte-swapped into DMA bounce
 * buffers and sent while the caller produces the next ones; lcd_stream_end() returns without waiting for
 * the last transfer, the next panel access waits for it. begin..end holds the panel lock (thread safe).
 */
void lcd_stream_begin(unsigned x, unsigned y, unsigned w, unsigned h);
void lcd_stream_write(const void *pixels, size_t bytes);
void lcd_stream_end(void);

/*
 * RGB565 framebuffer in PSRAM (BOARD_LCD_W * BOARD_LCD_H * 2 bytes, little endian) for the firmware's own
 * screens; the ILI9341 keeps the picture in its GRAM, so changes reach the glass through lcd_flush().
 * Pictures from the host are streamed past it (see gud_driver.c).
 */
uint8_t *lcd_framebuffer(void);
void lcd_flush(unsigned x, unsigned y, unsigned w, unsigned h);
void lcd_flush_all(void);

/* Panel sleep (display off + sleep in) / wake up; the GRAM contents are kept */
void lcd_sleep(bool sleep);
void lcd_draw_test_pattern(void);
