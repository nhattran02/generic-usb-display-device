/*
 * Host harness for main/gud/gud_lz4.c, driven by test_lz4.py.
 *   lz4_harness <in.lz4> <out.raw> <out_len>
 * prints: ok decoded odd_chunks chunks max_chunk
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "gud_lz4.h"

static FILE *s_out;
static unsigned long s_chunks, s_odd, s_max;

static void sink(void *ctx, const void *data, size_t len)
{
    (void) ctx;
    if (!len || (len & 1))
        s_odd++;
    if (len > s_max)
        s_max = len;
    s_chunks++;
    fwrite(data, 1, len, s_out);
}

int main(int argc, char **argv)
{
    if (argc != 4)
        return 2;
    FILE *f = fopen(argv[1], "rb");
    if (!f)
        return 2;
    fseek(f, 0, SEEK_END);
    long n = ftell(f);
    fseek(f, 0, SEEK_SET);
    uint8_t *in = malloc(n ? (size_t) n : 1);
    if (fread(in, 1, (size_t) n, f) != (size_t) n)
        return 2;
    fclose(f);

    s_out = fopen(argv[2], "wb");
    uint8_t *win = malloc(GUD_LZ4_WINDOW_SIZE);
    memset(win, 0xA5, GUD_LZ4_WINDOW_SIZE);   /* stale window contents must never leak into the output */

    uint32_t decoded = 0;
    bool ok = gud_lz4_decode(in, (uint32_t) n, (uint32_t) strtoul(argv[3], NULL, 10), win, sink, NULL, &decoded);
    fclose(s_out);
    printf("%d %u %lu %lu %lu\n", ok, decoded, s_odd, s_chunks, s_max);
    return 0;
}
