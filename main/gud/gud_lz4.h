// SPDX-License-Identifier: MIT
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

/* LZ4 match offsets are 16-bit: a 64 KB ring holds everything a back-reference can reach */
#define GUD_LZ4_WINDOW_SIZE  (64 * 1024)

/* Receives the decoded bytes in order, in runs of an even length (whole RGB565 pixels) */
typedef void (*gud_lz4_sink_t)(void *ctx, const void *data, size_t len);

/*
 * Decode one raw LZ4 block (no frame header) that must expand to exactly out_len bytes, streaming the
 * output to sink while decoding. window: GUD_LZ4_WINDOW_SIZE bytes, preferably internal SRAM.
 * Returns false on malformed input; what was decoded up to that point has been passed to sink anyway.
 * *decoded (may be NULL) gets the number of bytes decoded.
 */
bool gud_lz4_decode(const uint8_t *src, uint32_t src_len, uint32_t out_len, uint8_t *window,
                    gud_lz4_sink_t sink, void *ctx, uint32_t *decoded);
