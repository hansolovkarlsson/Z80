#!/usr/bin/env python3
"""Print a rectangle of a PNG as ASCII art, one character per pixel.

The counterpart to abc80/tests/litpix.py, which counts lit pixels. A count
is the right instrument for a regression check and the wrong one for an
investigation: it says whether something changed, never what the picture
actually is. This prints the picture.

That distinction has cost this project real time. Three coloured lines
were read as "all white" off a small screenshot; a 480-pixel-wide mode
was confirmed only by finding that every horizontal run had length 1,
which no pixel count could show; and a terminal renderer was checked
against the pixel renderer cell by cell. See
docs/postmortems/2026-08-30-binary-oracle-hides-its-premises.md.

Handles the colour types this repo's own writers emit (greyscale, RGB,
palette) and all five PNG filter types, with no third-party libraries -
the same constraint png.c itself is written under.

  scripts/pngart.py FILE [y0 y1 [x0 x1]]

Distinct colours are mapped to distinct characters, background (the most
common colour) to '.', so the shape is legible without knowing the
palette. --hex prints the actual pixel values instead.
"""
import sys, zlib, struct


def load(path):
    d = open(path, 'rb').read()
    if d[:8] != b'\x89PNG\r\n\x1a\n':
        raise SystemExit("%s: not a PNG" % path)
    pos, idat, plte = 8, b'', None
    w = h = bd = ct = None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos + 4])[0]
        typ = d[pos + 4:pos + 8]
        body = d[pos + 8:pos + 8 + ln]
        if typ == b'IHDR':
            w, h, bd, ct = struct.unpack('>IIBB', body[:10])
        elif typ == b'PLTE':
            plte = body
        elif typ == b'IDAT':
            idat += body
        pos += 12 + ln
    if bd != 8:
        raise SystemExit("only 8-bit channels are handled, got %d" % bd)
    bpp = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ct]
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev, p = [], bytearray(stride), 0
    for _ in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p + stride]); p += stride
        if f == 1:
            for x in range(bpp, stride):
                line[x] = (line[x] + line[x - bpp]) & 0xFF
        elif f == 2:
            for x in range(stride):
                line[x] = (line[x] + prev[x]) & 0xFF
        elif f == 3:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                line[x] = (line[x] + ((a + prev[x]) >> 1)) & 0xFF
        elif f == 4:
            for x in range(stride):
                a = line[x - bpp] if x >= bpp else 0
                b = prev[x]
                c = prev[x - bpp] if x >= bpp else 0
                q = a + b - c
                pa, pb, pc = abs(q - a), abs(q - b), abs(q - c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[x] = (line[x] + pr) & 0xFF
        elif f != 0:
            raise SystemExit("unknown PNG filter %d" % f)
        rows.append(bytes(line))
        prev = line
    px = [[tuple(r[x * bpp:(x + 1) * bpp]) for x in range(w)] for r in rows]
    if ct == 3 and plte:
        px = [[tuple(plte[v[0] * 3:v[0] * 3 + 3]) for v in row] for row in px]
    return w, h, px


def main():
    args = [a for a in sys.argv[1:] if a != '--hex']
    as_hex = '--hex' in sys.argv[1:]
    if not args:
        raise SystemExit(__doc__)
    w, h, px = load(args[0])
    y0 = int(args[1]) if len(args) > 1 else 0
    y1 = int(args[2]) if len(args) > 2 else h
    x0 = int(args[3]) if len(args) > 3 else 0
    x1 = int(args[4]) if len(args) > 4 else w
    y1, x1 = min(y1, h), min(x1, w)

    counts = {}
    for row in px[y0:y1]:
        for v in row[x0:x1]:
            counts[v] = counts.get(v, 0) + 1
    order = sorted(counts, key=lambda k: -counts[k])
    glyphs = {order[0]: '.'} if order else {}
    for i, v in enumerate(order[1:]):
        glyphs[v] = "#@%*+=~oxXO0123456789"[i % 21]

    print("%dx%d, showing y=%d..%d x=%d..%d" % (w, h, y0, y1, x0, x1))
    for row in px[y0:y1]:
        if as_hex:
            print(' '.join(''.join('%02X' % c for c in v) for v in row[x0:x1]))
        else:
            print(''.join(glyphs[v] for v in row[x0:x1]))
    if not as_hex and len(order) > 1:
        print("legend: " + "  ".join(
            "%s=%s" % (glyphs[v], ''.join('%02X' % c for c in v)) for v in order))


if __name__ == '__main__':
    main()
