#!/usr/bin/env python3
"""
Host tests for the firmware's pixel path, compiled from the real sources:
  - main/gud/gud_lz4.c (streaming LZ4 decoder) against blocks made by the reference LZ4 library,
    including far back-references, ring wrap-around, long repeating runs and broken input;
  - swap_copy() from main/lcd.c (RGB565 byte swap) for every alignment combination.

Needs: pip install lz4, and a C compiler (cc/gcc/clang on PATH, or pip install ziglang).
  python tools/test/test_lz4.py
"""
import os
import random
import re
import shutil
import struct
import subprocess
import sys
import tempfile

import lz4.block

ROOT = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
GUD = os.path.join(ROOT, "main", "gud")
HERE = os.path.dirname(os.path.abspath(__file__))
W, H = 320, 240


def compiler():
    for cc in ("cc", "gcc", "clang"):
        if shutil.which(cc):
            return [cc]
    try:
        import ziglang  # noqa: F401
        return [sys.executable, "-m", "ziglang", "cc"]
    except ImportError:
        sys.exit("no C compiler: install gcc/clang or 'pip install ziglang'")


def build(tmp, name, sources, includes):
    exe = os.path.join(tmp, name + (".exe" if os.name == "nt" else ""))
    cmd = compiler() + ["-O2", "-std=c11", "-Wall", "-Wextra", "-Werror", "-fsanitize=undefined",
                        "-fno-sanitize-recover=all", "-o", exe] + sources + ["-I" + i for i in includes]
    subprocess.run(cmd, check=True)
    return exe


# ------------------------------- test data --------------------------------

def rgb565(r, g, b):
    return struct.pack("<H", ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))


def frame(fn, w=W, h=H):
    return b"".join(rgb565(*fn(x, y)) for y in range(h) for x in range(w))


