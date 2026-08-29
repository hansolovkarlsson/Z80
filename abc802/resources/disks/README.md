# ABC802 floppy images

Real ABC-bus disk images for `bin/abc802 --disk`. **The images themselves
are not committed**; this README is.

## Why they are not in the repo

Same reasoning that was recorded when the ABC80 target faced the question
([`ABC80_COMPLETED.md`](../../../abc80/docs/ABC80_COMPLETED.md), Milestone
6): unlike the ROM images — which got an explicit provenance and
CRC32/SHA1 table before being committed — and unlike ZEXALL/ZEXDOC, which
are explicitly GPLv2, these disk dumps carry no license statement
anywhere. They are third-party-authored software on third-party-dumped
media. So the directory and this file are tracked; `*.dsk` and `*.img` in
it are not (see the repo's `.gitignore`).

Drop images here and they are ignored by git automatically.

## Attaching one

```
bin/abc802 --columns 80 --interactive --disk abc802/resources/disks/sys10sw.dsk
```

Which controller is fitted comes from the file's size, not a flag —
163,840 bytes selects an ABC830 (`MO0:`), 655,360 an ABC832/834 (`MF0:`).
Repeat `--disk` for more drives, or pin one with `1:FILE`. All attached
drives must be the same type, since one controller is modeled.

**Copy an image before doing anything that writes to it.** The DOS writes
to media on `SAVE`, and `--disk` opens the file read/write.

## Interleave: check this first when a disk misbehaves

**Two dump conventions are in circulation for the same media, and nothing
inside an image says which one it is.**

- abc80.net's `.img` archive stores ABC830 sectors in **physical** order.
  That is what the emulator's `MO` default (factor 7) exists for, and it
  was established by booting real media both ways — twice, on two
  machines and two DOS ROMs.
- Other archives ship `.dsk` files in plain **logical** order. Those need
  `--interleave 0`.

The symptom of the wrong choice is specific and easy to misread: the disk
is found, the directory is intact, filenames list correctly, and then
**every real file read fails with `Error 37`** ("incorrect sector
format"). It does not look like a dead disk.

```
bin/abc802 --disk abc802/resources/disks/sys10sw.dsk                  # Error 37
bin/abc802 --disk abc802/resources/disks/sys10sw.dsk --interleave 0   # works
```

The startup line reports what is in force, so a run is self-documenting:

```
ABC-bus: mo floppy controller, 1 drive attached, interleave 0 (overridden)
```

**A hex dump cannot tell the two apart.** Track-boundary sectors map to
themselves under any factor, and both formats keep a directory copy at
such a sector — so filenames are legible even when every other sector is
being read from the wrong place. Only loading a real file settles it.

The 640K `MF` drive defaults to no interleave, which happens to match the
`.dsk` convention, so 640K images from either archive tend to work
untouched. That is luck, not a rule: the two drives were each measured
independently and need *opposite* settings.

## What is here locally

Ten images, none committed. Provenance is **not recorded** — these were
supplied locally and the originating archive is unknown; fill in the
source here if you know it. Checksums are listed so a re-download can be
identified.

| File | Size | MD5 |
|---|---|---|
| `basope10.dsk` | 160K | `d0578b8fc605c44be84acb909fb0a31d` |
| `demo11.dsk` | 160K | `a099773427449b9be16aac4d5ee39e0b` |
| `graf16.dsk` | 160K | `3a2e22cae2893b2e8335cf64eff231f9` |
| `ord800.dsk` | 160K | `7c6371f0a1c63eae748de8cd6d29732c` |
| `pr800_62.dsk` | 160K | `0912637053991d4fa0da75e2ffac8cc8` |
| `red800.dsk` | 160K | `61dec3372fc5262824e64a3e655ba275` |
| `sana23.dsk` | 640K | `e9baf3a02f5c2fb5edd49f89cf5acc59` |
| `sys10fi.dsk` | 160K | `8d804b875d411aba56da982a42795047` |
| `sys10sw.dsk` | 160K | `28c7ac74899886c790dccf98f73723fe` |
| `tty800.dsk` | 160K | `8c3ff5e8663a86a1d2ab2ebf53312164` |

**All nine 160K images need `--interleave 0`.** `sana23.dsk` (640K) needs
nothing and autoboots as supplied.

Contents, as `bin/abcdisk list` reports them (it reads the image
directly, so no emulator and no correct interleave setting are needed):

| Image | Files | Holds |
|---|---|---|
| `sys10sw.dsk` | 15 | Swedish system disk: `LIB`, `COPY`, `DELETE`, `DOSGEN`, `DISCHECK`, `PROTECT`, `NAMEDISK`, `DIRCOPY`, plus `BASICINI.SYS` and `CMDINT.SYS` |
| `sys10fi.dsk` | 16 | the same, Finnish |
| `demo11.dsk` | 15 | `START`, `DEMO30`-`DEMO70`, `CIRKDIA.PDT`, `HATT.PDT` |
| `graf16.dsk` | 33 | graphics package: `START`, `GHMENY`, `GA`-`GF`, `GRAFIKON.DEF` and a `.HLP` file per command |
| `basope10.dsk` | 27 | `MENU`, `BASOPE1`-`BASOPE13` (a BASIC course), plus the system files |
| `ord800.dsk` | 23 | *ORD 800* word processor: `YRASM.ABS`, `YRBASIC`, and an `INITIERA.*` per printer make |
| `red800.dsk` | 37 | editor package |
| `pr800_62.dsk` | 20 | *PROMMIS* EPROM programmer |
| `tty800.dsk` | 7 | `TTY800`, `TTY`, `LIB`, `CAT800L`, `RELOC800`, `POKE` |
| `sana23.dsk` | 25 | autoboots *SANA 800 Versio 2.3*, a Finnish word processor (640K) |

## Making a blank disk

Neither ROM has a `FORMAT` command, and a zero-filled file of the right
size is not a formatted disk — it attaches, is recognized, and then fails
every `SAVE` with `Error 41`. `bin/abcdisk` writes a real one:

```
bin/abcdisk create work.dsk               # 160K ABC830 (MO)
bin/abcdisk create big.dsk --type mf      # 640K ABC832/834 (MF)
bin/abcdisk list work.dsk                 # what is on it
```

It writes in logical sector order, so attach a 160K one with
`--interleave 0` — `create` prints the exact line. `list` also reads real
media, under either dump convention, because the directory sits on a track
boundary and those sectors map to themselves under any interleave factor.

The whole disk command set has been run live against a disk created this
way: `SAVE`, `LOAD`, `RUN "file"`, `LIST "file"`, `MERGE`, `UNSAVE`,
`KILL` and `NAME … AS`. See
[`ABC802_BASIC_REFERENCE.md`](../../docs/ABC802_BASIC_REFERENCE.md)'s
"Program file commands" for what each was confirmed to do, including two
behaviours worth knowing before they surprise you: `MERGE` refuses a
`.BAC` file, and deleting a file frees its clusters but the next save does
not reuse them.

## Listing a disk's files from BASIC

There is no `DIR` in ROM — the BASIC and DOS ROMs contain no such keyword,
and `LIB` at the prompt is a spelling error. Directory listing is a
*program on the disk*:

```
RUN "MO0:LIB"
```

which is verified working off `sys10sw.dsk` with `--interleave 0`. It is
an interactive menu (`1 - Skrivare`, `2 - Storlek`, `3 - Filstatus`,
`4 - Viss drivenhet`), so drive it under `--interactive`; scripted
`--type` can start it but cannot reliably answer more than one of its
prompts, because the DART holds a single received byte and `--type-at`
offers only one delay point.

See [`ABC802_BASIC_REFERENCE.md`](../../docs/ABC802_BASIC_REFERENCE.md)'s
"Disk drives and storage" for the device names, file-name rules and the
on-disk layout.

## The regression suite

`abc802/tests/run_tests.sh` reads `ABC802_TEST_DISKS` — a directory, not a
file — and skips its floppy checks loudly without it. It wants
`disk001.img`, `mf001.img` and `mf002.img` from abc80.net's ABC800
archive, which are `.img` dumps in **physical** order and therefore work
at the default interleave. Point it here only if you put images with those
names in this directory.
