# ABC806 ROM images

The real Luxor ABC806 firmware `bin/abc806` boots. Same provenance and
verification standard as `abc80/resources/rom/` and
`abc802/resources/rom/`: every file was checked against an independent
source before being committed, not merely downloaded and assumed good.

## System ROM (32K, addresses 0x0000-0x7FFF)

Eight 4K EPROMs, in the same arrangement the ABC802 uses — the ABC806
just builds its 32K out of eight chips instead of four.

| File | Board | Address | CRC32 |
|---|---|---|---|
| `ABC806-basic.06-11.bin` | 1M | `0x0000` | `27083191` |
| `ABC806-basic.16-11.bin` | 1L | `0x1000` | `eb0a08fd` |
| `ABC806-basic.26-11.bin` | 1K | `0x2000` | `97a95c59` |
| `ABC806-basic.36-11.bin` | 1J | `0x3000` | `b50e418e` |
| `ABC806-basic.46-11.bin` | 2M | `0x4000` | `17a87c7d` |
| `ABC806-basic.56-11.bin` | 2L | `0x5000` | `b4b02358` |
| `ABC806-dos.66-31.bin` | 2K | `0x6000` | `a2e38260` |
| `ABC806-option.76-11.6490238-02.bin` | 2J | `0x7000` | `3eb5f6a1` |

The DOS PROM is the one that varies. Both committed images are the same
UFD-DOS pair the ABC802 carries, to the day:

| File | Contents | CRC32 |
|---|---|---|
| `ABC806-dos.66-31.bin` | UFD-DOS v.20 (1984-04-03) | `a2e38260` |
| `ABC806-dos.66-21.bin` | UFD-DOS v.19 (1984-03-02) | `c311b57a` |

`66-31` is the default (`--dos-rom` selects the other), matching MAME's
own default for this machine. **Both boot identically** as of milestone 1.

## Video PROMs

The ABC806's video hardware is steered by four small bipolar PROMs. None
is used yet — milestones 2 and 5 — but they are committed now so the set
is complete and verified in one pass.

| File | Chip | Size | Role |
|---|---|---|---|
| `ABC806-char.6490243-01.bin` | T06-1 @ 7C | 4K | character generator, Swedish/Finnish |
| `RAD.bin` | 60 90241-01 @ 9B | 512 | character line address |
| `HRU-I.bin` | 60 90128-01 @ 6E | 32 | HR horizontal timing and video memory access |
| `HRU-II.bin` | 60 90127-01 @ 12G | 512 | ABC800C HR compatibility palette |
| `V50.bin` | 60 90242-01 @ 7E | 512 | HR vertical timing, 50 Hz |

## The PALs

| File | Device | Part | Size |
|---|---|---|---|
| `ABC-P3-1.bin` | PAL16R4 | 60 90239-01 | 2754 |
| `ABC-P4-1.bin` | PAL16L8 | 60 90240-01 | 2769 |

**These are JEDEC fuse maps, not binaries** — ASCII, with an `L00000
1111...*` line per product term. Both are well-formed: 64 product lines of
32 fuses, 2048 in total, exactly a 16L8's or 16R4's array.

That matters because P4-1 *is* the ABC806's memory decode. MAME
reimplements it behaviourally and has its own PAL lookup commented out,
with `abc806 30K banking` as an open TODO beside it. Evaluating the real
fuse map is therefore a way to do something MAME does not — see
`abc806/docs/ABC806_SCOPING.md`. `emu/src/memory.c` currently follows
MAME's behavioural form; the fuse map is committed so that work has its
input ready.

## Provenance and verification

All files downloaded from the PC/M Personal Computer Museum's ABC archive:
<https://www.abc80.net/archive/luxor/Prom/fw/ABC806/> — the same archive
the other two targets' ROMs come from, with its own `00index.txt`
identifying each image.

Every one that MAME carries was then verified against the checksums
mainline `src/mame/luxor/abc80x.cpp` documents. **All sixteen match
exactly on both CRC32 and SHA1** — byte-for-byte identical, not merely
the right size:

