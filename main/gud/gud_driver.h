// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vladimir Avtsenov
// Based on driver.c from notro/gud-pico, Copyright (c) 2021-2024 Noralf Trønnes
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "gud.h"

/* LZ4 decode window (internal SRAM), needed if disp->compression != 0 */
#define GUD_LZ4_WINDOW_SIZE  (64 * 1024)

/* Bind the display (disp->fb must be set), the bulk receive buffer (holds one compressed or raw
 * rectangle; size == disp->max_buffer_size) and a GUD_LZ4_WINDOW_SIZE decode window. */
void gud_driver_setup(const struct gud_display *disp, void *rx_buf, size_t buf_size, void *lz4_window);

/* Interface number the GUD vendor interface has in the configuration descriptor. */
#define GUD_ITF_NUM   1

/*
 * Performance counters since boot, read by the host with the device-recipient vendor request
 * USB_REQ_VENDOR_STATS (bmRequestType 0xC0, see tools/gud_bench.c). Little endian, packed.
 */
#define GUD_DRIVER_STATS_VERSION  1

struct __attribute__((packed)) gud_driver_stats {
    uint32_t version;
    uint32_t rects;         /* rectangles shown */
    uint64_t pixels;        /* pixels in those rectangles */
    uint64_t raw_bytes;     /* uncompressed size */
    uint64_t wire_bytes;    /* bytes received over USB */
    uint64_t usb_us;        /* bulk armed -> last packet received */
    uint64_t decode_us;     /* LZ4 / copy into the framebuffer */
    uint64_t flush_us;      /* write_buffer (SPI flush to the panel) */
    uint64_t uptime_us;
};
