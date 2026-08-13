# ABC80 ROM images

## BASIC ROM (Milestone 1)

The four 4Kx8 chips holding the ABC80's Luxor BASIC interpreter, loaded at
`0x0000`/`0x1000`/`0x2000`/`0x3000` respectively (see `abc80/docs/ABC80_ROADMAP.md`
for the full memory map).

| File           | Board position | CRC32      |
|----------------|-----------------|------------|
| `3506_3.a5.bin`| ZA3506 / A5     | `e2afbf48` |
| `3507_3.a3.bin`| ZA3507 / A3     | `d224412a` |
| `3508_3.a4.bin`| ZA3508 / A4     | `1502ba5b` |
| `3509_3.a2.bin`| ZA3509 / A2     | `bc8860b7` |

Downloaded from the PC/M Personal Computer Museum's ABC archive:
<https://www.abc80.net/archive/luxor/Prom/fw/ABC80/> (the `-9913` checksum
revision of each chip, per that archive's own `00index.txt`). Each file's
CRC32 was independently verified against the checksums MAME's mainline
`src/mame/luxor/abc80.cpp` driver documents for `3506_3.a5`/`3507_3.a3`/
`3508_3.a4`/`3509_3.a2` — all four are exact, byte-for-byte matches, not
merely "close" or unverified.

## Character generator ROM (Milestone 2)

| File           | Chip position       | CRC32      |
|----------------|----------------------|------------|
| `chargen.bin`  | SN74S263N / "H2"     | `9e064e91` |

Downloaded from the same abc80.net archive
(`https://www.abc80.net/archive/luxor/Prom/fw/ABC80/char-rom.bin`, described
there as "Character ROM position 'H2' SN74S263 64 40047-01"). Verified
byte-identical (CRC32 `9e064e91`, 2560 bytes) to the dump embedded in MAME's
`src/devices/video/sn74s262.cpp` (`ROM_LOAD("sn74s263", ...)`).

**Caveat, not glossed over**: MAME's own source marks this dump `BAD_DUMP`
with the comment `// created by hand` — the SN74S263 is a mask-programmed
chip (the font is baked into fixed logic, not stored in an electronically
readable memory), so this is a best-effort hand reconstruction of the real
chip's output, not an electronic dump, despite being the de facto standard
copy every ABC80 emulation project (including this one) relies on. Decoding
it (`abc80/emu/src/chargen.c`) reproduced clean, correct letterforms for
`A`/`B`/`0`/`S`/`!`, and — independently confirming both the address formula
*and* that this is genuinely the right ROM — character `0x5B` (`[` in plain
ASCII) decodes to a clear **Ä** (umlaut dots over a rounded A), matching the
real chip's documented identity as the Swedish/Finnish national-charset
variant (`DEFINE_DEVICE_TYPE(SN74S263, ..., "sn74s263", ...) // Swedish/Finnish`
in MAME's own source) rather than plain ASCII.
