# ABC806 emulator roadmap

Status and known gaps for `bin/abc806`, the Luxor ABC806 machine target —
what works, what does not, and what is next.

Hardware facts live in [`ABC806_REFERENCE.md`](ABC806_REFERENCE.md), the
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

`make test-abc806` runs 13 checks, and 16 with `ABC806_TEST_DISKS` set;
it is part of `make test`.

## What is next

No milestone is outstanding. In rough order of value:

1. **Evaluate the PAL fuse map.** Started, and blocked on one missing
   fact. [`scripts/palanalyse.py`](../../scripts/palanalyse.py) parses
   `ABC-P4-1.bin` and dumps every product term, and the structure is
   settled: 64 rows of 32 columns, groups of eight per output, **the first
   group driving pin 19**. Two independent checks agree on that ordering —
   pin 15 (KDL) comes out permanently tri-stated with no live terms, which
   is exactly right for a pin MAME's list marks as an input, and the four
   signals MAME reads (ROMD, HRE, MUX, RAMD) are precisely the
   always-enabled outputs with many terms.

   **What is missing is the column-to-signal mapping.** A PAL16L8
   interleaves dedicated inputs with output feedback in an order the device
   datasheet gives, and this repository has no primary source for it. Two
   candidate layouts were tried and both fail the simplest check — that
   with EME off and KEYDTR high the array must select ROM below `0x8000`
   and RAM above it. A third guess would be the same mistake again, so the
   tool stops at the term dump. **A PAL16L8 fuse-map column table from a
   datasheet unblocks this**, and then the array can be checked against
   both MAME's approximation and this emulator's own fetch-window rule,
   which currently rests on behavioural evidence alone.
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
