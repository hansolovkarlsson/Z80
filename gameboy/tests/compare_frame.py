#!/usr/bin/env python3
"""Compare a dumped Game Boy PPU frame (PPM, see main.c's --ppm option)
against a reference image (PNG, 2-bit grayscale) pixel-for-pixel.

Written for gameboy/test_roms/dmg-acid2/reference-dmg.png specifically
(see gameboy/docs/GAMEBOY_ROADMAP.md's Phase 3 status), but the PNG decoder
below is generic to any 2-bit grayscale, non-interlaced PNG. No image
library dependency - a minimal decoder using only the standard library
(struct + zlib, both stdlib) is enough for this one format, and this
project already avoids adding dependencies where a small amount of
grounded code will do.

100% match isn't expected: as of Phase 4, this emulator gets 98.04%
(22589/23040) - up from Phase 3's 91.31% once interrupt dispatch made
dmg-acid2's mid-frame LY=LYC-driven register writes actually happen
(see its own README, and gameboy/test_roms/dmg-acid2/README.md). The
remaining gap (row 0's "HELLO WORLD!" text and the tail end of the
footer text) is a real, open, documented issue - see
gameboy/docs/GAMEBOY_ROADMAP.md's Phase 4 status - plausibly related to this
emulator's scanline-at-once renderer not modeling exact sub-scanline
interrupt-response timing, but not fully root-caused yet. This script's
job is to catch a real *regression* (a match rate meaningfully below
the established baseline), not to gate on a full pass that isn't
possible yet.
"""
import sys
import struct
import zlib


def read_png_grayscale(path):
    with open(path, "rb") as f:
        data = f.read()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError(f"'{path}' is not a PNG file")

    pos = 8
    width = height = bit_depth = None
    idat = b""
    while pos < len(data):
        length = struct.unpack(">I", data[pos:pos + 4])[0]
        ctype = data[pos + 4:pos + 8]
        chunk = data[pos + 8:pos + 8 + length]
        if ctype == b"IHDR":
            width, height, bit_depth, color_type = struct.unpack(">IIBB", chunk[:10])
            if color_type != 0:
                raise ValueError(f"'{path}': expected grayscale (color type 0), got {color_type}")
        elif ctype == b"IDAT":
            idat += chunk
        pos += 8 + length + 4

    raw = zlib.decompress(idat)
    row_bytes = (width * bit_depth + 7) // 8
    stride = row_bytes + 1
    pixels = [[0] * width for _ in range(height)]
    prev = bytearray(row_bytes)
    idx = 0

    def paeth(a, b, c):
        p = a + b - c
        pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
        return a if pa <= pb and pa <= pc else (b if pb <= pc else c)

    for y in range(height):
        ftype = raw[idx]
        row = bytearray(raw[idx + 1:idx + 1 + row_bytes])
        idx += stride
        if ftype == 1:  # Sub
            for i in range(len(row)):
                left = row[i - 1] if i >= 1 else 0
                row[i] = (row[i] + left) & 0xFF
        elif ftype == 2:  # Up
            for i in range(len(row)):
                row[i] = (row[i] + prev[i]) & 0xFF
        elif ftype == 3:  # Average
            for i in range(len(row)):
                left = row[i - 1] if i >= 1 else 0
                row[i] = (row[i] + (left + prev[i]) // 2) & 0xFF
        elif ftype == 4:  # Paeth
            for i in range(len(row)):
                left = row[i - 1] if i >= 1 else 0
                upleft = prev[i - 1] if i >= 1 else 0
                row[i] = (row[i] + paeth(left, prev[i], upleft)) & 0xFF
        prev = row

        bitpos = 0
        for x in range(width):
            byte = row[bitpos // 8]
            shift = 8 - bit_depth - (bitpos % 8)
            mask = (1 << bit_depth) - 1
            pixels[y][x] = (byte >> shift) & mask
            bitpos += bit_depth

    return width, height, pixels, bit_depth


def read_ppm(path):
    with open(path, "rb") as f:
        magic = f.readline().strip()
        if magic != b"P5":
            raise ValueError(f"'{path}': expected a raw grayscale PPM (P5)")
        dims = f.readline().split()
        w, h = int(dims[0]), int(dims[1])
        f.readline()  # maxval
        data = f.read(w * h)
    pixels = [[data[y * w + x] for x in range(w)] for y in range(h)]
    return w, h, pixels


def main():
    if len(sys.argv) < 3:
        print(f"usage: {sys.argv[0]} <output.ppm> <reference.png> [min_match_pct]")
        return 2

    ppm_path, png_path = sys.argv[1], sys.argv[2]
    min_pct = float(sys.argv[3]) if len(sys.argv) > 3 else 95.0

    ref_w, ref_h, ref, bit_depth = read_png_grayscale(png_path)
    out_w, out_h, out = read_ppm(ppm_path)

    if (ref_w, ref_h) != (out_w, out_h):
        print(f"FAIL: dimension mismatch - reference {ref_w}x{ref_h}, output {out_w}x{out_h}")
        return 1

    # PNG grayscale sample 0=black, max=white (standard PNG semantics) -
    # scale up to the same 0-255 range main.c's shade_to_gray() uses.
    scale = 255 // ((1 << bit_depth) - 1)
    matches = sum(
        1 for y in range(ref_h) for x in range(ref_w)
        if ref[y][x] * scale == out[y][x]
    )
    total = ref_w * ref_h
    pct = 100.0 * matches / total
    print(f"{matches}/{total} pixels match ({pct:.2f}%)")

    if pct < min_pct:
        print(f"FAIL: match rate below the {min_pct}% baseline - looks like a real PPU regression")
        return 1

    print("OK (informational gate against a 95% floor, not a 100% target - see "
          "gameboy/docs/GAMEBOY_ROADMAP.md's Phase 4 status for the still-open remaining gap)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