| File | MAME ROM name | CRC32 + SHA1 |
|---|---|---|
| `ABC806-basic.06-11.bin` | `abc 06-11.1m` | match |
| `ABC806-basic.16-11.bin` | `abc 16-11.1l` | match |
| `ABC806-basic.26-11.bin` | `abc 26-11.1k` | match |
| `ABC806-basic.36-11.bin` | `abc 36-11.1j` | match |
| `ABC806-basic.46-11.bin` | `abc 46-11.2m` | match |
| `ABC806-basic.56-11.bin` | `abc 56-11.2l` | match |
| `ABC806-dos.66-21.bin` | `abc 66-21.2k` | match |
| `ABC806-dos.66-31.bin` | `abc 66-31.2k` | match |
| `ABC806-option.76-11.6490238-02.bin` | `abc 76-11.2j` | match |
| `ABC806-char.6490243-01.bin` | `abc t6-11.7c` | match |
| `RAD.bin` | `60 90241-01.9b` | match |
| `HRU-I.bin` | `60 90128-01.6e` | match |
| `HRU-II.bin` | `60 90127-01.12g` | match |
| `V50.bin` | `60 90242-01.7e` | match |
| `ABC-P3-1.bin` | `60 90239-01.1b` | match |
| `ABC-P4-1.bin` | `60 90240-01.2d` | match |

### The PALs needed converting before they could be compared

This file previously said **"the two PALs have no MAME entry to check
against … they rest on the archive alone."** That was wrong, and the reason
is worth recording because it is an easy mistake to repeat: MAME *does*
carry both, in `ROM_REGION("abc_p3")` and `ROM_REGION("abc_p4")`, each with
a full pin list in a comment above it. They were missed on a first pass
because MAME never *reads* them — the PAL lookup in `read_pal_p4()` is
commented out — so they do not appear anywhere a search for behaviour would
land.

They also could not be compared directly. The archive ships **JEDEC ASCII**
(2,769 and 2,754 bytes) while MAME stores the **260-byte binary** its
`jedparse` produces, so the two describe identical fuses in different
containers and a naive checksum comparison fails.

[`scripts/jed2bin.py`](../../../scripts/jed2bin.py) does the conversion and
prints the checksums, so this is reproducible rather than a one-off claim:

```
$ scripts/jed2bin.py abc806/resources/rom/ABC-P4-1.bin
ABC-P4-1.bin     fuses=2048 bytes=260 crc32=3cc5518d sha1=343cf951d01c9d361b695bb4e80eaadf0820b6bc
```

The format is 4 bytes of fuse count big-endian, then the fuses packed 8 per
byte **least significant bit first**, uninverted — established by sweeping
the four plausible conventions until both files matched their published
checksums at once. Two independent files agreeing on one convention is what
makes that a determination rather than a fit.

### The pin lists

MAME's comments give both PALs' pinouts, which are not otherwise recorded
here and are what any future attempt to evaluate the fuse map will need.

`ABC-P4-1` (PAL16L8, the memory mapper) — outputs marked `>`:

| Pin | Signal | | Pin | Signal |
|---|---|---|---|---|
| 1 | I3 | | 11 | XML |
| 2 | A15 | | 12 | >ROMD |
| 3 | A14 | | 13 | HRAL |
| 4 | B13 | | 14 | HRBL |
| 5 | B12 | | 15 | KDL |
| 6 | B11 | | 16 | >HRE |
| 7 | M1L | | 17 | RKDL |
| 8 | EME | | 18 | >MUX |
| 9 | ENL | | 19 | >RAMD |
| 10 | GND | | 20 | Vcc |

`ABC-P3-1` (PAL16R4, the colour encoder): inputs `RTB`/`GTB`/`BTB` (pin
6-8), `SFG` (9), `RFG`/`GFG` (12-13), `BFG` (18); outputs `>YL` (14),
`>BL` (15), `>GL` (16), `>RL` (17), `>FGE` (19).

Two further parts are not dumped **anywhere**, MAME included: the
attribute-handler PAL (MAME carries only a pin list in a comment) and
`V60`, the 60 Hz vertical timing PROM, which it marks `NO_DUMP`. Neither
is needed at 50 Hz.

## Licensing

These are Luxor Datorer AB firmware images, copyrighted by their owner. No
redistribution license is claimed or granted; they are included as the real
software this emulator is validated against, on exactly the same footing
as the ABC80 and ABC802 ROM images already in this repository. See the
repository's top-level `README.md` License section.
