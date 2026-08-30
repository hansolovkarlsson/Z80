#!/usr/bin/env python3
"""Convert a JEDEC fuse map to MAME's own binary (jedbin) form.

Exists so this repository's two committed PALs can be checked against
MAME's published CRC32/SHA1 the way every other ROM here already is. They
could not be compared directly: the archive ships JEDEC ASCII (2,769 and
2,754 bytes) while MAME stores the 260-byte binary its jedparse produces,
so the two describe the same fuses in different containers.

The format, established by sweeping the four plausible conventions against
MAME's published checksums until both files matched:

  * 4 bytes, big-endian, of fuse count (2048 for a 16L8/16R4)
  * the fuses packed 8 per byte, **least significant bit first**
  * no inversion

Usage:  scripts/jed2bin.py FILE.bin [...]        # print checksums
        scripts/jed2bin.py -o OUT.bin FILE.bin   # write the binary
"""

import hashlib
import re
import sys
import zlib


def parse_jedec(path):
    """Return (fuse_count, [fuse bits]) from a JEDEC file."""
    text = open(path, 'rb').read().decode('latin-1')
    match = re.search(r'QF(\d+)\*', text)
    count = int(match.group(1)) if match else None
    fuses = {}
    for entry in re.finditer(r'L0*(\d+)\s+([01\s]+?)\*', text):
        addr = int(entry.group(1))
        bits = re.sub(r'\s', '', entry.group(2))
        for offset, bit in enumerate(bits):
            fuses[addr + offset] = int(bit)
    if count is None:
        count = (max(fuses) + 1) if fuses else 0
    return count, [fuses.get(i, 0) for i in range(count)]


def to_jedbin(count, bits):
    out = bytearray(count.to_bytes(4, 'big'))
    for i in range(0, count, 8):
        byte = 0
        for j in range(8):
            if i + j < count and bits[i + j]:
                byte |= 1 << j
        out.append(byte)
    return bytes(out)


def main(argv):
    out_path = None
    args = list(argv[1:])
    if args and args[0] == '-o':
        if len(args) < 2:
            print(__doc__, file=sys.stderr)
            return 1
        out_path = args[1]
        args = args[2:]
    if not args:
        print(__doc__, file=sys.stderr)
        return 1

    for path in args:
        count, bits = parse_jedec(path)
        blob = to_jedbin(count, bits)
        print('%-16s fuses=%d bytes=%d crc32=%08x sha1=%s'
              % (path.split('/')[-1], count, len(blob),
                 zlib.crc32(blob) & 0xFFFFFFFF, hashlib.sha1(blob).hexdigest()))
        if out_path:
            open(out_path, 'wb').write(blob)
            print('wrote %s' % out_path)
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
