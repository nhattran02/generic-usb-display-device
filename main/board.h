#pragma once
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "driver/i2c_master.h"

/*
 * LCDWiki ES3C28P (2.8" ESP32-S3 display, ESP32-S3-WROOM-1-N16R8)
 *   LCD:   ILI9341V, 240x320, 4-wire SPI; RST is tied to the chip EN line
 *   Touch: FT6336G (capacitive, 2 points), I2C
 *   USB:   the Type-C port is wired to the ESP32-S3 native USB (GPIO19/20)
 */

/* ---------------------------------- LCD ---------------------------------- */

#define BOARD_LCD_SPI_HOST   SPI2_HOST
#define BOARD_LCD_CS         10
#define BOARD_LCD_DC         46
#define BOARD_LCD_SCK        12
#define BOARD_LCD_MOSI       11
#define BOARD_LCD_MISO       13
/* GPIO10..13 are the FSPI IO_MUX pins, so 80 MHz works electrically; 40 MHz is safe for every ILI9341 */
#define BOARD_LCD_SPI_HZ     (40 * 1000 * 1000)

/* Native panel size (portrait) */
#define BOARD_PANEL_W        240
#define BOARD_PANEL_H        320

/*
 * Orientation of the picture the host sees, done in the panel (MADCTL), so the framebuffer and the host
 * share one orientation and nothing is rotated in software:
 *   0 = portrait 240x320, 1 = landscape 320x240, 2 = portrait upside down, 3 = landscape upside down.
 * idf.py -DBOARD_LCD_ROTATION=0 build
 */
#ifndef BOARD_LCD_ROTATION
#define BOARD_LCD_ROTATION   1
#endif

#if BOARD_LCD_ROTATION == 1 || BOARD_LCD_ROTATION == 3
#define BOARD_LCD_W          BOARD_PANEL_H
#define BOARD_LCD_H          BOARD_PANEL_W
#else
#define BOARD_LCD_W          BOARD_PANEL_W
#define BOARD_LCD_H          BOARD_PANEL_H
#endif

/* Panel colour quirks: if red and blue are swapped flip BGR, if the picture looks like a negative flip INVERT */
#define BOARD_LCD_BGR        1
#define BOARD_LCD_INVERT     0

/* Active area of the 2.8" panel, mm (portrait) */
#define BOARD_PANEL_W_MM     43
#define BOARD_PANEL_H_MM     58

/* ------------------------------- backlight ------------------------------- */

/* IO45: high = on, PWM through the backlight transistor. -1: no backlight control (always on) */
#define BOARD_BL_PWM_GPIO    45
/* Brightness after power-up, 0..100 (the host reads it as the initial backlight value) */
#define BOARD_BL_DEFAULT_PERCENT  50

/* --------------------------------- touch --------------------------------- */

#define BOARD_I2C_SDA        16
#define BOARD_I2C_SCL        15
#define BOARD_TP_RST         18
#define BOARD_TP_INT         17

esp_err_t board_init(void);
i2c_master_bus_handle_t board_i2c(void);

/* Backlight on/off (keeps the brightness level) */
void board_backlight(bool on);
/* Brightness 0..100 (0 = dark even when on) */
void board_backlight_brightness(uint8_t percent);
