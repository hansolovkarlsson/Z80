# ABC806 emulator roadmap

Status and known gaps for `bin/abc806`, the Luxor ABC806 machine target —
what works, what does not, and what is next.

Hardware facts live in [`ABC806_REFERENCE.md`](ABC806_REFERENCE.md), the
BASIC dialect and its graphics commands in
[`ABC806_BASIC_REFERENCE.md`](ABC806_BASIC_REFERENCE.md), the
finished milestone write-ups in
[`ABC806_COMPLETED.md`](ABC806_COMPLETED.md), and the feasibility review
written before any of this existed in
[`ABC806_SCOPING.md`](ABC806_SCOPING.md) — which now carries an outcome
section comparing what it predicted against what happened.

## Why a fourth machine target

The ABC806 is the top of the ABC800 family and shares this repository's
proven Z80 core (`z80core/z80.o` + `alu.o` via `z80_execute()`) with
`bin/z80`, `bin/abc80` and `bin/abc802`, on the terms those already
established. It mounts the same `abcbus/` floppy card as the other two ABC
targets.

The scoping document weighed *not* building it — MAME emulates this machine
more completely than a first cut here would — and the case for building
anyway was that this repository is a place where the reasoning gets written
down, and where a PROM gets decoded for real rather than approximated. That
bet paid: the high-resolution memory window this target implements exists
in MAME only as a commented-out TODO.

## Status: all five scoping gates met

Full write-ups, including what each milestone found the hard way, are in
[`ABC806_COMPLETED.md`](ABC806_COMPLETED.md).

| # | Milestone | Outcome |
|---|---|---|
| 1 | The MMU, and a boot | Executes past reset and programs the CRTC for its own 80x25 |
| 2 | Text video | `--screenshot` writes a real PNG in the machine's eight colours; the attribute decode is fixture-verified |
| 3 | Keyboard and a live session | `--type $'PRINT 6*7\r'` answers `42`; `--interactive` is a real 3 MHz session with a colour screen at 30fps |
| 4 | Disk | `--disk` boots real ABC832 UFD-DOS; `BYE` reaches the DOS shell and `LIB` lists the media |
| — | Real-time clock | The DOS prints the host's date and time to the second |
| 5 | High-resolution graphics | `FGCTL 2` then `FGLINE` draws red, green and yellow lines over the text |

`bin/abc806-gtk` (opt-in, `make abc806-gtk`) is a real GTK4 window on the
same terms as the other two ABC targets' — see
[`../gtk/README.md`](../gtk/README.md). It has three headless checks of
its own, including the colour one described under Testing below.

`make test-abc806` runs 41 checks — 35 needing nothing, 3 that need a real
disk image, and 3 that drive `bin/abc806-gtk` headlessly and skip loudly
when that opt-in binary is absent. One of those three is the only check
anywhere that would notice the GTK window losing *colour*: three pen lines
must render as three distinct colours in equal numbers, which a pixel
count cannot see. Those look in
`abc802/resources/disks/` for `sys832-ufd.img` (the same UFD-DOS system
disk serves both targets, so there is no second copy of a 640K image);
`ABC806_TEST_DISKS` overrides the location. It is part of `make test`.

## What is next

No milestone is outstanding, and the PAL investigation reached its own
conclusion (see [`ABC806_COMPLETED.md`](ABC806_COMPLETED.md)). What is left
is small:

1. **`I3` (pin 1) on the memory-mapper PAL**, untraced beyond sheet 5 of
   the schematics, and the polarity of its `ROMDIS`/`RAMDIS` outputs.
   Nothing depends on either today.
2. **The 544K RAM option**, and the protection device on the 74ALS259.

## Known gaps

Every milestone is complete; what follows is what the machine still does
not do, and why each one is deliberate rather than an oversight.

- **Right arrow is dropped, on inference rather than evidence.** The
  ABC802's line editor was swept byte by byte and turned out to have no
  cursor movement at all; this ROM is from the same family and the same
  year and is *assumed* to match. The sweep has not been done here.
- **The flash rate is assumed.** 2 Hz, the conventional rate. No source
  consulted gives the ABC806's own divider, and the ROM does not blink its
  cursor in software the way the ABC802's does, so nothing in the machine
  supplies the phase.
- **The 480-pixel-wide mode is unreachable through `FGCTL`.** That is the
  ROM, not a gap: none of its 128 palettes programs an entry whose halves
  differ. The mode itself works and is tested — BASIC reaches it by writing
  `hrc` directly, since the entry index is the port's high byte — but any
  program wanting 480 has to do that for itself.
- **The memory map still follows MAME's behavioural decode**, and so
  inherits its `abc806 30K banking` gap. The PAL itself is no longer the
  missing piece: `scripts/palanalyse.py` decodes `ABC-P4-1.bin` into
  equations, and that investigation is closed rather than deferred — it
  confirmed the fetch-window latch in the silicon, but its `ROMD`/`RAMD`
  outputs are inter-board *disable* lines rather than local chip selects,
  so the array was never going to yield the map on its own. See
  [`ABC806_COMPLETED.md`](ABC806_COMPLETED.md).
- **`emu/src/ports.c` is a near-copy of the ABC802's.** The CTC, SIO, DART
  and CRTC are the same chips, so it was seeded from that file rather than
  rewritten. That is a deliberate duplication, not an oversight: extracting
  a shared Z80-peripheral module is the right move *once it is known what
  is genuinely common*, which needs this target further along than
  milestone 1. `abcbus/` reached that point the same way — built inside one
  target, moved out when a second consumer proved the shape.
- **RAM is 32K directly addressable**, as on real hardware, with the rest
  reachable only through EME and the map. The 544K option is not modeled.
- **No protection device.** It hangs off the same 74ALS259 as the clock;
  its latch bit (3, PROT INI) is decoded and dropped.
- **The clock is read-only in practice.** Writes are implemented — the DOS
  can set the time — but nothing persists them, so a new run reads the
  host clock again rather than whatever was last written.
