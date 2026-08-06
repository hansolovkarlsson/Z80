# Game Boy Emulator Roadmap

## Project

A Game Boy (DMG, the original 1989 hardware) emulator, sharing this
repo with the Z80/CP-M emulator but built as a separate subproject
under `gameboy/` - see `gameboy/README.md` for the directory layout
and why cartridge ROMs are split into `roms/` (your own dumps, never
committed) vs `test_roms/` (open-source test suites, safe to commit).
Not started yet: this document exists to scope the work before writing
code, the same way `cpm/docs/ROADMAP.md` tracked the Z80 core before it was
built. This document tracks *status* - for the actual SM83 instruction
set and DMG hardware behavior (memory map, PPU, APU, timer, joypad),
see `gameboy/docs/CPU_REFERENCE.md` and `gameboy/docs/HARDWARE_REFERENCE.md`
instead, the same split `cpm/docs/ROADMAP.md`/`cpm/docs/Z80_REFERENCE.md`/
`cpm/docs/CPM_REFERENCE.md` already establish on the CP/M side.

## The CPU: Sharp SM83 (commonly called "LR35902")

Z80-*derived*, not a Z80. The real, confirmed differences from the
Z80 core already in `cpm/emu/src/z80.c`:

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
one of these, the same way `cpm/resources/ccp/upstream/ccp.asm` gets
cited for real CCP behavior rather than "CP/M documentation summaries"
per `CLAUDE.md`'s own stated preference.

## Architecture decision: standalone core, not shared with `z80.c`

A previous session's notes (`cpm/docs/ROADMAP.md`'s old Phase 4 entry)
sketched extracting `z80.c`/`alu.c` into a shared `core/` and
parameterizing the opcode table for the SM83's differences. Starting
this project standalone instead: `gameboy/src/` gets its own CPU core,
own opcode table, own ALU code, no dependency on `cpm/emu/src/`. Reasoning:
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
  framebuffer, not text - the `cpm/gtk/` subproject's approach (spawn the
  real, unmodified core binary attached to a pty, let a `VteTerminal`
  widget do the interpretation) doesn't transfer directly, since there's
  no terminal escape-code stream to interpret. A GTK+Cairo (or SDL2)
  window blitting a pixel buffer is the likely shape, decided when this
  phase actually starts rather than now.

## Status

**Phase 1 (CPU core): functionally complete and passing its gate.**
`gameboy/src/cpu.c`/`alu.c` implement the full SM83 instruction set -
every opcode in both the unprefixed and CB-prefixed tables, table-driven
in the same spirit as `cpm/emu/src/z80.c` (generic decode for the four fully
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
(GPLv2, committed at `cpm/emu/zexall/`) - the same cautious call already
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

**Phase 4 (interrupts, timer, joypad input): done, dmg-acid2 prediction confirmed.**
`gameboy/src/cpu.c`'s `gb_cpu_step()` now actually dispatches interrupts
(push `PC`, jump to `0x40`/`0x48`/`0x50`/`0x58`/`0x60`, 20 T-states,
priority by bit order) instead of just leaving `IF` bits set for no one
to read - grounded against pandocs' `Interrupts.md` (fetched during
this phase). The HALT bug (flagged unimplemented back in Phase 1) is
now real too: `IME=0` with an interrupt already pending at `HALT` time
sets `halt_bug` instead of actually halting, and `gb_cpu_step()`
replays the following instruction a second time with its real side
effects (not just a refetch) before continuing normally - matching
pandocs' `halt.md` precisely, including *why* it happens (a skipped PC
increment), not just the visible symptom.

