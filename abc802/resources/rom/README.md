# ABC802 ROM images

The real Luxor ABC802 firmware `bin/abc802` boots. Same provenance and
verification standard as `abc80/resources/rom/` — every file was checked
against an independent source before being committed, not merely
downloaded and assumed good.

## System ROM (32K, addresses 0x0000-0x7FFF)

Four 8K EPROMs, loaded in board order:

| File                     | Board position | Address  | CRC32      |
|--------------------------|----------------|----------|------------|
| `ABC802-basic.02-11.bin` | 9F             | `0x0000` | `b86537b2` |
| `ABC802-basic.12-11.bin` | 11F            | `0x2000` | `3561c671` |
| `ABC802-basic.22-11.bin` | 12F            | `0x4000` | `8dcb1cc7` |
| `ABC802-dos.32-31.bin`   | 14F            | `0x6000` | `fc8be7a8` |

The first three are BASIC II (v.01). The fourth is the DOS/option ROM,
which is the one that actually varies between machines:

| File                   | Contents                        | CRC32      |
|------------------------|---------------------------------|------------|
| `ABC802-dos.32-31.bin` | UFD-DOS v.20 (1984-04-03)       | `fc8be7a8` |
| `ABC802-dos.32-21.bin` | UFD-DOS v.19 (1984-03-02)       | `57050b98` |

`ABC802-dos.32-31.bin` is the default (`--dos-rom` selects another);
UFD-DOS v.20 is also what MAME defaults this machine to. Both are
committed so the alternative can be tried without a re-download.

## Character generator ROM

| File                          | Chip position | CRC32 (first 4K) |
|-------------------------------|---------------|------------------|
| `ABC802-char.6490191-01.bin`  | 3G            | `4d54eed8`       |

**The file is 8K, but only the low 4K is wired to the video hardware.**
The MC6845 addresses it as `(character_code << 4) | raster_row`, which
spans exactly 4096 bytes — 256 entries of 16 rows, of which the upper
half (selected by the Row Graphic attribute) holds the graphics
characters. The second 4K of the physical chip is not addressable in this
machine and is not loaded; it is kept in the committed file rather than
truncated, since it is what the real chip contains.

## Provenance and verification

All files downloaded from the PC/M Personal Computer Museum's ABC
archive: <https://www.abc80.net/archive/luxor/Prom/fw/ABC802/> — the same
archive `abc80/resources/rom/` uses, with its own `00index.txt`
identifying each image.

Every one was then verified against the checksums MAME's mainline
`src/mame/luxor/abc80x.cpp` documents for the corresponding chip. **All
five match exactly on both CRC32 and SHA1** — byte-for-byte identical,
not merely the right size:

| File                          | MAME ROM name    | SHA1 match |
|-------------------------------|------------------|------------|
| `ABC802-basic.02-11.bin`      | `abc 02-11.9f`   | yes        |
| `ABC802-basic.12-11.bin`      | `abc 12-11.11f`  | yes        |
| `ABC802-basic.22-11.bin`      | `abc 22-11.12f`  | yes        |
| `ABC802-dos.32-31.bin`        | `abc 32-31.14f`  | yes        |
| `ABC802-dos.32-21.bin`        | `abc 32-21.14f`  | yes        |
| `ABC802-char.6490191-01.bin`  | `abc t02-1.3g`   | yes (low 4K) |

Unlike the ABC80's character ROM — a hand-reconstructed `BAD_DUMP` of a
mask-programmed chip, see `abc80/resources/rom/README.md` — none of these
carry a bad-dump caveat: all are real electronic dumps of real EPROMs.

## Licensing

These are Luxor Datorer AB firmware images, copyrighted by their owner.
No redistribution license is claimed or granted; they are included as the
real software this emulator is validated against, on exactly the same
footing as the ABC80 ROM images already in this repository. See the
repository's top-level `README.md` License section.
