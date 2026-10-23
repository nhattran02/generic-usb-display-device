#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

esp_err_t lcd_init(void);
/*
 * RGB565 shadow framebuffer in PSRAM (BOARD_LCD_W * BOARD_LCD_H * 2 bytes, host byte order = little endian).
 * The ILI9341 keeps the picture in its own GRAM, so changes only reach the glass through lcd_flush().
 */
uint8_t *lcd_framebuffer(void);
/* Push a rectangle of the framebuffer to the panel (blocks until it has been sent). Thread safe. */
void lcd_flush(unsigned x, unsigned y, unsigned w, unsigned h);
void lcd_flush_all(void);
/* Panel sleep (display off + sleep in) / wake up; the GRAM contents are kept */
void lcd_sleep(bool sleep);
void lcd_draw_test_pattern(void);
