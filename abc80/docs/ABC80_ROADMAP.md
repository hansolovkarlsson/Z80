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

## Milestone 2: video generation — done

**Goal**: render `0x7C00`-`0x7FFF` (video RAM) to actual on-screen text, so
ROM boot progress becomes visible instead of only inferable from a PC trace.
Achieved — running `bin/abc80` now shows the real ROM's own "ABC80" sign-on
text and a live cursor, not just a PC trace. Not yet done: sound, keyboard
input, and the ABCbus/floppy expansion — see Milestones 3-6 below. Text-mode
rendering is solid; block-graphics rendering depends on the terminal's font
having Unicode sextant-glyph coverage (not yet true of every terminal).

- [x] **Decode the character-generator PROM** — done. The chip (SN74S263N,
      "H2") is mask-programmed, so its content isn't dumped the way the
      BASIC ROMs were; instead `abc80/resources/rom/chargen.bin` was
      downloaded from the same abc80.net archive and verified byte-identical
      (CRC32 `9e064e91`) to the reconstruction embedded in MAME's
      `src/devices/video/sn74s262.cpp` — grounding, though MAME's own source
      marks that reconstruction `BAD_DUMP`/"created by hand" itself (no
      claim of perfection here, just "same as the community-standard
      reference"). Address formula and dimensions lifted directly from
      `sn74s262_device::read()`: 128 characters (7-bit code) × 10 bytes,
      only rows 0-8 real (row 9 always blank), 6 pixels wide, MSB-first —
      implemented in `abc80/emu/src/chargen.c`/`.h`. Verified two ways, not
      just "the formula compiles": `bin/abc80-chargen-dump` (new standalone
      tool) prints every character as ASCII art, and by eye `A`/`B`/`0`/`S`/
      `!` are clean, correct letterforms. More tellingly, character `0x5B`
      (`[` in plain ASCII) decodes to an unambiguous **Ä** (umlaut dots over
      a rounded A) — independent confirmation of *both* the formula and the
      ROM's identity, since MAME's own device-type registration documents
      the SN74S263 specifically as the Swedish/Finnish national-charset
      variant, not plain ASCII. Worth remembering for later text-handling
      work: the punctuation range (`0x40`, `0x5B`-`0x5E`, `0x60`, `0x7B`-
      `0x7E`) is Swedish ISO646, not ASCII.
- [x] **Decode the sync/attribute/line-address PROMs** — done, for the
      logical row/column/attribute mapping specifically (not yet the actual
      pixel-rendering loop — that's next, once a display backend is
      chosen). Four PROMs (`hsync`/`vsync`/`attr`/`line`, all in
      `abc80/resources/rom/`, provenance in that directory's own
      `README.md`), address formulas ported from MAME's
      `src/mame/luxor/abc80_v.cpp` into `abc80/emu/src/video_timing.c`/`.h`.
      Verified programmatically against the real ROM bytes via
      `bin/abc80-video-timing-dump` (5/5 checks pass), not just narrative
      claims: the hsync PROM's `ROW_START` bit is set for exactly 40
      columns (`sx=15..54`); the vsync PROM's `FRAME_END` bit fires 23
      times, every 10 lines, giving 24 character rows; the line PROM cycles
      `0..9` once per character row, feeding `chargen.c`'s row parameter
      directly; the attr PROM's `BLANK` bit is clear in the border region
      and set for real characters in the active display area (an initial
      "looks inverted" read turned out to be checking the wrong half of the
      attribute address — resolved once `dh`/`dv`'s real meaning, "active
      display area" rather than a rare special case, became clear: 240/313
      scanlines and 40/64 column-slots have it set); and
      `abc80_videoram_addr()` (video RAM address from character row/column)
      is a genuine bijection across all 24×40=960 real character cells —
      zero address collisions, confirmed by brute-force enumeration.
      Deliberately not yet ported: the TEXT/GRAPHICS row-attribute state
      machine and the actual per-pixel chargen-vs-block-graphics selection
      loop, since both are inherently part of the rendering loop rather
      than a stateless PROM-decode primitive — left for the next step.
- [x] **Pick and wire up a display backend** — terminal-based, chosen over
      a windowed framebuffer for cheapness and consistency with how
      `bin/z80` already does console I/O (`cpm/emu/src/cpm.c`'s
      `console_emit()`/CP437-to-UTF-8 precedent). Implemented in
      `abc80/emu/src/render.c`/`.h`, deliberately simpler than a real
      pixel framebuffer: rather than stepping through individual scanlines
      the way MAME's `draw_scanline()`/`draw_character()` do, it renders
      one whole Unicode glyph per character cell directly — correct and
      much simpler for a terminal, which can't address individual pixels
      anyway. This finally ports the piece deliberately deferred from the
      PROM-decoding step above: the TEXT/GRAPHICS row-attribute state
      machine (`abc80/emu/src/render.c`, ported verbatim from
      `draw_character()`'s `m_mode`/`versal` logic). Two new supporting
      modules:
      - `abc80/emu/src/charset.c`/`.h` — TEXT-mode character-to-Unicode
        mapping. Not assumed from the Swedish ISO646 standard alone: all
        nine candidate punctuation positions (`0x40`, `0x5B`-`0x5E`,
        `0x60`, `0x7B`-`0x7E`) were individually decoded via
        `bin/abc80-chargen-dump` and confirmed to render as
        É Ä Ö Å Ü é ä ö å ü exactly, not guessed from the standard's
        published table.
      - GRAPHICS-mode block characters map onto Unicode's "Symbols for
        Legacy Computing" sextant block (U+1FB00-U+1FB3B) — ABC80's real
        2×3 block-mosaic bit layout (videoram bits `{0,2,4}`/`{1,3,6}`)
        matches that Unicode block's own 1-6 cell numbering exactly. The
        codepoint formula wasn't assumed from the bit pattern either — it
        was derived and cross-checked against Unicode's own published
        U+1FB00 table (compart.com's block listing) at both ends of the
        range and two interior points before being trusted.
      - Verified end-to-end with known-good synthetic input before ever
        trusting it against real CPU output: `bin/abc80-render-demo`
        writes known text (including all nine Swedish characters and a
        hand-picked block-graphics byte) into a synthetic video RAM buffer
        and renders it — output matched every prediction exactly: plain
        text, `ÉÄÖÅÜéäöåü`, a working reverse-video cursor, and a correct
        `▌` (LEFT HALF BLOCK) glyph from the graphics-mode path.
- [x] **Regression check** — done, and more convincing than hoped: wired
      the renderer into `abc80/emu/src/main.c` (video RAM, `0x7C00`-
      `0x7FFF`, is directly addressable within the flat `ram` array real
      execution already writes through — no copy needed) for a one-shot
      end-of-run render. Running the real, unmodified ROM produces a
      screen showing **"ABC80"** — the machine's own real startup banner,
      written by real ROM code, decoded through this independently-derived
      pipeline — plus a live cursor on the following line, exactly the
      "grounded in real firmware behavior" bar Milestone 1 set, now
      confirmed visually rather than only via a PC trace.

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

- Video generation (Milestone 2) is done. No PIO/keyboard, cassette, sound,
  or ABCbus/floppy yet (Milestones 3-6) — the reason the emulator can show
  the ROM's real boot screen but still can't be typed at or reach a usable
  BASIC prompt.
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
