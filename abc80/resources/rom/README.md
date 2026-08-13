# ABC80 BASIC ROM images

The four 4Kx8 chips holding the ABC80's Luxor BASIC interpreter, loaded at
`0x0000`/`0x1000`/`0x2000`/`0x3000` respectively (see `abc80/docs/ABC80_ROADMAP.md`
for the full memory map).

| File           | Board position | CRC32      |
|----------------|-----------------|------------|
| `3506_3.a5.bin`| ZA3506 / A5     | `e2afbf48` |
| `3507_3.a3.bin`| ZA3507 / A3     | `d224412a` |
| `3508_3.a4.bin`| ZA3508 / A4     | `1502ba5b` |
| `3509_3.a2.bin`| ZA3509 / A2     | `bc8860b7` |

## Provenance

Downloaded from the PC/M Personal Computer Museum's ABC archive:
<https://www.abc80.net/archive/luxor/Prom/fw/ABC80/> (the `-9913` checksum
revision of each chip, per that archive's own `00index.txt`). Each file's
CRC32 was independently verified against the checksums MAME's mainline
`src/mame/luxor/abc80.cpp` driver documents for `3506_3.a5`/`3507_3.a3`/
`3508_3.a4`/`3509_3.a2` — all four are exact, byte-for-byte matches, not
merely "close" or unverified.
