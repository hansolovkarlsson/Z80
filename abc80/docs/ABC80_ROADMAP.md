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

## Milestone 3: Z80 PIO + keyboard input — done

**Goal**: reach an actually interactive BASIC prompt — the first milestone
where a person can type something and see a response.

- [x] **Confirm the ROM's steady-state busy-loop is waiting on PIO/keyboard
      state** — done first, ahead of implementing anything, using this
      project's own `bin/z80dasm` on the real committed ROM images rather
      than assuming. The loop Milestone 1 observed (`PC≈0x02F7`) is exactly:
      ```
      L02F1: BIT 7,(IX+2)
             JR NZ,L0312
             IN A,(38h)      ; port 0x38, masked to 0x10 by the hardware's
                             ; 0x17 global address mask - this IS PIO Port A
             ADD A,A         ; shift bit 7 (strobe) into carry
             JR C,L0302      ; carry set -> a key is ready, go process it
             LD (IX+4),46h
             JR L02F1        ; else keep polling
      ```
      confirming with certainty (not inference) that this is a plain
      polling read of PIO Port A, not an interrupt wait or a video-timing
      dependency — so a real Z80 PIO interrupt/mode implementation isn't
      needed to unblock it, just this one port's read value being correct.
- [x] **Implement the Z80 PIO device behind ports `0x10`-`0x13`** (mirrored
      `0x14`-`0x17`, `abc80/emu/src/keyboard.c`/`.h`) — scoped to exactly
      what the disassembly above showed is needed: Port A's bit layout
      (bits 0-6 = key ASCII code, bit 7 = strobe), grounded against MAME's
      `abc80_state::pio_pa_r()`. Deliberately **not** the real hardware
      scan-matrix PROM (`abc80-keyboard.bin`, N82S141) — MAME's own
      `abc80_common()` machine config doesn't emulate that either; it wires
      a generic host-ASCII-keyboard device straight to `kbd_w()`, which
      does the identical byte-plus-strobe forwarding this file implements.
      Followed as a well-precedented simplification (literally what the
      most widely used ABC80 emulation does), not an invented shortcut.
      `abc80/emu/src/main.c` keeps every one of the 16 real hardware
      address aliases (`port & 0x17 == 0x10`) in sync each instruction,
      since `z80_io_in()`/`z80_io_out()` (`cpm/emu/src/z80.c`) are a plain
      flat array with no device/masking logic of their own by design.
- [x] **Map host keystrokes to the ABC80 input path** — non-blocking stdin
      polling (`select()`), plain ASCII passthrough (matching MAME's own
      simplification above) rather than the real Swedish scan-matrix
      layout — typing a literal Å/Ä/Ö from a host keyboard isn't wired up
      yet, a known gap, not a claim of completeness.
- [x] **Verified against real ROM execution, not just "it compiles"** —
      two genuine false starts along the way, both kept here rather than
      quietly fixed and forgotten, since each taught something real about
      the hardware:
      1. The strobe was first held for a fixed 64 instructions (long
         enough to outlast one poll-loop iteration, ~6 instructions),
         which turned out far too short - the ROM doesn't reach this poll
         loop at all until well after reset (RAM-size detection runs
         first). Bisecting with real runs (not guessing) found the actual
         threshold: ~500,000 instructions. A single Enter keystroke with a
         1,000,000-instruction hold (2x margin) worked - `PC` reached
         `0x0302`, the cursor moved, the splash cleared.
      2. Extending to a full typed line (`PRINT 1+1\r`) broke that same
         fixed-hold approach in the opposite direction: holding long
         enough to survive the *first* character's wait made it far too
         long for *later* characters, whose poll loop re-enters almost
         immediately - the ROM read the *same* still-asserted strobe
         several times over, silently repeating keystrokes and overflowing
         its input line (`ERR 11`, visibly corrupted screen text like
         `PPPPPPPP...RRRRRR...`). Switching to "clear the strobe the
         instant *any* `IN A,(n)` reads PIO Port A" (true edge-triggering)
         looked like the obvious fix but was *also* wrong, for a genuinely
         interesting reason: this ROM's key-read routine is itself a
         **debounce loop** requiring the strobe to stay asserted across
         *several consecutive polls* - `L0302`'s `DEC (IX+3)`/`DEC (IX+4)`
         counters - before accepting a key, and one of those counters is
         normally refreshed by a **real periodic interrupt**
         (`RETI` at `0x0336`, resetting `(IX+4)` to `0x46`) that this
         emulator doesn't generate. Clearing on the very first poll cut
         that debounce off before it could ever converge, so the key was
         never accepted at all. Fixed by clearing the strobe only at the
         *specific* address the disassembly shows the debounce actually
         converging at (`PC == 0x0316`: `IN A,(38h); AND 7Fh; RES
         7,(HL); ...`), not on generic port-match detection - letting the
         debounce genuinely run its course.
- [x] **Regression check: real, typed BASIC input produces correct
      output** — done, and it's the exact example this checklist item
      named: piping `PRINT 1+1\r` to `bin/abc80` and letting it run
      produces a rendered screen showing
      ```
      ABC80
      PRINT 1+1
       2
      ```
      — the real ROM echoing the typed command exactly as typed, then
      evaluating `1+1` and printing the correct result, on the very next
      line, exactly the ABC80 equivalent of Phase 3's Tasty Basic
      real-software validation milestone in `cpm/docs/ROADMAP.md`. All ten
      characters were read correctly, in order, with no repeats or drops
      (confirmed via instruction-level tracing during development, not
      just eyeballing the final screen). Execution reached `PC` addresses
      up to `0x376A` - deep into the fourth ROM chip, code never
      previously exercised by this project - and visited 1,074 distinct
      addresses, up from 191 with no input. A no-input control run over
      the same instruction count stayed byte-for-byte identical to the
      original pre-keyboard baseline, confirming this is a real,
      keyboard-driven effect.

