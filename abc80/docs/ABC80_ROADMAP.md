# ABC80 Roadmap

A second machine target sharing this repo's proven, ZEXALL/ZEXDOC-clean Z80
core (`cpm/emu/src/z80.c`/`alu.c`) rather than a from-scratch CPU — see
`CLAUDE.md`'s top-level structure section for why this lives here instead of
as a separate repo (unlike the old `gameboy/` subproject, which deliberately
shared no code with this one).

The Luxor ABC80 (Dataindustrier AB / DIAB, manufactured by Luxor AB, Motala,
Sweden, 1978): Z80A @ ~2.9952 MHz, 16 KB BASIC ROM, 16–32 KB RAM, 40×24
monochrome text display, cassette storage standard, ABCbus expansion for
disk/RAM/other peripherals.

## Milestone 1: boots the real ROM on the shared core — done

**Goal**: prove the shared Z80 core correctly executes real, unmodified
ABC80 firmware, with no CP/M-specific behavior leaking in.

- `abc80/emu/src/main.c` loads the four real 4Kx8 BASIC ROM chips
  (`abc80/resources/rom/`, provenance and checksums in that directory's own
  `README.md`) and runs them via `z80_execute()` — the new CP/M-agnostic
  entry point split out of what used to be a single combined `z80_step()`
  (see below).
- **Verified by direct execution**, not just successful assembly: from the
  reset vector (`PC=0x0000`), the real ROM performs a `JR` to its actual
  init code at `0x0068`, initializes I/O ports via a sequence of `LD A,n` /
  `OUT (n),A` pairs (matching the documented ABCbus/PIO port range below),
  sets up its own stack with `LD SP,HL`, clears working variables, and
  enters what is unmistakably a RAM-size-detection loop (complement-and-XOR
  probing of successive memory pages) — all real, meaningful ABC80 boot
  behavior, not guessed at. Ran cleanly for 5,000,000 instructions
  (59,285,011 T-states) with zero unimplemented-opcode failures, visiting
  191 distinct addresses across `0x0000`–`0x20D0` (three of the four ROM
  chips) before settling into a steady-state loop — expected, since nothing
  yet exists to interrupt it (no video sync, no keyboard, no timer).

### The core split this required

`z80_step()` (`cpm/emu/src/z80.c`, pre-Milestone-1) unconditionally
intercepted `PC == 0x0005` and `PC == 0x0000` on every instruction to catch
CP/M BDOS/BIOS calls. Both addresses sit inside real ABC80 ROM code (ROM
occupies `0x0000`-`0x3FFF`, execution starts at `0x0000`), so calling it
unmodified against ABC80 firmware would have misinterpreted normal ROM
execution as BDOS/BIOS calls. Fixed by extracting the CP/M-agnostic
instruction-execution body (interrupt sampling, opcode fetch/dispatch,
R-register increment — unchanged) into `z80_execute()` in `z80.c`, and
moving the three CP/M-specific lines into a `z80_step()` now defined in
`cpm.c` that calls `z80_execute()` after its own BDOS/BIOS interception.
`z80.c`/`alu.c` no longer reference `cpm.h` at all. Confirmed
behavior-preserving for the CP/M target: `make test` (ZEXALL, ZEXDOC,
`test_interrupts`, every `.asm` example) still reports `PASS` after the
split, with no new compiler warnings.

## Milestone 2: video generation

**Goal**: render `0x7C00`-`0x7FFF` (video RAM) to actual on-screen text, so
ROM boot progress becomes visible instead of only inferable from a PC trace.

- [ ] Decode the character-generator PROM (`char-rom.bin`, archived at
      abc80.net alongside the BASIC ROMs) into a glyph bitmap table.
