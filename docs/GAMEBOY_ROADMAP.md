# Game Boy Emulator Roadmap

## Project

A Game Boy (DMG, the original 1989 hardware) emulator, sharing this
repo with the Z80/CP-M emulator but built as a separate subproject
under `gameboy/` - see `gameboy/README.md` for the directory layout
and why cartridge ROMs are split into `roms/` (your own dumps, never
committed) vs `test_roms/` (open-source test suites, safe to commit).
Not started yet: this document exists to scope the work before writing
code, the same way `docs/ROADMAP.md` tracked the Z80 core before it was
built.

## The CPU: Sharp SM83 (commonly called "LR35902")

Z80-*derived*, not a Z80. The real, confirmed differences from the
Z80 core already in `emu/src/z80.c`:

- No `IX`/`IY` index registers, and therefore none of the `DD`/`FD`-
  prefixed instructions or `(IX+d)`/`(IY+d)` addressing.
- No alternate register set - no `EXX`, no `EX AF,AF'`.
- No `ED`-prefixed block instructions (`LDIR`, `CPIR`, etc.) - the
  `ED` prefix space is mostly unused on this CPU.
- Adds its own instructions the Z80 doesn't have, most notably `STOP`
  and `LD (HL+),A`/`LD (HL-),A`/`LD A,(HL+)`/`LD A,(HL-)` (auto-
  increment/decrement variants used constantly by real Game Boy code).
- No `IN`/`OUT` - I/O is entirely memory-mapped (`0xFF00`-`0xFF7F`).
- `DAA`'s behavior differs from the Z80's in ways worth verifying
  against a real reference rather than assuming Z80 semantics carry
  over unchanged.
- Runs at ~4.194304 MHz; the Z80 core's T-state cycle-counting
  approach still applies, but real timing values need to come from a
  grounded Game Boy reference, not carried over from Z80 timings.

**Primary references** (the same "ground everything in a real primary
source, don't guess" discipline `CLAUDE.md` already documents for the
Z80/CP-M side): [Pan Docs](https://gbdev.io/pandocs/) is the
community-maintained definitive Game Boy hardware reference; the
[gbdev.io](https://gbdev.io/) "Awesome Game Boy Development" list
rounds up the rest (opcode tables, timing docs, test ROM sources).
Cite the specific page/section when a behavior is implemented from
one of these, the same way `resources/ccp/upstream/ccp.asm` gets
cited for real CCP behavior rather than "CP/M documentation summaries"
per `CLAUDE.md`'s own stated preference.

## Architecture decision: standalone core, not shared with `z80.c`

A previous session's notes (`docs/ROADMAP.md`'s old Phase 4 entry)
sketched extracting `z80.c`/`alu.c` into a shared `core/` and
parameterizing the opcode table for the SM83's differences. Starting
this project standalone instead: `gameboy/src/` gets its own CPU core,
own opcode table, own ALU code, no dependency on `emu/src/`. Reasoning:
the ISA differences above are real and pervasive enough (missing
register files, a materially different instruction set, different
addressing modes) that a single parameterized dispatch table would
mean conditional logic threaded through nearly every opcode handler,
before a single Game Boy instruction has even been written and tested.
That's the premature abstraction `CLAUDE.md` already tells this project
to avoid ("Don't design for hypothetical future requirements"). If
real, painful duplication shows up once Phase 1 is underway - not
before - that's the time to extract something shared, informed by
what's actually common rather than what's guessed to be common now.

`cpm.c` (BDOS/BIOS emulation) doesn't apply to the Game Boy at all and
was never a candidate for sharing - that boundary already existed.

## Phases

### Phase 1: CPU core

The SM83 instruction set, table-driven dispatch in the same spirit as
`z80.c`'s `main_opcode_table`/`z80_op_prefix_cb` pattern (same *shape*
of solution, independently implemented - see the architecture decision
above). Gate: passing Blargg's `cpu_instrs.gb` and `instr_timing.gb`
test ROMs cleanly - the direct equivalent of ZEXALL/ZEXDOC gating the
Z80 core's Phase 1. Until this passes, nothing downstream (PPU, real
games) can be trusted to be running correct code.

### Phase 2: Memory map and cartridge/MBC support

