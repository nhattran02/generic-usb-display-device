// SPDX-License-Identifier: MIT
// Copyright (c) 2026 Vladimir Avtsenov
// Based on driver.c from notro/gud-pico, Copyright (c) 2021-2024 Noralf Trønnes
#pragma once
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"
#include "gud.h"
#include "gud_lz4.h"        /* GUD_LZ4_WINDOW_SIZE */

/* Bulk receive buffers: one rectangle is received while the previous one is being drawn */
#define GUD_RX_BUFS          2

/*
 * Bind the display (disp->rect_* must be set), GUD_RX_BUFS bulk receive buffers (each holds one compressed
 * or raw rectangle; buf_size == disp->max_buffer_size) and a GUD_LZ4_WINDOW_SIZE decode window, and start
 * the render task on render_core. Call before the USB stack starts.
 */
esp_err_t gud_driver_setup(const struct gud_display *disp, void *const rx_bufs[GUD_RX_BUFS], size_t buf_size,
                           void *lz4_window, int render_core);

/*
 * Drop rectangles that were received but not drawn yet. Call from the USB task before the firmware draws
 * its own screen over the host's picture (host gone / pipeline disabled), so no stale picture follows it.
 */
void gud_driver_discard(void);

/* Interface number the GUD vendor interface has in the configuration descriptor. */
#define GUD_ITF_NUM   1

/*
 * Performance counters since boot, read by the host with the device-recipient vendor request
 * USB_REQ_VENDOR_STATS (bmRequestType 0xC0, see tools/gud_bench.c). Little endian, packed.
 */
#define GUD_DRIVER_STATS_VERSION  2

struct __attribute__((packed)) gud_driver_stats {
    uint32_t version;
    uint32_t rects;         /* rectangles drawn */
    uint64_t pixels;        /* pixels in those rectangles */
    uint64_t raw_bytes;     /* uncompressed size */
    uint64_t wire_bytes;    /* bytes received over USB */
    uint64_t usb_us;        /* bulk transfer armed -> last packet received */
    uint64_t render_us;     /* render task: LZ4 + byte swap + queuing SPI transfers (overlaps the next USB transfer) */
    uint64_t stall_us;      /* SET_BUFFER -> bulk armed: waiting for a free receive buffer (the panel side is behind) */
    uint64_t uptime_us;
};
