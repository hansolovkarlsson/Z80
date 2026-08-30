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
1111...*` line per product term. `ABC-P4-1.bin` was checked and is
well-formed: 64 product lines of 32 fuses, 2048 in total, exactly a
PAL16L8's array.

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
mainline `src/mame/luxor/abc80x.cpp` documents. **All fourteen match
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

**The two PALs have no MAME entry to check against** — they are not in its
`ROM_START`, because MAME does not read them. Their checksums are recorded
here so a re-download can be compared, but unlike the other fourteen they
rest on the archive alone. Said plainly rather than left to look like the
same standard of evidence.

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