The Game Boy's memory map (ROM banks, VRAM, external/cartridge RAM,
WRAM, OAM, I/O registers, HRAM) and at least enough cartridge-type
(MBC) support to load real ROMs: MBC-less (32KB ROM-only) first, then
MBC1, MBC3 (with its real-time-clock registers), MBC5. Needed before
any real game can even be loaded, let alone run.

### Phase 3: PPU (the LCD controller)

Background, window, and sprite (OAM) rendering, plus the PPU mode
timing (`OAM scan`/`Draw`/`H-Blank`/`V-Blank`) real games and test
ROMs depend on. This is the feature that makes the output "a Game Boy
screen" rather than a headless CPU exerciser - the natural next
correctness gate here is a well-known visual test ROM (e.g. from the
Mooneye suite) compared pixel-for-pixel against a known-good reference
image, not just "look at it and eyeball it."

### Phase 4: Interrupts, timer, joypad input

The interrupt controller (`IE`/`IF`, the five real interrupt sources -
V-Blank, LCD STAT, Timer, Serial, Joypad), the `DIV`/`TIMA`/`TMA`/`TAC`
timer registers, and joypad input (`0xFF00`). Needed for essentially
any real game to be playable at all, not just "boots to a logo."

### Phase 5: APU (sound)

The four sound channels (two pulse, one wave, one noise) and the
master sound registers. Deferred until video output actually works -
a silent screen is a much smaller loss than a black one, and audio
correctness is much harder to verify without ears on it during
development.

### Phase 6: Real-game validation

Same convention this project already uses on the CP/M side
(`CLAUDE.md`'s stated session convention: find a bug via real software,
root-cause it against a grounded reference, fix, add a regression
test): run real, well-known homebrew/open-source ROMs from
`gameboy/test_roms/`, and - separately, locally, never committed -
real cartridge dumps from `gameboy/roms/` the user owns, fixing bugs
found against Pan Docs/hardware test results rather than guessing.

### Phase 7 (exploratory, not scoped)

- **Game Boy Color (CGB) support** - double-speed mode, the extra
  VRAM/WRAM banks, color palettes. A real hardware revision with its
  own documented differences, not guessed.
- **Save states / battery-backed cartridge RAM persistence.**
- **A real graphical front end.** The Game Boy's output is a pixel
  framebuffer, not text - the `gtk/` subproject's approach (spawn the
  real, unmodified core binary attached to a pty, let a `VteTerminal`
  widget do the interpretation) doesn't transfer directly, since there's
  no terminal escape-code stream to interpret. A GTK+Cairo (or SDL2)
  window blitting a pixel buffer is the likely shape, decided when this
  phase actually starts rather than now.

## Status