def cases(rng):
    yield "solid", rgb565(255, 0, 0) * (W * H)
    yield "black", bytes(W * H * 2)
    yield "noise", rng.randbytes(W * H * 2)
    yield "gradient", frame(lambda x, y: ((x * 2) & 255, y & 255, ((x + y) // 2) & 255))
    yield "ui", frame(lambda x, y: (0x20, 0x20, 0x20) if (y % 40 in range(14, 18) and (x // 6) % 7 < 5)
                      else ((0xC0,) * 3 if y % 40 < 2 else (0xFF,) * 3))
    yield "stripes", frame(lambda x, y: (255, 255, 0) if (x // 3 + y) % 5 == 0 else (0, 0, 64))
    # far back-references: a random block repeated, offsets close to the 64 KB limit
    for k in (30000, 32768, 40000, 65534, 65535):
        blk = rng.randbytes(k)
        data = (blk * (W * H * 2 // k + 2))[: W * H * 2]
        yield f"repeat{k}", data
    # short periods (overlapping matches with offsets 1..48) mixed with noise
    parts = []
    while sum(map(len, parts)) < W * H * 2:
        period = rng.randbytes(rng.randint(1, 48))
        parts.append(period * rng.randint(1, 3000))
        parts.append(rng.randbytes(rng.randint(0, 300)))
    yield "periods", b"".join(parts)[: W * H * 2]
    # small and odd-sized rectangles (still whole pixels)
    for w, h in ((1, 1), (2, 1), (40, 40), (7, 13), (320, 1), (1, 240), (123, 77), (319, 239)):
        yield f"rect{w}x{h}", frame(lambda x, y: ((x * 37) & 255, (y * 11) & 255, ((x ^ y) * 5) & 255), w, h)
    for i in range(12):
        n = rng.randint(1, W * H) * 2
        mix = bytearray()
        while len(mix) < n:
            kind = rng.random()
            if kind < 0.3:
                mix += rng.randbytes(rng.randint(1, 2000))
            elif kind < 0.7 and mix:
                off = rng.randint(1, min(len(mix), 65535))
                for _ in range(rng.randint(4, 5000)):
                    mix.append(mix[-off])
            else:
                mix += bytes([rng.randrange(256)]) * rng.randint(1, 20000)
        yield f"random{i}", bytes(mix[:n])


def compressions(data):
    yield "default", lz4.block.compress(data, mode="default", store_size=False)
    yield "fast", lz4.block.compress(data, mode="fast", acceleration=8, store_size=False)
    yield "hc12", lz4.block.compress(data, mode="high_compression", compression=12, store_size=False)


# --------------------------------- runner ---------------------------------

def run_lz4(exe, tmp, comp, out_len):
    src, dst = os.path.join(tmp, "in.lz4"), os.path.join(tmp, "out.raw")
    with open(src, "wb") as f:
        f.write(comp)
    res = subprocess.run([exe, src, dst, str(out_len)], capture_output=True, text=True, check=True)
    ok, decoded, odd, chunks, maxc = map(int, res.stdout.split())
    with open(dst, "rb") as f:
        out = f.read()
    return bool(ok), decoded, odd, chunks, maxc, out


def main():
    rng = random.Random(1234)
    failures = 0
    with tempfile.TemporaryDirectory() as tmp:
        exe = build(tmp, "lz4_harness", [os.path.join(GUD, "gud_lz4.c"), os.path.join(HERE, "lz4_harness.c")],
                    [GUD])

        n = 0
        for name, data in cases(rng):
            for mode, comp in compressions(data):
                ok, decoded, odd, chunks, maxc, out = run_lz4(exe, tmp, comp, len(data))
                good = ok and decoded == len(data) and out == data and odd == 0
                n += 1
                if not good:
                    failures += 1
                    first = next((i for i, (a, b) in enumerate(zip(out, data)) if a != b), min(len(out), len(data)))
                    print(f"FAIL {name}/{mode}: ok={ok} decoded={decoded}/{len(data)} out={len(out)} "
                          f"odd_chunks={odd} first_diff={first}")
            if name in ("solid", "gradient", "noise"):
                print(f"  {name:9s} {len(data)} -> {len(comp)} bytes (hc12), {chunks} sink calls, max {maxc}")

        # broken input: must fail cleanly, and what it did emit must be a correct prefix
        data = frame(lambda x, y: ((x * 3) & 255, (y * 7) & 255, (x + y) & 255))
        comp = lz4.block.compress(data, store_size=False)
        broken = [("truncated", comp[: len(comp) // 2]), ("short_by_1", comp[:-1]), ("empty", b""),
                  ("garbage", rng.randbytes(5000)), ("zero_offset", bytes([0x1F, 0x41, 0x00, 0x00])),
                  ("offset_beyond", bytes([0x10, 0x41, 0x05, 0x00])),
                  ("too_long", lz4.block.compress(data + data[:64], store_size=False))]
        for name, comp in broken:
            ok, decoded, odd, _, _, out = run_lz4(exe, tmp, comp, len(data))
            n += 1
            prefix_ok = name == "garbage" or out == data[: len(out)]
            if ok or decoded > len(data) or odd or len(out) > decoded or not prefix_ok:
                failures += 1
                print(f"FAIL broken/{name}: ok={ok} decoded={decoded} out={len(out)} prefix_ok={prefix_ok}")

        # swap_copy() from lcd.c, every alignment of source/destination and lengths 0..40
        lcd = open(os.path.join(ROOT, "main", "lcd.c"), encoding="utf-8").read()
        fn = re.search(r"^static void swap_copy\(.*?^}\n", lcd, re.S | re.M).group(0)
        c = os.path.join(tmp, "swap.c")
        with open(c, "w") as f:
            f.write("#include <stdint.h>\n#include <stddef.h>\n#include <stdio.h>\n" + fn + r"""
int main(void) {
    static uint16_t src[64], dst[64];
    int bad = 0;
    for (int so = 0; so < 4; so++) for (int dof = 0; dof < 4; dof++) for (int n = 0; n <= 40; n++) {
        for (int i = 0; i < 64; i++) { src[i] = (uint16_t) (i * 0x0101 + 0x1234); dst[i] = 0xDEAD; }
        swap_copy(dst + dof, src + so, (size_t) n);
        for (int i = 0; i < 64; i++) {
            uint16_t want = (i >= dof && i < dof + n) ? (uint16_t) ((src[i - dof + so] >> 8) | (src[i - dof + so] << 8)) : 0xDEAD;
            if (dst[i] != want) { bad++; break; }
        }
    }
    printf("%d\n", bad);
    return 0;
}
""")
        swap = build(tmp, "swap", [c], [])
        bad = int(subprocess.run([swap], capture_output=True, text=True, check=True).stdout)
        n += 1
        if bad:
            failures += 1
            print(f"FAIL swap_copy: {bad} combinations wrong")

    print(f"{n - failures}/{n} passed")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