## Milestone 4: cassette storage (SAVE/LOAD) — done

**Goal**: load and save real BASIC programs, the ABC80's standard storage
medium (no disk drive without an ABCbus expansion card — Milestone 6).

**Scope decision, made deliberately rather than defaulting to the original
plan**: real ABC80 cassette I/O is analog — MAME's own `abc80_state::
cassette_update()` samples actual audio waveforms at 44.1kHz to decode tape
input (Kansas-City-style FSK), which would need real tape-encoding research
to emulate faithfully, well beyond what a PIO Port B implementation alone
gets you. MAME itself doesn't require that for everyday use: alongside its
real (incomplete/best-effort) cassette device, it wires up a
`QUICKLOAD_LOAD_MEMBER` (`abc80_state::quickload_cb`) that bypasses the
analog path entirely — read BASIC's own program-storage pointer, inject a
file's bytes directly into RAM there, fix up the end-pointer. This
milestone ports that exact mechanism (`abc80/emu/src/cassette.c`/`.h`, new
`--quickload FILE`/`--quicksave FILE` flags on `bin/abc80`) rather than the
originally-planned "cassette read/write via the Z80 PIO" — the same
well-precedented kind of simplification already used for the keyboard
(Milestone 3 bypassed the real scan-matrix PROM the identical way, since
MAME does too).

- [x] **The injection mechanism**: `BOFA`/`EOFA`/`HEAD` (`0xFE1C`/`0xFE1E`/
      `0xFE20`) are BASIC's own program-storage pointers, addresses ported
      directly from MAME's `abc80.h` (`BOFA = 0xfe1c`, etc.) — "Beginning/
      End Of File Area". Verified empirically against this project's own
      real ROM run, not just trusted from MAME's naming: typing a numbered
      line (`10 PRINT 1+1`) moved `EOFA` forward by exactly the stored
      line's length (19 bytes), and dumping that memory range showed
      genuine, well-formed BASIC tokens — a length byte matching the
      line's own size, a little-endian line number, a `PRINT` token, a
      repeated 6-byte numeric-literal encoding for each `1`, a `+` operator
      token, ending in the same `0x0D` line-input uses.
- [x] **A host-file-backed "cassette" image** — `abc80/resources/rom/`-
      adjacent `.bac`-style files, not a directory mapping like `bin/z80`'s
      `cpm_disk/` (there's no filesystem/directory concept on a real
      cassette, just one program per tape position, matching one file per
      quickload/quicksave here). One reserved header byte, matching MAME's
      own "skip the file's first byte" convention exactly, precedes the raw
      program bytes.
      **Honest caveat, not glossed over**: MAME's driver source doesn't
      explain what that header byte means, and this project could not find
      a definitive primary source for it despite real effort (abc80.net's
      archive, the TOSEC ABC80 software set, and the abc80.org mailing
      list were all checked - none had a byte-level answer). This
      implementation writes a fixed `0x00` placeholder there rather than
      guess at a real convention. Round-tripping through this emulator's
      own quicksave/quickload is fully verified (below); loading a real
      historical `.bac` file downloaded from an archive is untested and
      may need that header byte's real meaning figured out first if it
      turns out to matter.
- [x] **Verified end-to-end, including a real bug caught by testing, not
      assumed correct from the mechanism alone**: typing `10 PRINT 1+1` then
      quicksaving produced a 20-byte file; quickloading it into a *fresh*
      run (no typed program at all) and typing `LIST` correctly
      de-tokenized it back to `10 PRINT 1+1` on screen — proving the core
      byte-capture/injection is genuinely correct. But the first version
      tried, capturing exactly `[BOFA, EOFA)`, *looked* complete (`LIST`
      already worked) while `RUN` silently did nothing after a quickload,
      despite working perfectly when the same program was typed directly.
      Found by direct comparison, not guesswork: dumping the byte
      immediately *at* `EOFA` after each path showed a real difference —
      `0x01` after typing a program normally, `0x00` (this emulator's RAM
      zero-init, untouched by the naive quickload) after a quickload. That
      byte is a terminator BASIC's `RUN` depends on but `LIST` apparently
      doesn't. Fixed by capturing `[BOFA, EOFA]` *inclusive* (one byte
      more) in quicksave, and having quickload set the new `EOFA` to point
      at that restored terminator rather than just past the raw data.
      With that fix, the full round-trip — type a program, quicksave,
      start a genuinely fresh run, quickload, `RUN` — now produces the
      exact same output (`2`) as typing and running the program directly
      in one session, execution reaching the same deep ROM addresses
      (`0x376A`) either way.

## Milestone 5: SN76477 sound — done (scoped)

**Goal**: port `0x06` produces audible output matching the real complex
sound generator chip. Lower priority than 2-4 — cosmetic, not needed to
reach a usable interactive machine.

**No live audio in this environment**: unlike video (renderable as
terminal text) and keyboard (drivable via piped stdin), there's no way to
play or hear real-time audio here. `bin/abc80`'s `--wav FILE` flag renders
the SN76477 register's activity to a WAV file instead — the practical,
independently-verifiable deliverable, checked via zero-crossing frequency
analysis rather than by ear.

**Scoped deliberately, not attempted in full**: real SN76477 emulation
(MAME's own `src/devices/sound/sn76477.cpp`) is a genuine per-sample
analog simulation of four interacting RC-timed subsystems (VCO, SLF noise-
warble oscillator, noise generator, envelope/one-shot generator) — a large
undertaking for the roadmap's own lowest-priority, purely cosmetic
milestone. This implementation (`abc80/emu/src/sound.c`/`.h`) synthesizes
real audio for exactly one case: a steady tone at a fixed pitch (mixer
selecting VCO alone, envelope in "Mixer Only" continuous mode, VCO pitch
not SLF-swept) — the single most directly useful case for a BASIC-driven
beep, and the case this project's own real-ROM test below happened to
exercise. Every other register combination (noise, SLF, one-shot attack/
decay envelopes, alternating polarity, SLF-swept warble) produces silence
in this model rather than incorrect audio - documented as a known,
deliberate gap, not silently approximated.

- [x] **The register bit layout** — ported from MAME's `abc80_state::
      csg_w()`, and the mixer/envelope mode meanings from `sn76477.cpp`'s
      own `mixer_a_w`/`mixer_b_w`/`mixer_c_w`/`envelope_1_w`/
      `envelope_2_w` bit-packing and `log_mixer_mode()`/
      `log_envelope_mode()`'s mode-name tables — not guessed from the pin
      names alone. See `sound.c`'s own top comment for the full bit-by-bit
      derivation.
- [x] **The VCO frequency** — grounded against ABC80's real board
      component values (`R=100kΩ`, `C=10nF`, from MAME's own
      `machine_config`: `set_vco_params(0, CAP_N(10), RES_K(100))`), not
      an arbitrary/pleasant-sounding pitch. Derived by hand from MAME's own
      general analog-simulation formula
      (`compute_vco_cap_charging_discharging_rate()`), specialized for
      this board's actual fixed 0V control-voltage case (where the duty
      cycle is exactly 50%, so the general per-sample simulation reduces
      to a clean closed form): **f = 0.64 / (R × C) = 640 Hz**.
- [x] **Verified twice, isolated module first** — the same "prove it
      against known input before trusting real CPU output" discipline this
      project has used since Milestone 2's `render_demo.c`:
      1. `bin/abc80-sound-demo` (new tool) feeds a known synthetic
         register-event sequence (silence → tone → silence) and renders a
         WAV. Zero-crossing frequency analysis on the tone segment measured
         **639.39 Hz against the 640.00 Hz prediction** (0.1% error, well
         within sampling-quantization tolerance) — and the silent segments
         measured exactly 0 RMS, confirming the gating logic too.
      2. Wired into `main.c`'s real execution loop, detecting writes to
         port `0x06` (masked the same way as the PIO — `video_timing.c`'s
         port-map comment). Found and fixed a real gap while wiring this
         up, not assumed correct: the first version only recognized the
         `OUT (n),A` immediate-port opcode (`0xD3`) — real ABC80 BASIC
         actually has a working `OUT port,value` statement (confirmed by
         typing `OUT 6,64` at a real prompt and seeing no syntax error),
         but its *compiled* code uses the register-indirect `OUT (C),r`
         form instead (port from `BC`, value from whichever register the
         opcode names — traced via this project's own disassembly to
         `OUT (C),L`), since a general two-expression statement can't
         assume its port argument is a compile-time constant the way
         hand-written `OUT (n),A` assembly can. Fixed by decoding both
         real opcode forms generically (not hardcoded to the one traced
         instance). With that fix, typing `OUT 6,64` at a real prompt and
         letting the ROM run produced a genuine WAV tone starting a few
         seconds in (matching real BASIC command-processing time) at
         **639.95 Hz measured against the same 640.00 Hz prediction**
         (0.008% error) — real, unmodified ABC80 BASIC driving this
         project's own from-scratch sound model to the theoretically
         correct frequency.

## Milestone 6: ABCbus expansion

**Goal**: RAM expansion to the real machine's full 32KB, and real disk
storage.

- [x] **RAM expansion — done.** Replaces Milestone 1's flat-RAM
      simplification for `0x4000`-`0xBFFF` with genuine floating-bus
      behavior by default, plus an opt-in `--ram32k` flag modeling a real,
      well-documented 16KB expansion.
- [ ] **Floppy/DOS controller card + ABC-DOS or UFD-DOS ROM loading —
      scoping and protocol research in progress, not yet implemented.**
      See its own section below.

### RAM expansion sub-step (grounded, not guessed)

**The bug this fixes**: Milestone 1 modeled the entire ABCbus-delegated
range (`0x4000`-`0xBFFF`, minus video RAM) as ordinary flat, always-writable
RAM — a documented simplification at the time, but one that made the
emulator behave like a real 32K-expanded machine by accident, unconditionally,
even though nothing had actually implemented the expansion. Confirmed via
this milestone's own before/after measurement (below): the un-fixed
emulator's BASIC RAM-detection loop settled on `BOFA = 0x8000`, the real
*expanded*-machine value, on every run.

**What real hardware does without a card**: MAME's own `abcbus_slot_device`
forwards every read in this range to the attached card's `abcbus_xmemfl()`;
its default implementation (no card attached) is `return 0xff;`
unconditionally — a fixed floating-bus value, not "whatever was last
written" — confirmed directly from MAME's `abcbus.h`
(`device_abcbus_card_interface::abcbus_xmemfl()`/
`abcbus_slot_device::xmemfl_r()`). This project now matches that exactly:
`cpu.bus_read_hook` (see below) forces every read in the unpopulated part of
the range to `0xFF` regardless of the underlying array's contents. Writes
are deliberately left un-intercepted — since reads are already forced,
letting a write land harmlessly in the backing array needs no matching
write-side hook.

**The real RAM-expansion mod being modeled** (`--ram32k`): Christer Ekman,
"Bygg ut din ABC 80 till 32K RAM," *Mikrodatorn* nr 7, 1982 — a primary
source (the original magazine article, retrieved and read page-by-page from
abc80.net's archive, not summarized secondhand), and the same "Mikrodatorn"
RAM expansion MAME's own driver TODO list names but has never implemented.
Two banks of eight 4116 DRAM chips are piggybacked onto the machine's
existing eight, with an added OR gate fooling the address decoder into
starting the RAS/CAS generator across `0x8000`-`0xFFFF` (32K-64K) instead of
only `0xC000`-`0xFFFF` (48K-64K), and two more OR gates steering the `CAS`
signal so the *new* bank answers `0x8000`-`0xBFFF` while the *original* bank
keeps `0xC000`-`0xFFFF`. Not a separate ABCbus expansion card physically,
but the natural place to model it here regardless, since it occupies
exactly the address range Milestone 1's flat-RAM simplification left too
permissive. `0x4000`-`0x7BFF` is unaffected by this specific mod either
way — real DOS/printer/IEC ROM cards live there instead (default base
address 24K/`0x6000`, confirmed from a second real primary source,
`ABC80-minneskort-bruksanvisning.pdf`), a separate, not-yet-modeled part of
this milestone.

**Implementation**: a new optional `Z80.bus_read_hook` function pointer
(`cpm/emu/src/z80.h`/`z80.c`) — `NULL` by default (its zero-initialized
value on every existing caller, including the entire CP/M target), checked
by `z80_read_byte()` after it reads the flat array. `abc80/emu/src/main.c`
installs `abc80_bus_read_hook()`, which forces `0xFF` for `0x4000`-`0x7BFF`
always, and for `0x8000`-`0xBFFF` unless `--ram32k` was given. This is a
small, deliberately narrow addition to the shared, ZEXALL/ZEXDOC-proven
core — not a speculative abstraction: `z80_read_byte`/`z80_write_byte` were
already documented in `CLAUDE.md` as "a bus abstraction ... currently just
index straight into that array (no bank switching/MMU)," precisely
anticipating this kind of extension for a second machine target, the same
reasoning that justified the `z80_execute()`/`z80_step()` split in
Milestone 1. `make test` (ZEXALL/ZEXDOC/`test_interrupts`/every `.asm`
example) still passes with zero new warnings after the change, confirming
it's behavior-preserving for CP/M with the hook left unset.

**Verified against the magazine article's own worked example, not just
internal consistency**: the article itself proves the mod is wired in
correctly by reading `BOFA` (`PEEK(65052)+PEEK(65053)*256`) — `49152`
(`0xC000`) on the base 16K machine, `32768` (`0x8000`) once expanded. This
project's own `bin/abc80` run summary now prints the identical value (read
from `ram[ABC80_BOFA_ADDR]`, the same address BASIC's own boot-time
RAM-size detection loop sets — see `cassette.h`), and it matches exactly in
both configurations:

| Config | Measured `BOFA` | Magazine article's real-hardware value |
|---|---|---|
| default (base 16K) | `0xC000` | `0xC000` (49152) |
| `--ram32k` | `0x8000` | `0x8000` (32768) |

Settling is fast in both cases (measured directly, by re-running with
successively larger instruction caps and reading `BOFA` from each run's
summary): the base-16K detection loop now settles within ~1,000
instructions (down from the un-fixed version's ~10,000 — it now hits a real
floating-bus `0xFF` read immediately above `0xC000` instead of continuing to
probe further down through fake, accidentally-writable RAM), and the
`--ram32k` loop settles within ~1,500. `QUICKLOAD_INJECT_AFTER_INSTRUCTIONS`
(50,000) comfortably covers both.

**Full functional regression, not just the BOFA number**: re-ran
Milestone 3's keyboard flow and Milestone 4's cassette round-trip against
the corrected default. Typing `10 PRINT 1+1` then `LIST` still
de-tokenizes correctly; a fresh quicksave → quickload → `LIST`/`RUN` cycle
still reproduces the exact same output (`2`) as typing the program
directly, both under the corrected default `0xC000` config and under
`--ram32k`. No regression from moving `BOFA` off its old, accidental
`0x8000` value.

## Milestone 7: periodic PIO interrupt (timer model) — done

**Goal**: implement the real periodic interrupt this ROM's boot init
configures (`IM 2`, a Z80 PIO Port A interrupt vector) but this emulator
never delivered, retiring the "No periodic interrupt / timer model" Known
Gap below with the real hardware mechanism rather than leaving it as a
permanent, growing pile of PC-address workarounds.

**Grounded, not guessed** — three independent sources cross-checked before
writing any code:

- **MAME's own driver** (`src/mame/luxor/abc80.cpp`): `scanline_tick()`
  (a `TIMER_CALLBACK_MEMBER`, scheduled once per scanline via
  `m_scanline_timer->adjust(..., m_screen->scan_period())` in
  `abc80_v.cpp`) toggles `m_pio_astb` and calls `m_pio->strobe_a(...)`
  every call — the real Z80 PIO's Port A *strobe* input pin, normally a
  keyboard handshake line, repurposed as a free-running clock. Screen
  timing constants (`abc80.h`: `ABC80_HTOTAL=384`, pixel clock
  `XTAL(11'980'800)/2` = 5,990,400Hz) give a 15,600Hz line rate and,
  against the real 2,995,200Hz CPU clock, exactly 192 T-states/scanline —
  a clean whole number, not a coincidence of the crystal choice.
- **MAME's Z80 PIO device** (`src/devices/machine/z80pio.cpp`): Port A
  defaults to `MODE_INPUT` on reset (never overridden by this ROM — its
  own control-port writes, found below, only touch the vector/interrupt-
  control/mask registers). In `MODE_INPUT`, `strobe()`'s rising-edge case
  unconditionally calls `trigger_interrupt()` — so only every *other*
  `scanline_tick()` call (the toggle's rising half) is a real interrupt
  event: 384 T-states apart, a 7800Hz rate.
- **This ROM's own disassembly** (`bin/z80dasm` on the real, committed
  BASIC ROM, not the Perl-generated MAME source): boot init
  (`0x0068`-`0x00C5`) sets `IM 2`, `I=0` (`LD I,A` with `A=0` at `0x008C`),
  and writes exactly one Z80 PIO Port A control sequence via
  `OUT (39h),A` — `0x39 & 0x17 == 0x11`, Port A control under the same
  `0x17` hardware address mask the keyboard/sound ports use — three bytes:
  `0x34` (interrupt vector — any control-port byte with bit 0 clear loads
  the vector register, confirmed from `z80pio.cpp`'s own `control_write()`
  regardless of mode), `0xB7` (interrupt control word: enable=1,
  mask-follows=1), `0x7F` (the mask byte mask-follows requires next). With
  `I=0` and vector `0x34`, the real IM2 table entry sits at `0x0034`; this
  ROM's own bytes there (`0x1E 0x03`, little-endian) point to `0x031E` — a
  genuine interrupt handler, confirmed by its own `RETI` at `0x0336`. It
  reads Port A directly, checks for a Ctrl-C-style break combo (`0x83`,
  latching a break flag and beeping the SN76477 if matched), and —
  unconditionally, regardless of that check — reloads a fixed value
  (`0x46`) into `0xFDF7` (`IX+4` in the keyboard poll loop's own
  `IX=0xFDF3` base, confirmed at `0x02A5`): exactly the debounce-counter
  refresh Milestone 3's own writeup already inferred from indirect
  evidence, now confirmed directly.

**Implementation**: a periodic scheduler in `abc80/emu/src/main.c`'s main
loop calls `z80_request_int(&cpu, 0x34)` every 384 T-states of accumulated
CPU time (`total_cycles`), unconditionally from power-on — matching real
hardware, since `z80_request_int()` is level-held (`cpu.int_pending` stays
set until serviced) and the ROM's own `IFF1` naturally gates delivery until
its `EI`. No changes to the shared, ZEXALL/ZEXDOC-proven core beyond what
already existed — `z80_request_int()`/IM2 vectoring were already built and
tested for CP/M's own Known Gaps list.

**Two real bugs found by this project's own regression testing, not
assumed correct from the mechanism alone**:

1. **Interrupt interception silently broke the existing keyboard/sound
   `pc_before`-prediction hooks.** `main.c` already inspected
   `ram[pc_before]` *before* calling `z80_execute()` to predict upcoming
   keyboard-consumption (`PC==0x0316`) and sound-register writes (for
   Milestones 3/5) — a pattern that implicitly assumed `z80_execute()`
   always executes the instruction at `pc_before`. That stopped being true
   the moment interrupts could intercept a step instead (`z80_execute()`
   services a pending interrupt *without* fetching the predicted opcode,
   deferring it to a later call). Caught concretely: typing
   `10 PRINT 1+1` after enabling the interrupt dropped the `N` and produced
   a real `ERR 11`, from a spurious `abc80_keyboard_consumed()` firing on a
   step that predicted `PC==0x0316` but actually serviced an interrupt
   instead, releasing the next host keystroke too early. Fixed by computing
   `interrupt_will_intercept_this_step` (mirroring `z80_execute()`'s own
   acceptance check exactly) *before* the predictions and gating both on it.
2. **`--quickload`'s timing needed fixing twice.** The original fixed
   50,000-instruction wait assumed keyboard consumption couldn't happen
   before ~500,000 instructions (Milestone 3's own idle-boot measurement).
   With real interrupts, this ROM's interrupt handler can pre-latch an
   already-pending keystroke into a fixed RAM cell (`0xFDF5`) the very
   first time it runs, letting a key typed at the very start of a run get
   consumed thousands of instructions earlier — a fresh run piping
   `LIST\rRUN\r` right after `--quickload` now consumed and ran both
   commands *before* the file was ever injected, against BASIC's still-
   empty program. The first fix attempt — inject the instant `BOFA` first
   reads non-zero, providably race-free against keyboard timing — was
   *still* wrong, for a second, unrelated reason the same regression test
   caught: shortly after `EI`, this ROM unconditionally re-initializes the
   program to empty (`0x0A79`-`0x0A9B`: `EOFA := BOFA`, a terminator byte
   written at `BOFA` itself, `HEAD := EOFA+1`) inside a wait loop
   (`0x00CC: BIT 5,(IY+15) / JR NZ,L00C6`) that re-runs it an unpredictable
   number of times — injecting right after `BOFA` settles let this later
   reset silently clobber the freshly quickloaded program before anything
   ever read it. Fixed for real by injecting at `0x02AA` instead — the
   entry point of the ROM's own line-reading routine, called exactly once
   right after the "ABC80" banner prints and the sole call chain leading
   into the keyboard poll loop, so the ordering (reset-loop-already-done,
   input-not-yet-possible) is structural, not timed, with no margin to
   re-verify if boot timing ever changes again.

**Verified end-to-end**: distinct PCs visited on a plain boot run rose
from 191 to 201 — the ~10 instructions of the real ISR (`0x031E`-`0x0336`),
now genuinely executed for the first time, not just present in ROM. Full
`make test` still passes (hook-free for CP/M, unaffected). Re-ran every
prior milestone's own regression check against the interrupt-enabled
build: typing a program then `LIST`/`RUN`, the full quicksave → fresh run
→ quickload → `LIST`/`RUN` round-trip (base 16K and `--ram32k`), and the
sound WAV render (frequency re-measured at 640.01Hz via zero-crossing
analysis, still matching the 640.00Hz prediction) — all pass with no
regressions from before this milestone.

### Floppy/DOS controller sub-step — protocol research (in progress, no code yet)

**Why this is scoped differently from every other sub-step here**: a real
ABC830/832/838-class controller (the family the `abc80_cards` slot list's
"abc830"/"fd2" options represent, per MAME's `src/devices/bus/abcbus/
lux21046.h`) is not a simple memory-mapped FDC register set — it's a whole
second, autonomous computer, with its own Z80 CPU, its own Z80 DMA
controller, and its own FD1793/WD1771-class floppy disk controller chip,
all running the card's own separate firmware
(`abc80/resources/rom/FIO-V3.2.bin`, confirmed as "PROM on FIO board 2708"
per abc80.net's own index — the FD2/FD2U technical manual's own
`FLOPPY DISKENS DATOR` section independently confirms this: "Datorn
administreras av en Zilog Z80:CPU... En Zilog Z80:PIO används som en
buffer mellan det interna datorsystemet och floppydiskelektroniken" -
"[the controller's internal] computer is managed by a Zilog Z80 CPU... a
Zilog Z80 PIO is used as a buffer between the internal computer system and
the floppy disk electronics"). Faithfully emulating that hardware would
mean building two entirely new peripheral-chip emulators (a Z80 DMA
controller and an FD1793-class FDC, neither of which this project has ever
needed before) plus a second CPU instance, before the actual DOS ROM
(which runs on the *host* CPU, not the controller's) could do anything at
all — a different scale of undertaking than any milestone so far.

**Deliberate scope decision**: bypass the controller's internal hardware
entirely (mirroring Milestone 4's cassette quickload precedent — skip the
real, complex device and intercept at the boundary real software already
crosses) rather than build it. Concretely: intercept the *byte-level
command protocol* the ABC80 host CPU exchanges with the controller card
over the ABCbus, and answer it directly from a host-file-backed virtual
disk image — the controller's own internal Z80/DMA/FDC never needs to
exist in this emulator if the bytes it would have produced on the bus are
faithfully reproduced. This is a bigger bypass than cassette's (which just
copies bytes at a known RAM address) since it requires understanding a
real, undocumented-by-MAME-comments protocol — grounded by disassembling
the actual ABC-DOS ROM, exactly the same methodology used for every other
subsystem in this project.

**Confirmed via direct disassembly of `ABCDOS80.bin`** (the real,
committed, checksum-recorded "ABC-DOS for ABC 80" ROM — see
`abc80/resources/rom/README.md` — disassembled with `bin/z80dasm ... -o
0x6000`, its real base address per `ABC80-minneskort-bruksanvisning.pdf`;
the load address is independently self-confirmed by the disassembly
holding together as coherent code across the full 4KB image, including
jump targets throughout `0x6000`-`0x6FFF`):

- **Card select**: `LD A,2Dh` (45 decimal) / `OUT (01h),A` — an exact
  match to MAME's own `ADDRESS_ABC830 = 45` constant
  (`src/devices/bus/abcbus/lux21046.h`) *and* to the FD2 technical
  manual's own worked example (`"KORTETS ADRESS ÄR 45... OUT 1,45"`) —
  three independent sources agreeing, not a guess.
- **Port roles**: port `0x00` = data byte transfer; port `0x01` = status
  (read) / card-select (write); port `0x02` = a control line, written once
  per command. Status bit 7 = card ready; bit 0 = per-byte ready during a
  transfer; bit 1 = a second ready gate checked before a transfer starts.
- **Command packet**: a real 4-byte structure, `[function_code,
  drive_number, D, E]`, sent byte-by-byte with a ready-bit handshake per
  byte (`L612B`/`L6143` in the ROM's own code, `0x612B`-`0x614C`). Drive
  number comes from a fixed RAM cell (`0xFD01`, masked to 3 bits — an
  0-7 drive select). `D`/`E` are caller-supplied and not yet decoded (see
  below).
- **Two concrete operations identified by function code and data
  direction**: function `3` (`L6068`/`L60A1` at `0x6068`, entered via the
  ROM's own jump table at `0x600F`) reads a run of bytes *from* the
  controller into a host buffer (`L614D` receive-loop); function `0x0C`
  (12) (`L609F`/`L60A1`, jump table `0x6012`) sends a run of bytes *to*
  the controller from a host buffer (`L60B4` send-loop) — almost
  certainly "read sector" and "write sector" respectively, given the
  data-direction match and that both are reached via the ROM's own public
  jump table (i.e., these are real, externally-callable DOS entry
  points, not internal helpers).

**Still open, not yet resolved — the real remaining work**:
- The exact meaning of the command packet's `D`/`E` bytes (track+sector
  vs. a linear block/sector number) — not yet traced back to a caller
  that supplies concrete values.
- The transfer-length encoding: routed through a lookup table addressed
  via a RAM pointer at `0xFD12`, referenced in well over a dozen places
  across the ROM (`0x61C0`, `0x61E8`, `0x620F`, `0x6253`, `0x6258`,
  `0x6326`, `0x637E`, `0x639A`, `0x63BE`, `0x63CD`, `0x6613`, `0x69CC`,
  `0x6C75`, `0x6C7E`, `0x6D42`, and more) — clearly central to the ROM's
  whole buffer/track model, not a one-off detail worth guessing at.
- The response/error convention (carry-flag-based on return, per
  `L60D7`/`L6068`'s own `SCF`/`RET`/`RET Z` pattern, but the specific
  error codes aren't pinned down).
- The disk image's own filesystem layout (directory format, sector
  layout) — needed before anything like `DIR` or a real `LOAD` against a
  virtual disk could work, not just raw sector I/O in isolation.
- `UFD80V20.bin` (the alternate real DOS variant, also committed) hasn't
  been examined at all yet — unknown whether it shares this same
  low-level protocol or differs.

## Milestone 8: `--interactive` — real keyboard input and a live screen — done

**Goal**: close the gap between "this emulator can execute real ABC80
firmware" and "this is usable as an actual interactive machine." Every
earlier milestone verified correctness by piping a whole scripted
keystroke sequence through stdin and inspecting one final end-of-run
screen snapshot — genuinely useful for regression testing, but not what
"take input from the keyboard" means for a real user sitting at a real
terminal.

**Two real gaps, both closed together, since neither alone would have been
usable**:

1. **No raw terminal mode.** `poll_stdin_byte()` already read stdin
   non-blockingly, but with a real interactive terminal left in its
   default (canonical) mode, keystrokes wouldn't reach the emulator until
   Enter was pressed, and the host terminal would echo them a second time
   on top of whatever the ROM itself draws. Fixed by
   `abc80_console_init()`/`abc80_console_shutdown()` — a direct port of
   `cpm/emu/src/cpm.c`'s own already-proven `cpm_console_init()` (same
   `ICANON`/`ECHO` clearing, same `ICRNL`/`INLCR`/`IGNCR`/`IXON` fixes for
   the identical real reasons documented there), gated behind `isatty()`
   exactly like the CP/M target's version. One deliberate divergence from
   a byte-for-byte port: `ISIG` is left *enabled*, so host Ctrl-C still
   raises `SIGINT` (this tool's own "quit cleanly" mechanism, consistent
   with how the CP/M target's console already behaves) rather than
   reaching the emulated ROM as a genuine `0x03` keystroke — a deliberate,
   documented scope boundary (see Known Gaps below), not an oversight.
2. **No live display.** `abc80_render_frame()` was only ever called once,
   after the entire run ended — meaning even with real per-keystroke
   input working, a user would see nothing on screen until they killed
   the process. Fixed with a periodic render inside the main loop itself,
   throttled to real time rather than checked every instruction (a
   `clock_gettime()`/potential `nanosleep()` pair every instruction would
   swamp actual emulation work): every `ABC80_PACING_CHECK_INTERVAL`
   (500) instructions, compare elapsed wall-clock time against elapsed
   *emulated* time (`total_cycles / ABC80_CLOCK_HZ`) and sleep off any
   difference, then redraw at most every `1/30` second. This closes a
   second problem for free: at full host speed, the existing 5,000,000-
   instruction default cap would complete in well under a real second,
   ending an "interactive" session before a human could ever react.
   `--interactive` instead removes the fixed cap entirely (runs until
   `SIGINT`) — real-time pacing already bounds how much wall-clock time
   (and CPU) a live session consumes, so a separate instruction limit
   would only get in the way.

**A genuine side effect, not a separate feature**: cursor blink is now
real. `blink_phase` was hardcoded to `1` (always on) for the old one-shot
snapshot; the live render loop instead computes it from real elapsed time
against `ABC80_BLINK_HZ` (3.125Hz — MAME's own `m_blink_timer`,
`attotime::from_hz(XTAL(11'980'800)/2/6/64/312/16)`, not guessed). This is
exactly the "Cursor blink still isn't live... revisit only if a future
milestone adds live/interactive rendering" gap Milestone 7 left open,
closed as a direct consequence of adding the live loop this needed anyway.

**Verified, not just "compiles and doesn't crash"**: this sandboxed
environment has no real controlling TTY to test raw-mode keystrokes
against directly, but every other mechanic was verified concretely —
- Real-time pacing: a 2-second `--interactive` run (killed via `SIGINT`
  after a measured wall-clock delay) accumulated 5,998,571 T-states —
  against a predicted 5,990,400 (2 real seconds × `ABC80_CLOCK_HZ`), a
  0.14% difference, confirming the emulator is genuinely throttled to real
  ABC80 speed rather than running at full host speed.
- Live rendering: the same run's output contained 59 distinct
  clear-screen sequences, matching the expected ~60 redraws for 2 seconds
  at the 30Hz render throttle.
- Keyboard input still works end-to-end through the new paced loop (not
  just the old batch loop): piping `10 PRINT 1+1` then `LIST` through
  `--interactive` (with `sleep`s between chunks to simulate real typing
  pace) produced the identical correct output as the existing non-
  interactive regression check.
- Clean shutdown: `SIGINT` during a live run exits with status 0, prints
  the same final render + run summary as a normal batch-mode end (now
  printed in the opposite order - render first, then summary - so the
  summary text a real user is trying to read doesn't get wiped by the
  render's own clear-screen escape code), and correctly reports "user
  requested exit (Ctrl-C)" as the stop reason.
- Full regression: every existing default-mode (non-`--interactive`) test
  in this milestone's own history re-verified byte-for-byte identical
  after this change (boot trace, typed-program `LIST`/`RUN`, full
  quicksave/quickload round-trip) - `--interactive` is strictly additive,
  gated behind its own flag throughout.

**Known gap, deliberately not solved here**: with `ISIG` left enabled,
there is currently no way to send a real Ctrl-C *break* keystroke through
to BASIC via `--interactive` - host Ctrl-C always quits this tool instead.
Solving that would need either disabling `ISIG` (and then inventing some
other, non-colliding host-level "quit the emulator" mechanism, since
Ctrl-C would no longer be available for that) or a distinct escape
sequence, and wasn't part of what this milestone set out to fix - see
Known Gaps below.

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
| `0x4000`-`0xBFFF` | External ABCbus expansion, minus the video RAM carve-out below. `0x4000`-`0x7BFF`: always floating (`0xFF`) — no DOS/printer/IEC ROM card modeled yet. `0x8000`-`0xBFFF`: floating (`0xFF`) by default, or real RAM with `--ram32k` (the real 16KB expansion mod — see Milestone 6). |
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

- Video generation (Milestone 2), keyboard input (Milestone 3), cassette
  quickload/quicksave (Milestone 4), a scoped SN76477 tone model
  (Milestone 5), RAM expansion / floating-bus fidelity (Milestone 6's first
  sub-step), the real periodic PIO interrupt (Milestone 7), and real
  interactive keyboard input with a live, real-time-paced screen
  (Milestone 8, `bin/abc80 --interactive`) are done. No floppy/DOS
  controller yet (Milestone 6's remaining second half) —
  real protocol facts have been derived by disassembling the actual ABC-DOS
  ROM (card-select address, port roles, command packet format, two
  identified operations), but the parameter encoding, transfer-length
  table, and disk filesystem layout are still open; see that sub-step's
  own write-up above before resuming this.
  Cassette storage is a host-file bypass of BASIC's own program-storage
  pointers, not real analog tape emulation, and sound only synthesizes a
  single steady-tone case (no noise/SLF-warble/envelope shaping) rendered
  to a WAV file rather than played live - see each milestone's own
  write-up.
- **Cursor blink is now live** (Milestone 8) in `--interactive` mode,
  computed from real elapsed time against the real 3.125Hz rate MAME's own
  `m_blink_timer` uses. Default (non-`--interactive`) mode is still a
  one-shot end-of-run snapshot with `blink_phase=1` hardcoded, unchanged -
  a deliberate difference between the two modes' purposes, not a gap.
- **No real Ctrl-C break through `--interactive`** (Milestone 8): host
  Ctrl-C quits this emulator tool (`ISIG` deliberately left enabled,
  mirroring the CP/M target's own console handling) rather than being
  delivered to the emulated ROM as a genuine `0x03` keystroke, so BASIC's
  own Ctrl-C-break handling (the `0x83` check in the real interrupt
  handler at `0x031E` - see Milestone 7) can't currently be exercised
  interactively. Would need either disabling `ISIG` and inventing a
  different, non-colliding host-level quit mechanism, or a distinct escape
  sequence - not attempted here.
- **Memory-map fidelity for `0x4000`-`0xBFFF`**: fixed by Milestone 6's RAM
  expansion sub-step (see above) — `0x4000`-`0x7BFF` and, by default,
  `0x8000`-`0xBFFF` now correctly float (fixed `0xFF` reads, matching MAME's
  own no-card `abcbus_slot_device` behavior) instead of being ordinary flat
  RAM. `0x8000`-`0xBFFF` still has no real DOS/printer/IEC ROM card content
  even with `--ram32k` off — that's the still-open second half of
  Milestone 6, not this sub-step.
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
- Christer Ekman, "Bygg ut din ABC 80 till 32K RAM," *Mikrodatorn* nr 7,
  1982 — <https://www.abc80.net/archive/luxor/ABC80/ABC80-32K-mod-Mikrodatorn.pdf>
  (Milestone 6's RAM-expansion sub-step).
- *Bruksanvisning Minneskort ABC* (Luxor) —
  <https://www.abc80.net/archive/luxor/ABC80/ABC80-minneskort-bruksanvisning.pdf>
  (the separate ROM/DOS expansion card, Milestone 6).

See `abc80/docs/ABC80_REFERENCE.md` for a consolidated hardware reference
(memory map, I/O ports, ROM/PROM inventory, per-subsystem register layouts)
pulled from all of the above plus this project's own code comments — this
section only lists sources, not the facts derived from them.