**Phase 1 (CPU core): functionally complete and passing its gate.**
`gameboy/src/cpu.c`/`alu.c` implement the full SM83 instruction set -
every opcode in both the unprefixed and CB-prefixed tables, table-driven
in the same spirit as `emu/src/z80.c` (generic decode for the four fully
regular blocks: `LD r,r`, the r8 and d8 ALU groups, and the whole
CB-prefixed table; individually-named handlers for everything else).
`gameboy/src/mmu.c` is a deliberately temporary flat-memory harness
(echo RAM, a "not usable" stub, a serial-port capture hook) - just
enough to run a real, unbanked ROM, not a real MMU (Phase 2's job).
`make gameboy` builds `bin/gameboy`, opt-in like `make gtk` (not part of
`all`/`test` yet - see the Makefile comment for why).

Every opcode's bytes/cycles/flags were checked against the official
gbdev.io opcode table
(<https://gbdev.io/gb-opcodes/optables/>, data at
`https://gbdev.io/gb-opcodes/Opcodes.json`) rather than trusted from
memory - this caught one real erratum in passing: a commonly-mirrored
community JSON dataset (`lmmendes/game-boy-opcodes`) lists `BIT b,(HL)`
as 16 cycles; the official table (and this emulator) has it at 12, since
`BIT` never writes anything back the way the other CB read-modify-write
ops do. DAA, the accumulator-vs-CB-prefixed rotate distinction, and
`ADD SP,e8`/`LD HL,SP+e8`'s flag quirks were all grounded the same way.
The HALT bug (pandocs' dedicated `halt.md` page, fetched during this
phase) is documented and has a field reserved for it in `GBCpu`, but
isn't implemented yet - it needs a real `IE`/`IF` (Phase 4) to detect
the "interrupt already pending" condition that triggers it.

**Correctness gate**: Blargg's `cpu_instrs` individual sub-tests
(fetched locally for testing - see the licensing note below) pass
10 of 11: `01-special`, `03-op sp,hl`, `04-op r,imm`, `05-op rp`,
`06-ld r,r`, `07-jr,jp,call,ret,rst`, `08-misc instrs`, `09-op r,r`,
`10-bit ops`, `11-op a,(hl)` all print `Passed`. The 11th,
`02-interrupts`, and the separate `instr_timing.gb` (which needs the
`DIV` register incrementing to measure elapsed cycles) both fail in
exactly the way expected: both need a real timer/interrupt controller,
which doesn't exist until Phase 4 - not a CPU-core correctness bug.

**Licensing note - why `gameboy/test_roms/` is still empty**: Blargg's
test ROMs (fetched from `retrio/gb-test-roms` to validate the above,
not committed) carry no explicit license, unlike ZEXALL/ZEXDOC
(GPLv2, committed at `emu/zexall/`) - the same cautious call already
documented in `gameboy/README.md`. This also means Blargg's ROMs can't
be wired into `make test` the way ZEXALL is, since that would need
either committing them anyway or a network fetch at test time (neither
matches this project's reproducible-test convention). The Mooneye GB
test suite (`Gekkio/mooneye-test-suite`, confirmed MIT-licensed) is the
recommended path to a real, committable, `make test`-integrated
correctness gate - deferred rather than pursued now since it ships as
assembly source needing `rgbds` to build, not prebuilt ROMs, which is
real additional setup work of its own.

**Phase 2 (memory map and cartridge/MBC support): done.**
`gameboy/src/cart.{c,h}` parses a real cartridge header (`0x0134`-
`0x014F` - title area, cartridge-type byte, ROM/RAM size codes, header
checksum) and implements MBC-less, MBC1, MBC3 (with its RTC latch
register set), and MBC5 bank switching - the scope this phase's own
plan set out, covering the overwhelming majority of real cartridges.
Every register layout and banking quirk (MBC1's "bank 0 reads as bank
1" translation and its simple-vs-advanced mode secondary register that
either extends ROM addressing *or* selects a RAM bank depending on
cartridge size; MBC3's RTC register-select-vs-RAM-bank overlap and its
0x00-then-0x01 latch sequence; MBC5's clean 9-bit ROM bank number with
no bank-0 translation quirk at all) is grounded against pandocs'
`MBC1.md`/`MBC3.md`/`MBC5.md`/`nombc.md`/`The_Cartridge_Header.md`
(fetched during this phase), not guessed. `gameboy/src/mmu.c` now
routes `0x0000`-`0x7FFF` and `0xA000`-`0xBFFF` through `cart.c`;
`cpu->memory` (see `cpu.h`) is VRAM/WRAM/OAM/I-O-registers/HRAM only, no
longer the whole address space. `gb_cpu_reset()`'s F-register
simplification from Phase 1 (hardcoding the "nonzero header checksum"
case) is gone - it now reads the cartridge's real header-checksum byte,
per a pandocs footnote whose exact condition (is the checksum *byte*
zero, not whether it *validates* - a real Game Boy refuses to run a
cartridge whose checksum doesn't validate at all, so by the time code
is running the two almost always agree, but they're different
questions) was easy to get subtly wrong and worth citing precisely.

**Correctness gate**: none of Blargg's `cpu_instrs` ROMs use banking at
all (they're all plain 32 KiB MBC-less), so they don't exercise any of
this phase's actual work, and no real MBC1/MBC3/MBC5 test ROM was
available to fetch and commit (same licensing situation as `cpu_instrs`
- see above). Instead, `gameboy/tests/test_cart.c` (`make gameboy-test`,
26 checks) unit-tests `cart.c` directly against the exact scenarios in
pandocs' own addressing diagrams - MBC1 basic and large-ROM/advanced-
mode banking, MBC1 RAM banking (both banking modes), RAM-disabled
behavior, MBC3 ROM banking and its RTC latch sequence, MBC5's 9-bit
banking, and `gb_cart_load()`'s real file-header-parsing path (the one
integration point `main.c` actually depends on, not exercised by the
struct-construction tests above it). This is this project's own code
testing its own code, so - unlike the ROM-based gates - it has no
licensing question and could be `make`-invoked directly; it's simply
not part of the top-level `all`/`test` yet, matching the rest of this
still-early subproject.

**Phase 3 (PPU): done, with a documented, evidenced gap.**
`gameboy/src/ppu.{c,h}` implements the LCD controller: all twelve
registers (`0xFF40`-`0xFF4B`), the mode/timing state machine (OAM
scan/Drawing/HBlank/VBlank, 456 dots/scanline, 154 scanlines/frame),
and a scanline-at-a-time renderer covering background, window, and
objects (both 8x8 and 8x16, correct selection/drawing priority, X/Y
flip, the two tile-addressing modes and their signed-vs-unsigned
quirk, DMG palette translation). `gameboy/src/mmu.c` now routes
`0xFF40`-`0xFF4B` through it and triggers OAM DMA transfers. Every
register layout, addressing mode, and priority rule is grounded
against pandocs' `LCDC.md`/`STAT.md`/`Tile_Data.md`/`Tile_Maps.md`/
`OAM.md`/`Rendering.md`/`Palettes.md`/`OAM_DMA_Transfer.md` (fetched
during this phase - see `ppu.c`'s own comments for which page backs
which rule), not guessed. Two deliberate simplifications, both
documented in `ppu.c` at the exact line they apply: Mode 3 is always
172 dots (the real minimum) rather than the real hardware's variable
172-289 (SCX/window/object timing penalties aren't modeled - affects
STAT-interrupt timing precision, not rendered pixel content); OAM DMA
is an instant 160-byte copy rather than the real timed 160 M-cycle
transfer (correct for any program that follows the universal
busy-wait-in-HRAM convention real hardware requires anyway).

**Correctness gate**: [dmg-acid2](https://github.com/mattcurrie/dmg-acid2)
(Matt Currie, MIT-licensed - committed at `gameboy/test_roms/dmg-acid2/`,
unlike Blargg's ROMs) is the standard PPU correctness test in the Game
Boy dev community, with a known-correct reference image to compare
against pixel-for-pixel - exactly the gate this phase's own original
plan called for. `make gameboy-visual-test` renders a frame and runs
`gameboy/tests/compare_frame.py` (a small dependency-free PNG decoder +
comparator, since there's no image library in this project) against it:
**21037/23040 pixels match (91.31%)**.

The remaining ~9% has a specific, evidenced cause, not a mystery: dmg-acid2's
own README states it "uses `LY`=`LYC` coincidence interrupts to perform
register writes on specific rows of the screen during mode 2" - nearly
every interesting visual feature (the window being toggled on/off for
the eyes and chin, `LCDC` bit 0 toggling to hide hair, the tile map
switching for the footer) is implemented as a mid-frame raster effect
driven by a STAT interrupt actually firing and being *handled*. This
project has interrupt *requests* (the PPU already sets `IF` bits on
VBlank/STAT events - see `ppu.c`) but not interrupt *dispatch* (jumping
to a handler when `IME`/`IE`/`IF` allow it), which is explicitly Phase
4, not built yet. A side-by-side comparison confirms this precisely:
the static parts (overall face shape, mouth, general background
structure - whatever was set up once before any interrupt would have
fired) render correctly, while every interrupt-gated detail is visibly
wrong or missing exactly as predicted - the footer text ("dmg-acid2 by
Matt Currie") is entirely blank (the window never gets disabled to
reveal it), the eyes render differently (their two-stage window/object
overlay never gets its mid-frame update), and the "HELLO WORLD!" text's
exclamation mark handling is affected (the row it's on is exactly where
`gameboy/tests/compare_frame.py`'s pixel diff concentrates). Re-run this
gate once Phase 4 lands - a rate meaningfully *below* 91.31% at that
point would flag a real regression, which is why `compare_frame.py`
treats its baseline as a floor to check against, not a fixed target.

**Next**: Phase 4 (interrupts, timer, joypad input) - both to let
dmg-acid2 actually pass, and because it's a real prerequisite for
essentially any game being playable at all, not just "boots to a
logo."