- [ ] Decode the sync/attribute/line-address PROMs (checksums documented in
      MAME's driver) enough to reproduce the 40×24 text-mode layout and any
      per-character attribute bits (the ABC80's "Teletext"-style attributes) —
      not full analog-timing accuracy, just the logical row/column/attribute
      mapping video RAM bytes go through.
- [ ] Pick and wire up a display backend. Two real options, not yet decided:
      a terminal-based renderer (cheapest, consistent with how `bin/z80`
      already does console I/O) vs. a real windowed framebuffer (closer to
      real hardware, needed eventually for block graphics). Revisit once
      Milestone 3 (keyboard) makes interactive use possible — the choice
      affects both together.
- [ ] Regression check: confirm the real ROM's own sign-on/startup message
      (if any is written to video RAM before the machine reaches its
      current steady-state loop) renders correctly, the same "grounded in
      real firmware behavior" bar Milestone 1 used.

## Milestone 3: Z80 PIO + keyboard input

**Goal**: reach an actually interactive BASIC prompt — the first milestone
where a person can type something and see a response.

- [ ] Implement the Z80 PIO device behind ports `0x10`-`0x13` (mirrored
      `0x14`-`0x17`): mode control, keyboard scan-matrix reads. The keyboard
      matrix PROM (`abc80-keyboard.bin`) is already archived at abc80.net.
- [ ] Map host keystrokes to the ABC80 scan matrix.
- [ ] Confirm the ROM's current steady-state busy-loop (observed at the end
      of Milestone 1, `PC≈0x02F7`) is in fact waiting on PIO/keyboard state
      — verify against the ROM disassembly rather than assuming, since it
      could equally be waiting on a video timing signal (Milestone 2) or
      something else not yet identified.
- [ ] Regression check: real, typed BASIC input (e.g. `PRINT 1+1`) produces
      correct output — the ABC80 equivalent of Phase 3's Tasty Basic
      real-software validation milestone in `cpm/docs/ROADMAP.md`.

## Milestone 4: cassette storage (SAVE/LOAD)

**Goal**: load and save real BASIC programs, the ABC80's standard storage
medium (no disk drive without an ABCbus expansion card — Milestone 6).

- [ ] Cassette read/write via the Z80 PIO (shared hardware with Milestone 3,
      so sequenced after it).
- [ ] A host-file-backed cassette image, the natural equivalent of `bin/z80`'s
      own `cpm_disk/` host-directory mapping for CP/M file I/O.

## Milestone 5: SN76477 sound

**Goal**: port `0x06` produces audible output matching the real complex
sound generator chip. Lower priority than 2-4 — cosmetic, not needed to
reach a usable interactive machine.

- [ ] Model the SN76477's relevant control bits (tone/noise/envelope) well
      enough for recognizable BASIC `SOUND`-statement output.

## Milestone 6: ABCbus expansion

**Goal**: RAM expansion to the real machine's full 32KB, and real disk
storage.

- [ ] RAM-expansion card emulation, replacing Milestone 1's flat-RAM
      simplification for `0x4000`-`0xBFFF` with something that actually
      reflects "card present vs. not" per real hardware.
- [ ] Floppy/DOS controller card + ABC-DOS or UFD-DOS ROM loading (both
      already archived at abc80.net, alongside the BASIC ROMs already in
      `abc80/resources/rom/`).

## Memory map (grounded, not guessed)

Cross-checked between MAME's current mainline driver
(`src/mame/luxor/abc80.cpp`) and an independent Swedish ABC80 hobbyist-forum
mention of the screen memory address (31744–32767 decimal =
`0x7C00`–`0x7FFF`, matching MAME exactly) — this resolved a discrepancy
found against an older/outdated MESS driver fork, which is why both sources
are recorded here rather than trusting either alone.

| Range | Contents |
|---|---|
| `0x0000`-`0x3FFF` | ROM: four 4Kx8 chips — `3506_3.a5`/`3507_3.a3`/`3508_3.a4`/`3509_3.a2` at `0x0000`/`0x1000`/`0x2000`/`0x3000`. CRC32 `e2afbf48`/`d224412a`/`1502ba5b`/`bc8860b7`. |
| `0x4000`-`0xBFFF` | External ABCbus expansion (RAM-expansion cards, floppy/DOS controller, etc.), minus the video RAM carve-out below. |
| `0x7C00`-`0x7FFF` | Video RAM (1KB — 40×24 char cells). |
| `0xC000`-`0xFFFF` | Onboard RAM (16KB base configuration). |

I/O ports (global mask `0x17`, i.e. ports repeat every `0x18`):

| Port(s) | Function |
|---|---|
| `0x00`-`0x01` | ABCbus data/status |
| `0x02`-`0x05` | ABCbus control lines C1-C4 |
| `0x06` | SN76477 sound chip |
| `0x07` | ABCbus reset |
| `0x10`-`0x13` (mirrored `0x14`-`0x17`) | Z80 PIO |

## Known gaps / near-term technical debt

Everything not yet implemented is tracked as a concrete Milestone above
(2-6), not just listed here — this section is a quick-scan summary, not
where the real detail lives:

- No video, PIO/keyboard, cassette, sound, or ABCbus/floppy yet (Milestones
  2-6) — the reason the emulator currently just spins in a busy-wait loop
  once ROM init completes rather than reaching a usable BASIC prompt.
- **Memory-map fidelity for `0x4000`-`0xBFFF`**: modeled as ordinary flat RAM
  for now rather than correctly floating/unmapped when no expansion card is
  present — a deliberate, documented simplification (see Milestone 1 above),
  not an oversight. Real hardware without a RAM-expansion card installed
  would see inconsistent/floating reads there. Folded into Milestone 6.
- **ROM write-protection**: `0x0000`-`0x3FFF` is writable in this model,
  matching this repo's existing flat-memory-model precedent for the CP/M
  target (`CLAUDE.md`'s Architecture section) rather than a new abstraction
  introduced early. No milestone yet — revisit only if something concrete
  needs it, same standard `cpm/docs/ROADMAP.md` applies elsewhere.

## Sources consulted

- MAME mainline driver: `src/mame/luxor/abc80.cpp` (memory map, I/O map,
  ROM filenames/CRC32 checksums, machine configuration).
- *Mikrodatorns ABC* (Gunnar Markesjö) — block diagrams and partial circuit
  schematics covering most of the machine; full text at
  <https://archive.org/stream/microdatorns_abc/microdatorns_abc_djvu.txt>.
- ABC80 service manual, PC/M Personal Computer Museum archive:
  <https://www.abc80.net/archive/luxor/ABC80/ABC80-servicemanual.pdf>.
- ROM images: <https://www.abc80.net/archive/luxor/Prom/fw/ABC80/> — see
  `abc80/resources/rom/README.md` for the exact files and checksum
  cross-check against MAME.
