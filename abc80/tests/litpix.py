#!/usr/bin/env python3
"""Count non-background pixels in a PNG. Used by the abc80 GTK checks."""
import sys, zlib, struct

def lit(path):
    d = open(path, 'rb').read()
    pos, idat = 8, b''
    w = h = ct = None
    while pos < len(d):
        ln = struct.unpack('>I', d[pos:pos+4])[0]
        typ = d[pos+4:pos+8]
        if typ == b'IHDR':
            w, h, _bd, ct = struct.unpack('>IIBB', d[pos+8:pos+18])
        elif typ == b'IDAT':
            idat += d[pos+8:pos+8+ln]
        pos += 12 + ln
    raw = zlib.decompress(idat)
    bpp = {2: 3, 6: 4}[ct]
    stride = w * bpp
    prev = bytearray(stride)
    counts = {}
    p = 0
    for _y in range(h):
        f = raw[p]; p += 1
        line = bytearray(raw[p:p+stride]); p += stride
        # Undo the PNG row filter so pixel values are real.
        for i in range(stride):
            a = line[i-bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i-bpp] if i >= bpp else 0
            if f == 1: line[i] = (line[i] + a) & 0xFF
            elif f == 2: line[i] = (line[i] + b) & 0xFF
            elif f == 3: line[i] = (line[i] + (a + b) // 2) & 0xFF
            elif f == 4:
                pa = abs(b - c); pb = abs(a - c); pc = abs(a + b - 2*c)
                pr = a if (pa <= pb and pa <= pc) else (b if pb <= pc else c)
                line[i] = (line[i] + pr) & 0xFF
        for x in range(w):
            px = bytes(line[x*bpp:x*bpp+3])
            counts[px] = counts.get(px, 0) + 1
        prev = line
    bg = max(counts, key=counts.get)      # the most common colour is the background
    return sum(n for px, n in counts.items() if px != bg)

if __name__ == '__main__':
    print(lit(sys.argv[1]))