`gameboy/src/timer.{c,h}` (new) implements `DIV`/`TIMA`/`TMA`/`TAC`
(`0xFF04`-`0xFF07`) as the real hardware does: a free-running 16-bit
"system counter" (`DIV` is just its visible upper byte) with `TIMA`
incrementing on a *falling edge* of one specific counter bit (selected
by `TAC`'s clock-select field), not a naive independent periodic
counter. That choice isn't pedantry - it's what makes two genuinely
obscure, easy-to-get-wrong behaviors fall out for free instead of
needing special-casing: writing `DIV` (or executing `STOP`, which resets
the same counter) can cause a spurious `TIMA` tick if the monitored bit
happened to be set; and a `TIMA` overflow doesn't reload from `TMA` and
request an interrupt until one M-cycle *after* the overflow, reading
`$00` in between (pandocs' `Timer_Obscure_Behaviour.md`). Both are
covered by `gameboy/tests/test_timer.c` (`make gameboy-test`, 14
checks) directly, independent of any ROM.

`gameboy/src/joypad.{c,h}` (new) implements `P1`/`JOYP` (`0xFF00`) -
the action/direction button multiplexing and its inverted "0 = pressed"
polarity (pandocs' `Joypad_Input.md`) - and a `gb_joypad_set_action()`/
`gb_joypad_set_direction()` API for a future front-end or test harness
to call. No real input source exists yet (still Phase 7's job), so the
joypad reports "nothing pressed" for the whole run in `main.c` as of
this phase - the register and interrupt-request logic are real and
tested (correct multiplexing, correct polarity), just never actually
driven by anything yet.

**Correctness gate, part 1**: all 12 of Blargg's `cpu_instrs`/
`instr_timing` sub-tests now pass, including the two that failed back
in Phase 1/2 specifically *because* interrupts/timer didn't exist yet
(`02-interrupts`, `instr_timing`) - exactly the predicted outcome, not
a surprise.

**Correctness gate, part 2**: `make gameboy-visual-test` (dmg-acid2)
jumped from Phase 3's 91.31% to **22589/23040 (98.04%)** the moment
interrupt dispatch existed - direct, strong confirmation that the
Phase 3 diagnosis (nearly every visual detail is gated behind a
mid-frame `LY`=`LYC` interrupt actually firing and being handled) was
right, not a guess that happened to sound plausible. A side-by-side
comparison shows the predicted features now rendering correctly: the
"HELLO WORLD!" exclamation mark, correct eye rendering, and most of the
footer text ("dmg-acid2 by Ma..." - see below for what's still off).

**Remaining gap, honestly reported**: 451 pixels still mismatch,
concentrated in exactly two places - the top row (`LY=0`) of the
"HELLO WORLD!" banner (66 pixels, *unchanged* from Phase 3's count
before interrupt dispatch existed - direct proof this specific one
isn't interrupt-timing-related at all), and the tail end of the footer
text (`LY=133`-`141`, 385 pixels - the footer is *present* now, just
cut off partway, consistent with a timing-related issue this time).
Both are plausibly connected to the same root cause: `gb_ppu_step()`
still renders each scanline all at once when Mode 3 completes (Phase
3's documented simplification), rather than progressively pixel-by-
pixel the way real hardware's pixel FIFO does - dmg-acid2's own README
says its register writes happen "during mode 2 (OAM scan)" specifically
*because* real hardware can react within that window, but this
emulator's CPU/PPU/timer are stepped in sequence once per whole
instruction rather than interleaved sub-instruction, which can shift
exactly when an `LY`=`LYC` interrupt actually gets serviced relative to
when a scanline gets drawn. This is a real, open issue - not silently
swept under the "documented simplification" umbrella without stating
plainly that it isn't fully root-caused - see `compare_frame.py`'s own
95%-floor regression check (set from this 98.04% baseline, not 100%)
for how future changes get checked against it.

**Next**: Phase 5 (APU/sound) or Phase 6 (real-game validation) - both
now genuinely possible for the first time, since interrupts/timer are
what most real games actually need to be playable rather than just
bootable. The row-0/footer gap above is worth a dedicated debugging
pass whenever precise Mode-3 pixel timing becomes the active work,
rather than something to chase down mid-Phase-4.

**Phase 5 (APU/sound): done, with several genuinely obscure quirks
honestly deferred.** `gameboy/src/apu.{c,h}` (new) implements all four
sound channels (two pulse, one wave, one noise), the DIV-APU frame
sequencer (512 Hz, tied to `DIV` bit 4's falling edge - the same real-
hardware-counter approach `timer.c` already uses for `DIV`/`TIMA`, not
an independent counter), CH1's sweep unit with its own shadow register,
length timers, envelope, DAC on/off, and `NR50`/`NR51`/`NR52` mixing
including the documented DMG high-pass filter. `gameboy/src/mmu.c`
routes the full `0xFF10`-`0xFF3F` span (not two narrower ranges split
around `NR52`/Wave RAM, which silently missed the `0xFF27`-`0xFF2F`
gap registers - found via Blargg's `01-registers.gb`, see below) to it.
`main.c` gained `--wav`/`--seconds` flags to dump generated audio as a
standard 16-bit PCM WAV file, since `dmg_sound`'s later sub-tests
report results via the screen rather than serial output, needing
`--ppm` (viewed as a PNG) rather than grepped stdout. Every register
layout, the frame sequencer's timing, the DAC's negative-slope analog
mapping, and the high-pass filter's own cited algorithm are grounded
against pandocs' `Audio.md`/`Audio_Registers.md`/`Audio_details.md`
(fetched during this phase), not guessed.

Two of `Audio_details.md`'s "Obscure Behavior" quirks around the length
timer's interaction with the DIV-APU frame sequencer's phase are also
implemented, not just documented as deferred - found necessary (not
optional polish) by Blargg's own `03-trigger.gb` and
`08-len ctr during power.gb`, both of which use them as their actual
measurement technique for probing the length counter's otherwise-
unreadable internal state: writing `NRx4` with a 0-to-1 length-enable
transition on a frame-sequencer step that wouldn't itself have clocked
length immediately clocks it once early (`extra_length_clock_on_enable()`),
and triggering a channel under the same condition, when length is being
reloaded from zero, reloads to one below max instead of max
(`trigger_length_reload()`).

**A real, separate bug found and fixed this phase**: `01-registers.gb`
("Failed #2") caught `mmu.c`'s APU routing gap above - `0xFF27`-`0xFF2F`
(nine unused registers between `NR52` and Wave RAM) fell through to
plain flat memory instead of the APU's own read-as-`$FF`/ignore-write
handling, since the original routing was two ranges split around that
gap rather than one contiguous span. Diagnosed by building an isolated
C reproduction of the test's own register/mask table first (ruling out
an `apu.c`-only logic bug), then re-deriving the test's actual address
range from its source rather than guessing - confirmed via the real
Blargg assembly (`retrio/gb-test-roms`' `dmg_sound/source/*.s`, fetched
during this phase, the same "get the primary source, don't guess"
discipline `CLAUDE.md` already documents for the CP/M side).

**A real design correction found and fixed this phase**: an earlier
version of the `NR52` power-off handler excluded `NR11`/`NR21`/`NR31`/
`NR41` (the length-timer registers) from being zeroed, based on a
literal reading of pandocs' footnote that length timers are unaffected
by power-off on DMG. `01-registers.gb`'s test 5 (fills every register
with `$FF`, powers off, expects a full clear) proved this too broad:
`NR11`/`NR21`'s duty-cycle bits (6-7) are real, readable register bits
that *do* clear on power-off, distinct from the internal length
countdown (a separate `GBApuChannel.length_timer` field, never derived
from the raw register byte at read time) that survives. A second,
related correction: `fill_apu_regs`'s own loop (used by `08-len ctr
during power.gb`) writes every register including `NR52` last, leaving
the APU powered off by the time the test's own length-counter-loading
writes run - initially dropped entirely by the blanket "ignore every
write while off" guard, until re-checked against the same pandocs
footnote taken correctly this time: on DMG, an `NRx1` write's length-
*reload* reaches the internal counter even while powered off (bypassing
the register bank the rest of that guard protects), while the byte's
own readable bits still don't change. Both fixes were verified against
the real Blargg assembly source (`08-len ctr during power.s`'s own
comment: "On CGB, length counters are reset when powered up. On DMG,
they are unaffected, and not clocked") rather than re-guessed a third
time.

**Correctness gate**: Blargg's `dmg_sound` sub-tests (fetched from
`retrio/gb-test-roms` for testing, same ambiguous-license/not-committed
situation as `cpu_instrs` - see Phase 1's licensing note) - **7 of 12
pass**: `01-registers`, `02-len ctr`, `03-trigger`, `04-sweep`,
`06-overflow on trigger`, and `11-regs after power` all print `Passed`.
The 5 that don't are genuinely obscure, narrow hardware behaviors, not
signs of a broader problem - and are being left deferred rather than
chased indefinitely, the same call already made and documented for
dmg-acid2's remaining ~2% gap in Phase 4:

- `05-sweep details` (Failed #4, "Exiting negate mode after calculation
  disables channel"): a real, pandocs-documented CH1 sweep quirk not
  implemented - `sweep_calc()`/`tick_sweep()` in `apu.c` don't yet track
  "was negate mode used since the last trigger."
- `07-len sweep period sync` (Failed #5, "Powering up APU MODs next
  frame time with 8192"): an APU-power-on/frame-sequencer-phase-
  synchronization detail not yet root-caused - this project's frame
  sequencer resets its own step counter independent of any fixed phase
  relationship to the power-on event itself, and pandocs doesn't specify
  one explicitly enough to implement with confidence rather than guess.
- `08-len ctr during power` (Failed, checksum mismatch): partially
  root-caused this phase (see the two fixes above, both found via this
  exact test) but the final printed length-counter values are still
  consistently one tick off from what the test's checksum expects,
  even after both quirks above are correctly modeled and hand-verified
  against the test's own `get_len_a` polling algorithm
  (`retrio/gb-test-roms`' `cpu_instrs/source/common/apu.s`, the shared
  helper `dmg_sound` also uses) - the remaining gap is plausibly a
  cycle-exact frame-sequencer-phase detail in the boot-time `sync_apu`
  alignment this test relies on, not a logic error in the two quirks
  themselves.
- `09-wave read while on`, `10-wave trigger while on`,
  `12-wave write while on`: all exercise Wave RAM's real mid-playback
  corruption/lock behavior (accessing Wave RAM while CH3 is actively
  reading it doesn't behave like a normal RAM access on real hardware) -
  deliberately not modeled, and already flagged as such in `apu.h`'s own
  top-of-file comment from when this phase started, not a new gap found
  during testing.

**Next**: Phase 6 (real-game validation) - now the natural next step,
since CPU/PPU/interrupts/timer/joypad/APU all exist and a real game can
plausibly run start-to-finish for the first time. The five `dmg_sound`
gaps above are worth a dedicated pass if audio-accuracy work becomes
the active focus again, particularly `08`'s remaining one-tick
discrepancy given how close the current implementation already is.

**Phase 6 (real-game validation): a real, unmodified homebrew game
boots, plays, and merges tiles correctly.** Same convention this
project already uses on the CP/M side (`CLAUDE.md`'s stated session
convention: find a bug via real software, root-cause it against a
grounded reference, fix, add a regression test) - see
`gameboy/test_roms/2048-gb/README.md` for the full story. The target was
[2048-gb](https://github.com/Sanqui/2048-gb) (zlib-licensed, committed
to `gameboy/test_roms/2048-gb/` same as dmg-acid2), a complete, real
homebrew Game Boy port of the 2048 sliding-tile puzzle.

**A real bug found immediately, before the ROM would even load**: its
cartridge header declares RAM size code `0x01`, which
`gb_cart_load()`'s `ram_banks_for_code()` (`cart.c`) rejected outright -
the previous phase's own comment there called `0x01` "never used by any
real cartridge," which pandocs' `The_Cartridge_Header.md` itself
contradicts once read carefully: `0x01` is officially "Unused," but the
same page documents that "Various 'PD' ROMs... are known to use the
`$01` RAM Size tag, but this is believed to have been a mistake with
early homebrew tools, and the PD ROMs often don't use cartridge RAM at
all" - exactly this ROM's situation (cart type `0x03`,
MBC1+RAM+BATTERY, but no actual save-game behavior was ever observed in
testing). Fixed by treating code `0x01` as 0 RAM banks rather than a
load error, letting the existing zero-size RAM handling
(`gb_cart_read_ram`/`gb_cart_write_ram`) take over rather than guessing
at a nonstandard chip size.

**No real interactive input source existed at all before this
phase** - `main.c`'s bring-up driver only ever reported "nothing
pressed" (Phase 7, a real front end reading a host keyboard/controller,
was always going to be needed eventually, but real-game validation
needs *some* way to press buttons well before then). Added `--input
<script>`: a plain text file of `<frame> <BUTTON> <down|up>` lines,
timed to VBlank frame count (the same granularity a real player's
presses land on) rather than a raw instruction count, applied via
`gb_joypad_set_action`/`gb_joypad_set_direction` - the exact API
`joypad.h` already documented as "the API a future front-end or test
harness will call," unused until now. This is a scripted test harness,
not Phase 7's real thing, but it's what made this phase's validation
possible at all.

**Validation performed**: booted 2048-gb to its title screen (rendered
frame matches the game's own known title-screen layout - "2048-gb" /
credits / "Press Start!" - see `gameboy/test_roms/2048-gb/README.md`),
scripted a Start press to begin a new game (two `2` tiles spawn, Score/
High score row renders correctly), then scripted `DOWN`/`RIGHT`/`DOWN`
moves - tiles visibly slid and a new tile spawned after each move, and
the final move produced a genuine merge (two `2` tiles combining into a
single `4`, with the score updating from `0` to `4` to match) - real
game logic, not just a static frame, running correctly start-to-finish
for the first time. The full run was confirmed byte-for-byte
deterministic across repeated executions (no host-timing-derived
randomness anywhere in this emulator's reset path), so `make
gameboy-2048-test` locks the post-merge frame in as a plain `cmp`
regression baseline rather than a fuzzy match.

**Next**: Phase 7 (exploratory) - a real interactive front end (GTK+Cairo
or SDL2, decided when that phase actually starts) is now the main
remaining gap between this emulator and something actually playable by
a person in real time, now that a real game has been proven to run
correctly under scripted input. Trying more real ROMs against
`--input` scripts (particularly ones exercising MBC3's RTC or deeper
save-RAM behavior, neither meaningfully exercised by 2048-gb) is also
worth doing opportunistically, without needing a dedicated phase for it.

**Phase 7 (real graphical front end): started, video + input working,
audio deliberately deferred.** `gameboy/gtk/src/main.c` (new, opt-in via
`make gameboy-gtk`, same GTK4-dependency reasoning as `cpm/gtk/`) is a
real playable front end - a GTK4 window rendering the live framebuffer
through Cairo (nearest-neighbor-scaled 4x so the real 160x144 pixel
grid stays sharp) and reading real keyboard input into the joypad
(arrows = D-pad, Z/X = B/A, Enter = Start, Right Shift = Select - the
same default layout convention BGB/SameBoy use, not invented here).
Architecturally different from `cpm/gtk/`'s approach on purpose: that
one spawns the real `bin/z80` as a child process and hands a pty to a
`VteTerminal` widget, which works because CP/M output is a text/
escape-code stream a terminal widget already knows how to interpret.
The Game Boy's output is a raw pixel framebuffer, so this front end
links the core (`gameboy/src/*.c`, minus `main.c`'s own competing
`main()`) directly into one binary instead - no child process, no pty,
and therefore the macOS `posix_spawn`/xzone crash documented in
`cpm/gtk/README.md` (triggered by VTE's own child-spawn path) doesn't
apply here at all. A `g_timeout_add(16, ...)` callback steps the core
one real video frame (70224 T-states) per tick and queues a redraw;
stepping itself takes microseconds, so the ~16ms timer interval is what
actually paces wall-clock speed (a documented, deliberate
approximation of the real 59.7275 Hz - see `main.c`'s own comment - in
the same spirit as `ppu.h`'s existing "Mode 3 is always 172 dots"
simplification). Manually verified stable (steady ~30% CPU, no crash,
no illegal-opcode stop) running both 2048-gb and dmg-acid2 (an
MBC-less cart, unlike 2048-gb's MBC1) for extended periods.

**Live audio output: done**, via CoreAudio's AudioQueue (macOS-specific
by deliberate choice, not oversight - the same judgment call
`cpm/gtk/src/main.c` already made using `<mach-o/dyld.h>` directly
rather than adding portability guards for a platform nothing here is
built/tested on; a portable library like SDL2 was the other option
considered, rejected to avoid a second external dependency alongside
GTK4). `gameboy/src/apu.c` was already generating real samples
(`main.c --wav` proved that) - the gap was purely playback. `setup_audio()`/
`flush_audio()` (`gtk/src/main.c`) use a "push" model matched to how
sample production actually works here: `gb_apu_step()` already paces
itself off the same ~16ms GLib timer driving video (one `step_frame()`
tick = up to one real video frame's ~738 stereo sample pairs), so each
tick just hands whatever accumulated since the last one to CoreAudio as
one small buffer and resets the append position - no ring buffer or
lock-free bookkeeping needed, since there's only ever one producer and
CoreAudio's own completion callback frees each buffer once played.
Cleaned up (`AudioQueueStop`/`AudioQueueDispose`) from the same
`on_window_destroy()` handler that already stops the video timer, same
reasoning as that earlier fix. Manually verified: builds and links
clean against the `AudioToolbox` system framework (no new Homebrew
dependency), runs stably with no CoreAudio errors in the system log
across an extended 2048-gb session.

**Still not done**: Game Boy Color support and save states, the other
two Phase 7 items - both still fully unscoped.

**A sharper diagnosis of dmg-acid2's still-open gap, found through
this front end specifically**: watching it run continuously (rather
than capturing one still frame, all `--ppm` testing ever did) showed a
visible flicker - real, not a GTK rendering artifact. Confirmed by
instrumenting the core directly and diffing consecutive `--ppm`
captures many frames apart: the rendered image cycles through **4
distinct states** indefinitely (LCDC/SCX settle to different values at
VBlank depending on `frame_seen % 4`, e.g. `LCDC=A9,SCX=00` /
`LCDC=C9,SCX=00` / `LCDC=D1,SCX=F3` / `LCDC=D1,SCX=F3` and back), with
up to ~10,000 of 23,040 pixels differing between adjacent frames -
`make gameboy-visual-test`'s 98.04% baseline is only ever measuring
one specific point in that cycle (the `--frames 2` capture), which is
why this was never caught before. Root cause is the same one already
named above, now confirmed at a finer grain: `gb_ppu_step()`'s fixed
172-dot Mode 3 doesn't match real hardware's variable 172-289 dots
(base + `SCX & 7` scroll penalty + per-object and window-restart
fetcher-stall penalties, pandocs' `pixel_fifo.md`), so small
CPU/PPU misalignments compound across dmg-acid2's own `SCX`-driven
raster effects instead of settling into the single static image real
hardware shows. Checked pandocs' actual formula before considering a
fix and deliberately didn't attempt one: it's a genuine per-dot pixel-
FIFO simulation (fetcher steps, object-fetch cancellation, window-
restart pixel injection), not a small `SCX % 8` patch on top of the
current whole-scanline-at-once renderer - a real, separately-scoped
rewrite of `render_scanline()`/`gb_ppu_step()`'s Mode 3 handling, not
attempted here rather than guessed at partially.
