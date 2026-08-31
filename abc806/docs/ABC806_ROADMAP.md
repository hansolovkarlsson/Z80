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
[`../gtk/README.md`](../gtk/README.md).

`make test-abc806` runs 21 checks — 18 media-free, plus 3 that need a real
disk image and otherwise skip loudly (set `ABC806_TEST_DISKS`). It is part
of `make test`.

## What is next

No milestone is outstanding. In rough order of value:

1. **Finish evaluating the PAL fuse map.** Substantially advanced.
   [`scripts/palanalyse.py`](../../scripts/palanalyse.py) now decodes
   `ABC-P4-1.bin` into readable equations: the column layout came from the
   PAL16L8 logic diagram in TI's SRPS016 datasheet, and is self-validating,
   since the terms come out as recognisable memory decode where two earlier
   guesses produced none.

   **It confirmed the fetch-window rule independently.** `HRAL`/`HRBL` are
   a cross-coupled SR latch set by
   `A15'.I3'.A14.B13.B12.B11.M1L'.(ENL+EME').XML` — the `0x7800`-`0x7FFF`
   window during an opcode fetch — and `HRAL` gates `ROMD` and `HRE`. That
   is the rule `memory.c` implements, arrived at from behaviour, and the
   array explains why it is a *latch*: the diversion has to persist through
   the instruction's data cycles.

   **The real schematics settled most of the rest.**
   `ABC806-schema.pdf` on abc80.net (sheet 5, PAL at position 2D) confirms
   the pinout from the board, shows `RKDL` carrying a 22k pull-up to Vcc —
   so it is high whenever the PAL tri-states it — and identifies `XML`,
   `M1L` and `KDL` as the active-low board signals `/XM`, `/MI` and `/HR`
   from the processor unit, with `ENL` coming from the 74F189 that *is* the
   page map. It also corroborates the ROM overlay: one of `RAMD`'s terms is
   the bare literal `A15'`, so RAM is selected across the whole low 32K and
   `ROMD` only decides whether ROM overrides on a read.

   **And it explains why the array alone cannot give the memory map.**
   `ROMD` goes to P2-4 "ROMDIS" and `RAMD` to P1-7 "RAMDIS", each with a
   330 ohm pull-up: they are inter-board *disable* lines to the processor
   unit, not local chip selects. So no term needs to enable ROM between
   `0x4000` and `0x77FF` — the processor board decodes that itself and this
   PAL only intervenes. `memory.c` should therefore not be rewritten "from
   the PAL"; the array's value is settling specific questions, which it has
   now done twice.

   Genuinely open: **`I3` (pin 1)**, untraced beyond sheet 5, and the
   polarity of ROMDIS/RAMDIS.

2. **The remaining graphics modes.** Only the four-colour mode is
   exercised; `FGCTL`'s other arguments program the palette differently.

## Known gaps

Everything below is expected at this point: two milestones in, of five.

- **Right arrow is dropped, on inference rather than evidence.** The
  ABC802's line editor was swept byte by byte and turned out to have no
  cursor movement at all; this ROM is from the same family and the same
  year and is *assumed* to match. The sweep has not been done here.
- **The flash rate is assumed.** 2 Hz, the conventional rate. No source
  consulted gives the ABC806's own divider, and the ROM does not blink its
  cursor in software the way the ABC802's does, so nothing in the machine
  supplies the phase.
- **Only one graphics mode is exercised.** The four-colour mode works and
  is tested; `FGCTL`'s other arguments program the palette differently and
  are not covered.
- **The HRS bank select is untested.** Every check runs with `hrs = 0`, so
  a wrong shift in the plane's physical-address calculation would pass
  silently. Needs a case that actually banks.
- **The PAL fuse map is not evaluated.** `ABC-P4-1.bin` is a well-formed
  JEDEC dump and the memory decode currently follows MAME's behavioural
  approximation instead — inheriting its `abc806 30K banking` gap. See
  `ABC806_SCOPING.md`.
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
