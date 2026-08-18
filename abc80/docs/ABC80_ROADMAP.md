# ABC80 Roadmap

A second machine target sharing this repo's proven, ZEXALL/ZEXDOC-clean Z80
core (`z80core/z80.c`/`alu.c`) rather than a from-scratch CPU — see
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

`z80_step()` (`z80core/z80.c`, pre-Milestone-1) unconditionally
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
      since `z80_io_in()`/`z80_io_out()` (`z80core/z80.c`) are a plain
      flat array with no device/masking logic of their own by design.
- [x] **Map host keystrokes to the ABC80 input path** — non-blocking stdin
      polling (`select()`), plain ASCII passthrough (matching MAME's own
      simplification above) rather than the real Swedish scan-matrix
      layout — typing a literal Å/Ä/Ö from a host keyboard wasn't wired up
      at this point, a known gap, not a claim of completeness. **Update**:
      closed by a later sub-step below ("Swedish character keyboard
      mapping") — not by building a real scan-matrix model (see that
      sub-step for why that framing was never actually the real gap), but
      by decoding the host's UTF-8 keystrokes for Å/Ä/Ö/Ü/É (and lowercase)
      into the correct ABC80 character-set byte.
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
in this model rather than incorrect audio - documented, at the time, as a
known, deliberate gap, not silently approximated.

**Update**: this deliberate gap was later closed. Milestone 11's "full
SN76477 emulation (SLF, noise, one-shot, envelopes)" sub-step below
implemented all of these remaining subsystems as real per-sample RC
integrators (ported from MAME's own `sn76477.cpp` algorithm), so every
register combination now produces real, verified audio rather than
silence — see that sub-step for the implementation and verification
detail.

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
- [x] **Floppy/DOS controller card + ABC-DOS ROM loading — done.** A
      `--disk` bypass (real DOS ROM boots and runs unmodified against it)
      supports genuine `SAVE`/`LOAD` round trips against real ABC80 disk
      images, verified across independent process runs. See its own
      section below for the full derivation, including several dead
      ends corrected along the way. `UFD-DOS` (the alternate ROM,
      `UFD80V20.bin`) remains unexamined - out of scope now that
      `ABC-DOS` itself works.

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
(`z80core/z80.h`/`z80.c`) — `NULL` by default (its zero-initialized
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

### Floppy/DOS controller sub-step — protocol research and a working `SAVE`/`LOAD` implementation — done

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

#### The device-dispatch mechanism (how BASIC's `LOAD`/`SAVE` actually reach floppy code)

Confirmed by tracing backward from the low-level READ/WRITE functions
above to find what actually calls them, rather than just assuming: the
base BASIC ROM has a real, general **device/extension dispatch table**,
not a single fixed vector.

- **`0xFE0A`** is the head of a linked chain of device-handler entries,
  each 7 bytes: `[next:2][name:3][handler:2]`. The base ROM's own boot
  init points it at its built-in cassette handler (`0x018C`); this is a
  genuine extension point, not a DOS-specific hack.
- The DOS card's own init routine (`L6D24` at `0x6D24`, reached from a
  card-presence probe in the base ROM's boot sequence — `LD A,(604Bh) /
  CP 0C3h / CALL Z,604Bh`, i.e. "if there's a real `JP` opcode sitting at
  the DOS ROM's known init address, the card is genuinely present, so
  call it") **prepends seven new entries to this exact same chain**,
  found by walking the raw bytes directly (linear disassembly
  misidentifies this region as code, since it's data — see
  `abc80_dasm.txt`'s own documented limitation): `DR0`, `DR1`, ...,
  `DR6` (drive numbers 0-6), all pointing at one shared handler,
  `0x6EE4` → `0x6DAA`. The chain's last entry continues on to the
  *original* `0xFE0A` value via a RAM indirection cell (`0xFD35`, set by
  the DOS init to the base ROM's old cassette-handler address) — meaning
  cassette (`CAS:`) support is preserved alongside the new floppy
  entries, not replaced.
- The common handler (`L6DAA`, `0x6DAA`) initializes a per-request,
  FCB-like structure (self-referencing linked-list sentinel fields,
  a stored drive number, fixed type/state bytes) — real, but this
  project doesn't need to understand it further, per the strategic
  decision below.

**Strategic decision this finding led to**: initially assumed a working
bypass would mean faithfully reproducing the real byte-level ABCbus
protocol (card-select, status-byte polling, per-byte handshaking)
underneath these functions. That's unnecessary. The existing
`--quickload` precedent already establishes the right pattern: intercept
execution at a specific PC address, do the real work directly in C, and
resume the emulated CPU as if the real routine had simply returned - no
different in kind from intercepting at `0x02AA` for quickload injection,
just at `0x6068`/`0x60A1` instead. This means none of the real ABCbus
protocol, the controller's own internal firmware, or even ABC-DOS's own
directory/FCB format need to be understood or reproduced at all - the
genuine, unmodified DOS ROM code (running normally on the emulated CPU)
already handles all of the file-management logic *above* these two
functions; a bypass only needs to answer "given this block number, hand
back/accept these bytes" correctly, and let the real ROM code do the rest
exactly as it already does today.

#### The `L6068`/`L60A1` calling convention (grounded, complete)

Traced instruction-by-instruction from `L606B`/`L60A4` (the low-level
READ/WRITE bodies), not assumed:

| Register | Role |
|---|---|
| `B` (in) | Bits 4-6 select a **channel** number (0-7) - which open file/drive this transfer belongs to. Other bits not yet decoded. |
| `D`,`E` (in) | The 16-bit block/record number - confirmed by tracing the actual bytes sent in the command packet, unmodified by the routine itself. |
| `C`,`HL` (in) | Pushed on entry and restored unchanged before return - **not** used as a destination buffer address, despite that being the natural first guess. |
| Destination buffer | Computed **internally**, not caller-supplied: `0xFD12` (a RAM cell, set to `0xF500` by DOS init) `+ (channel × 0x100)` - eight fixed, pre-allocated 256-byte buffers, one per channel. Copying from this fixed buffer to wherever BASIC actually wants the bytes happens at a higher level this project doesn't need to trace, per the strategic decision above. |
| `A`, Carry (out) | Success: Carry clear, `A=0`. Failure: Carry set (`SCF`), via one of a couple of distinct error paths in `L608F` - see the "closing the three remaining loose ends" sub-step further below for the full, now-characterized breakdown. |
| Transfer size | Confirmed, not just circumstantial: `L6106`'s own real disassembly shows `AND 04h` zeroing `A` (hence `B`) right before `LD B,A` on its normal-completion path - the `LD B,0`/`DJNZ` 256-iteration idiom is the ROM's own deliberate logic, not an inference from indirect evidence. |

**Resolved** (originally "Still open, not yet resolved" - all four items
below are now closed, each linked to where):
- `B`'s other bits (0-3, 7) confirmed genuinely unused - see the "closing
  the three remaining loose ends" sub-step further below.
- `L608F`'s failure paths fully characterized (still not reproduced in
  the emulator - a deliberate simplification, not an oversight) - same
  sub-step.
- 256-byte transfer size confirmed directly from the ROM's own
  disassembly (table row above), not just circumstantially.
- `UFD80V20.bin` examined (see this milestone's own dedicated sub-step
  below) - not wired up, since `ABC-DOS` itself already works and a
  second, more general driver isn't needed without a concrete reason.

#### First implementation attempt — mechanism verified against a real disk, blocked on a direct-mode restriction

Built `--disk FILE` (`abc80/emu/src/main.c`): loads the real, committed
`ABCDOS80.bin` at `0x6000` (carving that range out of Milestone 6's
otherwise-permanent floating-bus fill), opens/creates a flat host file as
the virtual disk, and implements `abc80_disk_trap()` - intercepting
`PC==0x6068`/`0x60A1` exactly as designed above, reading `B`/`D,E` for
channel/block, performing real `fseek`/`fread`/`fwrite` against the host
file at `block × 256`, and manually popping the return address to resume
execution, with carry/`A` set for success.

**What this confirmed actually works, verified by real execution, not
assumed**:
- The DOS-card-presence probe in the base ROM's own boot sequence
  (`LD A,(604Bh) / CP 0C3h / CALL Z,604Bh`) passes naturally, with no
  special-casing needed - the real loaded ROM bytes already satisfy it.
- The DOS init routine (`L6D24`) runs to completion with no hang: a
  200,000-instruction run shows PCs visited reaching `0x6D74` (inside
  `L6D24`'s own body) and returning cleanly to the base ROM's normal
  flow, with `BOFA` still correctly `0xC000` - RAM detection unaffected.
- The trap itself is mechanically correct: real reads and writes occur
  against the host disk-image file (confirmed both by tracing individual
  trap calls during development and by the image file growing to a
  plausible size), register/flag/return-address simulation works with no
  crashes, and every existing regression check (default mode,
  `--interactive`, `make test`) still passes unchanged - this feature is
  strictly additive, gated behind `--disk`.

**New blocker found, revising an earlier assumption**: the "strategic
decision" above assumed ABC-DOS's own directory/filesystem format would
never need to be understood, since the real ROM code would handle that
layer once the low-level block I/O is answered correctly. That's true for
the *mechanics* of the file-management code, but not quite the whole
story: with a freshly-created, all-zero disk image, `SAVE` doesn't reach a
working state at all. Tracing the actual trap calls during a real `SAVE`
attempt shows the ROM repeatedly scanning eight fixed candidate blocks
(`512, 544, 576, ..., 736` - each 32 blocks apart, all read via a helper
at `0x6978` called from a loop around `0x6827`-`0x6850`), finding nothing
recognizable in any of them (all zero), and looping back to try again -
consistent with a real "is a validly-formatted disk present" check that a
blank image can never satisfy, the same way a real, truly blank floppy
would need an actual format step before use. **Not yet resolved**: the
exact condition being checked at those eight locations, and what a
plausible/valid value would need to look like - the loop at `0x6827` was
only partially traced before pausing to write this up, not fully
understood.

#### A real disk image, not a blank one — new ground truth, new (different) blocker

abc80.net's own archive turns out to host real, dumped ABC80 floppy
images: `sw/disk_images/ABC80/160k/` alone has 49 of them, several with
scanned disk-label photos and short descriptions. Downloaded
`disk003.img` ("System.diskett ABC80 Ver. 2.1", per that directory's own
`index.txt`) to use as real ground truth instead of continuing to guess
at the blank-image blocker above.

**Confirmed empirically, not just circumstantially, against this real
image**:
- **256-byte blocks**: `disk003.img` is exactly `163840` bytes = `640 ×
  256` - a clean division against a real, known 160KB disk capacity, not
  a coincidence. The earlier `LD B,0`/`DJNZ`-idiom evidence now has a
  second, independent confirmation.
- **The real directory format**: readable directly out of the image's
  own bytes (no ROM disassembly needed for this part) - blocks 8 and 16
  (identical, presumably a redundant copy) hold real 11-byte filename
  entries (`BASICERR SYS`, `CMDINT   SYS`, `COPY     ABS`, `MAP     ABS`,
  etc. - classic 8+3 name/extension, space-padded, no separator byte),
  and block 0 holds a real volume label string, `"SYSTEM-DISKETT ABC-80
  Vers. 2.1."` - matching the disk's own physical label exactly.
- **The real Swedish DOS error-message table**: also directly readable
  from the image (stored as plain text inside one of the system files,
  not the ROM) - e.g. `SKIVAN FULL` (disk full), `SKIVAN EJ KLAR` (disk
  not ready), `HITTAR EJ FILEN` (file not found), `OTILLÅTET SOM
  KOMMANDO` (not allowed as \[direct\] command) - genuine primary-source
  text for what was previously an unresolved "Still open" item (exact
  error meanings).
- **The directory is *not* at blocks 512-736** - it's near the very
  start of the disk (blocks 8/16). This means the blank-image blocker
  documented above (an 8-location scan at blocks 512-736 that never finds
  anything) almost certainly wasn't scanning for the directory itself -
  more likely a disk-geometry/type auto-detection sequence (try one
  capacity/format, fall back to another) that a real, correctly-sized
  disk should satisfy at an earlier step than a blank image ever would.

**Testing `SAVE` against this real image confirms that reasoning**: the
emulator no longer gets stuck in the endless scan loop the blank image
produced - it now returns a concrete, numbered `ERR 41` instead. A
different, more diagnosable failure, and real evidence the real image
changes behavior exactly as expected.

**`ERR 41` traced to its actual cause, using the emulator itself rather
than static disassembly alone** (added temporary PC-history and register
tracing to `main.c`, since blind disassembly reading had diminishing
returns by this point - removed again once the trace was captured): the
full call chain runs through `0x6D76`/`0x6E23` (the *same* DOS-card init
tail used at boot - a genuinely shared/reusable utility, not a re-run of
boot itself) into a shared base-ROM utility at `0x062E`
(`LD (IY+14) checked; JR NZ,L0669` - "abort with the error code already
in `A` if this flag is set"). Register tracing at the exact decision
point shows `(IY+14) = 0xFE24 = 0x01` and `A = 0xA9` (masks to `0x29` =
41, confirming this really is the `ERR 41` path). `(IY+14)` turns out to
be a genuine, simple **direct-mode flag**: set to `1` unconditionally at
the top of the READY-prompt loop (`0x00DE`, i.e. "not currently running a
program"), and cleared to `0` specifically when `RUN` begins executing a
program (`0x0D5F`, right before entering the real statement-dispatch
loop `L0D6F`/`L0D90` this project's own Milestone 9 write-up already
found). So the concrete, mechanical cause of `ERR 41` is: **this specific
DOS code path is only valid while a program is running, not when typed
as a direct command** - matching `OTILLÅTET SOM KOMMANDO` ("not allowed
as \[direct\] command") from the real error table above, by inference
from context rather than a confirmed code-to-message mapping.

**Correction (this project's own earlier conclusion was wrong) - the real,
confirmed Swedish error-code table**: the "direct-mode flag" mechanism
above is real (verified by tracing `(IY+14)`'s own read/write sites), but
concluding it explains `ERR 41` was a mistake - `(IY+14)` gates whether an
error code found in a 16-byte table at `0x0659` gets silently retried/
resumed (jumping back into the statement-dispatch loop at `L0D6F`) instead
of aborting; when it's nonzero (direct-mode) *or* the error code isn't in
that table at all, `L062E` always falls straight through to the abort path
regardless of the flag - the flag only ever suppresses the abort, it never
causes one. The actual error text was still unconfirmed at the time, so
this was a plausible-sounding but unverified inference, not a checked
fact.

Extracting `disk003.img`'s own error-message table (blocks 18-31,
delimited by high-bit marker bytes - confirmed to be `0x80 + error code`
by cross-referencing several message boundaries against their neighbors)
gives the real, complete mapping instead of guessing from context. In
particular: **`ERR 41` is `0xA9 & 0x7F = 41` = `SKIVAN FULL` ("disk
full")** - nothing to do with direct-vs-running mode at all. And
`OTILLÅTET SOM KOMMANDO` ("not allowed as \[direct\] command"), the
message this project's earlier write-up guessed `ERR 41` mapped to, is
actually **`ERR 2`** (`0x82 & 0x7F = 2`), a completely different code. The
full table (all 64 messages, `0x80`-`0xBF`) is now captured in
`abc80/docs/ABC80_REFERENCE.md`'s own error-code section for future
reference rather than re-derived from scratch next time. `ERR 21` (see
below) is `0x95 & 0x7F = 21` = `HITTAR EJ FILEN` ("file not found") -
exactly the ordinary, expected error for a file that genuinely can't be
located, not a sign of anything exotic.

**A `LOAD` test against the same real image, and a new, more fundamental
finding**: rather than continuing to chase `SAVE`'s "disk full" report
against a disk that plainly isn't full, tried `LOAD DIRCOPY` (a real
`.BAC` - tokenized BASIC program, confirmed by extension per
`ABC80_BASIC_REFERENCE.md` - genuinely present in `disk003.img`'s own
directory, block 8/16). Decoded that entry's own two-byte start-position
field (`0x0FE0`) against the directory's raw bytes for all fourteen
files and found every value is a clean multiple of `0x20` - consistent
with `start_block = value >> 5` (32 real per-block "sub-block" units),
which lands every file at a distinct, monotonically increasing, in-range
block number (`DIRCOPY.BAC` → block 127) - a real, testable hypothesis,
not yet confirmed by an actual successful read.

It wasn't confirmed, because **the `LOAD` never reached the directory at
all**. Added the same kind of temporary trap-call tracing used for the
`SAVE` investigation (removed again afterward) and found every single
disk-trap call during the entire run - both for a bare `LOAD DIRCOPY` and
for `LOAD DR0:DIRCOPY` (guessing at a `DR0:`-style device prefix from the
`DR0`-`DR6` device-chain names this project's own earlier research found -
**not confirmed from any primary source**, since no manual excerpt
consulted so far documents the actual floppy-device syntax) - is the exact
same repeating 8-block scan (`512, 544, ..., 736`) already found during
the blank-image investigation, cycling continuously for the entire run
(80-192 reads observed, always the same 8 blocks, never blocks 8/16/127).
The directory-search code, and the `>>5` hypothesis above, were never
actually exercised - both `ERR 41` and `ERR 21` were reached without the
DOS ROM ever reading the real directory or file data at all.

This reframes the real blocker: it isn't a `SAVE`-specific or
`LOAD`-specific problem, and it isn't (as far as tested) about command
syntax - it's that **this 8-block scan never resolves**, for both
operations, and everything downstream (`SAVE` reporting "full", `LOAD`
reporting "not found") looks like a generic fallback/degraded state
reached once that scan gives up, not two independent bugs. Two real
possibilities, not yet distinguished: (1) it's a one-shot boot-time
"is a formatted disk present" check that never finds what it wants
against blocks that are genuinely blank/filler (`0x40`-filled at
512-608, actually zero - past real end-of-file - at 640-736) on this
*specific* real disk, and every later command silently falls back to a
"no disk" state without a fresh probe; or (2) it's a periodic
background poll (plausibly tied into the same interrupt this project's
own Milestone 7 already wired up) that runs continuously regardless of
foreground command activity, in which case it may be entirely
unrelated noise and the real per-command failure point hasn't been
found yet. Also found, incidentally, while tracing this: `abc80_disk_read_block()`
returns success (`ok=1`, zero-filled) for a block number at or past the
real end of the disk-image file (block 640 on this 640-block image)
rather than failure - real hardware would presumably report a
read/seek error for an out-of-range sector; not yet fixed, since it
wasn't shown to be the actual cause of either error above, but flagged
here rather than left silent.

#### The scan is a bounded, one-shot boot check, not periodic - and the disk-image archive is smaller than earlier claimed

Resolved possibility (2) above directly: added temporary instruction-count
tracing to every `abc80_disk_trap()` call (removed afterward) and ran a
boot-only test (no typed command at all, so any activity is purely
boot-time). Result: the scan runs the same eight blocks in a fixed,
uniform ~395-instruction cadence, for **exactly eight full cycles (64
reads total)**, then stops calling the trap entirely for the rest of a
2,000,000-instruction run - boot still reaches the normal `ABC80` banner
and blinking-cursor `READY` state afterward. **This is a one-shot,
bounded retry loop at boot, not a periodic background poll** - option
(1) from the prior write-up, not (2). The uniform spacing (versus the
periodic PIO interrupt's fixed 384 T-states, a different unit and a
different, unrelated number) rules out any connection to Milestone 7's
interrupt.

While looking for a real disk more likely to satisfy this boot check,
re-fetched abc80.net's own `sw/disk_images/ABC80/160k/` directory
listing directly (`curl`, not summarized through a fetch tool) and found
it actually contains **fourteen** images (`disk001.img`-`disk014.img`),
not the forty-nine this project's own earlier write-up claimed -
correcting that count rather than repeating it. Re-downloaded
`disk003.img` fresh and confirmed it's byte-identical (`md5`) to the
copy already used in this investigation, ruling out download corruption
as the cause of anything found so far.

The archive's own `index.txt` labels `disk001.img`/`disk002.img` as an
official `SYSTEMSKIVA VER.1.0` with a real Luxor article number
(`ARTNR:68 99101-31`), versus `disk003.img`'s third-party-authored
`System.diskett ABC80 Ver. 2.1` - a plausible reason a specific disk
might not match whatever this ROM's boot check expects. More usefully,
**`disk009.img`'s own label reads `"Basregister 80 / Programskiva / DR
0: / Run start"`** - real, primary-source confirmation that `DR0:` genuinely
is the correct floppy device-prefix syntax (resolving that "not
confirmed" item from the previous write-up) and, incidentally, that a
real ABC80 user selects/runs a floppy program with a plain `DR 0:`
prefix, matching this project's own earlier guess.

Comparing raw bytes across all four candidate images at exactly the
scanned blocks (512, 544, 576, 608) found something unexpected:
`disk001.img`, `disk002.img`, and `disk003.img` all have the identical
`0x40`-filled filler there, but **`disk009.img` has real, structured,
non-filler data at those same four blocks** - a promising, concrete lead.
Testing `disk009.img` through the same boot-only trace, though,
**produced an identical trace to `disk003.img`, byte-for-byte** (same 64
reads, same instruction counts, same final PC/state) - real content at
those blocks doesn't change the outcome at all, at least not with the
disk-image-read bug described next still in place. Ruled out, not yet
explained: whatever this check is actually testing, it isn't simply
"is there non-blank data at these four blocks."

**Found and fixed a real bug this uncovered**: `abc80_disk_read_block()`
returned success (`ok=1`, zero-filled) for a block number at or past the
real end of the disk-image file - block 640 onward is out of range on
this 640-block (163840-byte) image, and a real floppy controller would
report a genuine seek/sector-not-found error there, not silent success.
Fixed to return failure for a short/empty read, matching
`abc80_disk_write_block()`'s own existing convention. Re-running the
boot-only trace with the fix in place shows the scan behaves
differently as a direct result - each cycle now stops after the first
out-of-range block (`512, 544, 576, 608, 640[[fail]]` - five reads, not
eight) rather than reading all eight before restarting - confirming the
ROM's own retry logic really does branch on the read's success/failure,
not just its content. The scan still doesn't ultimately succeed against
either test disk even with this fix, so it isn't the root cause by
itself, but it's a genuine correctness fix or the ROM's own real
success/failure branching would misbehave against any real image with a
different block count.

#### The real breakthrough: the sector-number formula was wrong, not the disk image

Prompted by the user pointing at a real, independent open-source ABC80
emulator - **sasq64/abc80sim** (<https://github.com/sasq64/abc80sim>) -
that also implements floppy support. Rather than guessing at how it
solved this, read its actual controller implementation
(`src/disk.c`/`src/abcio.c`) directly. It takes a completely different
approach from this project's own bypass: it emulates the real ABCbus
port-level protocol itself (card-select via `OUT (1),n` storing
`n & 0x3F`, a 4-byte command packet `K[0..3]` shifted in one byte at a
time via sequential `OUT`s to port 0, dispatch by card-select value -
`36`/`44`/`45`/`46` for `hd`/`mf`/`mo`/`sf` drive types respectively),
rather than trapping a fixed PC address the way this project does. Its
card-select table confirms `45` (`0x2D`) is the "`mo`" (mini-floppy,
single-sided, `40×1×16 = 640` sectors) type - the exact card-select
value and exact sector count this project's own research already
independently found for the real `ABC830` controller, useful
cross-confirmation.

The genuinely new information is `cur_sector()`, the function computing
a real sector index from the command packet's `k[2]`/`k[3]` bytes (this
project's own `D`/`E` register values, per the calling-convention
research above):

```c
if (state->new)
    return (k2 << 8) + k3;
else
    return (((k2 << 3) + (k3 >> 5)) * state->secperclust) + (k3 & 31);
```

The `mo` drive type (matching this project's real disk images) doesn't
set `new`, and has `secperclust == 1`, so the real formula reduces to
`(D << 3) + (E >> 5) + (E & 31)` - **not** the flat `(D << 8) | E` this
project's own `abc80_disk_trap()` had been using since the bypass was
first built. Recomputing the boot-time scan's own eight observed values
against the *real* formula is decisive: all eight share `D = 2`, and
`E` cycles through multiples of `0x20` (so `E & 31` is always `0`),
giving real sector numbers `16, 17, 18, ..., 23` - not `512, 544, ...,
736` as this project had been reporting for three separate investigation
rounds. Sector `16` is the *exact* second copy of the real directory
this project's own earlier research already found - the scan was never
probing some mysterious high-numbered region at all; every prior
investigation session (the blank-image loop, the `ERR 41` trace, the
`disk009.img` comparison, the EOF-read bug) was chasing symptoms of
this one wrong formula, reading/writing the wrong real sector every
single time. Fixed `abc80_disk_trap()` to use the real formula, citing
`abc80sim`'s `disk.c` directly in the comment rather than re-deriving it
from scratch.

**Result, verified by real execution**: `SAVE TEST` against `disk003.img`
now completes with **no error at all** - previously `ERR 41` ("disk
full"). Diffing the disk image before/after shows real, structurally
valid writes: a new eleven-byte directory entry (`TEST    BAC`, start
marker `03 00 00 00` - decoding via this project's own `>>5` hypothesis
to block 24) appended to *both* directory copies (blocks 8 and 16),
real tokenized-BASIC-looking data written to blocks 24-25, and a change
to block 6 (very likely a free-space bitmap being updated - not yet
confirmed). This is the single biggest result in this whole Milestone 6
investigation: real, correct, ROM-driven disk writes, not a trap
returning a plausible-looking success code.

**`LOAD` makes real, different progress too, but isn't fully working
yet**: `LOAD DIRCOPY` (a real pre-existing file) against a *clean*
`disk003.img` now reports **`ERR 37`** (`FELAKTIGT RECORDFORMAT` -
"malformed record format") instead of the old `ERR 21` ("file not
found") - the file is being *found* now (real progress), but something
about interpreting its data once located still isn't right; not yet
diagnosed; the `>>5` start-block hypothesis or the (still-unverified,
still using this project's original disassembly-derived bit layout
rather than `abc80sim`'s `k[1]` layout) channel/buffer extraction are
the most likely remaining suspects. Separately, `LOAD TEST` (loading
back the file just `SAVE`d in the test above, against the *same,
already-modified* image) hit `ERR 48` (`FEL I BIBLIOTEKET` - "error in
the library") - and, oddly, printed that same `ERR 48` once *before* the
`LOAD TEST` command even appeared on screen, suggesting the `SAVE`
above may have left the disk in a subtly inconsistent state (perhaps
the block 6 bitmap-like update isn't fully correct) that a fresh boot
against that same modified image now trips over - not yet investigated.

#### Channel/buffer layout re-verified correct - `LOAD`'s real remaining blocker is a second, disk-position-dependent read

Went looking for the "`B` bits 4-6 = channel" vs. `abc80sim`'s `k[1] & 7`
/`k[1] >> 6` mismatch flagged above by reading `L6068`'s own real body
(`0x606B` onward) instruction-by-instruction, rather than assuming either
side was right. It resolves cleanly, and both are actually correct,
just describing two different things:

- `L606B` immediately overwrites the caller's `B` with a **hardcoded
  function-code constant** (`3` for read, `0x0C` for write - matching
  this project's own earlier finding) before ever sending it anywhere,
  and gets the real drive-select byte from a fixed RAM cell
  (`(0xFD01) & 7`), not from any caller register at all. So `abc80sim`'s
  `k[1] & 7` (drive/unit select) genuinely has no relationship to the
  caller's original `B` - it's real, but it's about a completely
  different byte in the 4-byte command packet than this project's own
  register-level trap ever touches.
- The caller's *original* `B` (saved via `PUSH BC`/`POP BC` around the
  packet-send sequence, restored before use) is read back only much
  later, at `L6106`: `AND 70h` then `RRCA ×4` (i.e. exactly `(B >> 4) &
  0x07`) to compute a **host-RAM buffer address** —
  `H = buf_base_high + channel`, precisely matching
  `abc80_disk_trap()`'s own existing `channel`/`buf_addr` computation.

So this project's own channel/buffer extraction was **already right**,
confirmed directly against the real code rather than left as an
unverified guess - no fix was needed there after all. `abc80sim`'s
`k[1]` bit layout is real too, just for the separate, ROM-internal
drive-select field this project's single-flat-file bypass model doesn't
need to reproduce at all.

**Ruled out `DIRCOPY.BAC`'s blank data as the general explanation for
`ERR 37`**: computed every one of `disk003.img`'s real 14 directory
entries' start blocks with the *exact* controller formula (not the
`>>5`-of-combined-bytes shortcut, which happens to agree with it only
because every entry's low byte has its bottom 5 bits clear) and checked
each against real disk content. Thirteen land on solid, structured,
clearly-real data with a consistent-looking 3-byte header. Only
`DIRCOPY.BAC` (block 127) lands on pure `0x40` filler - the same
"unformatted" pattern found at the old mystery blocks - meaning this
one specific file's data genuinely isn't present on this real dump
(deleted-but-not-cleared entry, or simply never written), not an
emulator bug. Testing `LOAD` against files that *do* have real data
(`WPROT`, `LIB`) still fails, ruling that theory out as the general
cause.

**The real remaining pattern, found by tracing several different `LOAD`
attempts**: every one - regardless of target file or which of
`disk001.img`/`disk002.img`/`disk003.img` is used - follows an
identical shape: read the directory (`16`), read the target file's own
first block (correctly, at whatever block this project's own formula
computes), then two *further* reads into two *different* channels
(`2` and `1`) before failing. The channel-`2` read is always
`target_block + 1` (the file's own second block, a sensible read-ahead)
and always succeeds. The channel-`1` read is the odd one: a fixed
value **not related to the target file at all** - `31` against
`disk003.img`, `29` against `disk001.img`/`disk002.img` - decoding
(via the real sector formula) to `D=3` with varying `E`, the same `D`
value as `BASICERR.SYS`'s own directory entry, i.e. this looks like
"the real last block of the `BASICERR.SYS` message file" (a plausible
disk-integrity/version check reading the system error-message file as
a validation step during open), and it differs between disks simply
because each real disk's own `BASICERR.SYS` is a different real length.
This read also reports success (`ok=1`) every time - the failure
happens interpreting its content, not fetching it, and `buf_addr` was
confirmed clean (exact `0xF500`/`0xF600`/`0xF700` multiples, no
off-by-few address bug) in this simpler case, unlike an unrelated
`0xF503` buf-address anomaly separately spotted in a same-run
`SAVE`-then-`LOAD` test.

`disk009.img` diverges differently under the identical `LOAD LIB` test -
no channel-1/2 pattern at all, a read of block `638` (near the real
640-block disk's own end), and `ERR 48` (library error) rather than
`ERR 37` - consistent with `disk009.img` simply having a different real
directory/file layout (a different, non-system disk) rather than
pointing at the same bug.

#### Live-traced the real `ERR 37`/`ERR 48` chain to its actual mechanism - a real understanding gained, but the true root cause is still open

Went back to the emulator itself (temporary PC/register watchpoints at
several specific addresses, removed afterward) rather than more static
reading, since the previous round's "channel-1 read looks like a
`BASICERR.SYS` validation" theory turned out to need direct verification.
Two of this round's own working theories were tested and **disproven** in
the process - recorded here rather than quietly dropped, since both
looked highly plausible before being checked:

- **Not cassette code.** A static grep for the literal byte pattern that
  raises error `0xA5` (37) turned up `LD A,0A5h` at `0x05F6`, inside what
  looked exactly like a Kansas-City-FSK cassette receiver (`CP 02h`/
  `CP 03h` STX/ETX framing, a checksum, calls into `L0619`/`L061B`).
  Live-watching that exact address during a real `LOAD` showed **it is
  never executed at all** - a coincidental false match, not the real
  path. (`L0619`/`L061B` themselves turned out to be the tail of the
  periodic PIO interrupt handler already documented in Milestone 7,
  reused as a general-purpose "commit state and return via `RETI`"
  primitive - not cassette-specific code at all, on closer reading.)
- **Not a `DR0:`-prefix parsing problem, and not failing device-chain
  matching either.** Tried `LOAD WPROT`, `LOAD DR0:WPROT`, and
  `LOAD DR 0:WPROT` (matching `disk009.img`'s own space-separated label
  literally) - all three produced **byte-for-byte identical execution
  traces**, ruling out prefix syntax as the variable that matters here.
  More importantly, dumping the live device chain at `0xFE0A` at the
  moment of failure shows it's built exactly right: `DR0`-`DR6` all
  present, all pointing at the real shared handler `0x6EE4`, `CAS`/`PR`/
  `IEC` present and correctly terminating the chain - the DOS ROM's own
  device registration (Milestone 6's earlier `L6D24` finding) is
  confirmed working correctly with the sector-formula fix in place.

**What's actually happening, confirmed by live register/memory
inspection**: the real failure is inside a base-ROM routine at `0x0819`
onward that builds a small RAM record for the parsed command - found by
dumping it directly rather than guessing: `[self-pointer:2][resume
address:2]["WPROT   BAC"]["DR0"][6 more bytes]`, a genuine parsed-command
structure holding the 8+3 filename and the 3-character device name this
project's own device-chain research already described, confirming that
part of the design end-to-end. Chasing the actual error through nested
calls (`L084B`/`L088A`/`L08A5`/`L0184`) shows this code path isn't
device-name matching at all, despite resembling it superficially - it's
default-extension handling (conditionally copying a literal `"BAC"`/
`"BAS"` string into the record if no extension was typed), and a shared
low-level primitive (`L08A5`) that reads two bytes from a register-supplied
pointer, does arithmetic against the *call site's own return address*
(a `-9`-through-`+15`-step encoded "which of 9 near-identical call sites was
this" trick), and hands the result to `L0184`, which requires the
resulting byte to be `0xC3` (a real `JP` opcode) as a sanity check before
trusting it further - failing that check is what raises the error. In
the traced case, the pointer being dereferenced landed on the *first two
characters of the filename itself* (`"WP"` from `"WPROT"`, read as the
16-bit value `0x5057`) rather than on anything resembling real code,
which is certainly wrong, but **why** the pointer ends up there - whether
this project's bypass has left some register/RAM cell unpopulated that a
real ABCbus transaction would set, or whether this specific code path
was never meant to be reached this way at all - is not yet understood.
Documented honestly as the real state of things rather than asserting a
fix that hasn't been verified.

#### `LOAD DR0:NOSUCH` (a genuinely nonexistent file) vs. `LOAD DR0:WPROT` - a real, informative divergence

Compared live traces (same temporary watchpoints as above) between a
known-real file and a filename this project deliberately made up. Three
concrete, new facts came out of it, each correcting or narrowing the
previous write-up:

- **`SAVE` never touches any of this code at all.** A working
  `SAVE DR0:TEST` (confirmed successful, no `ERR`) hits none of the
  `0x0010`/`0x062E`/`0x085A` watchpoints - this entire mechanism is
  `LOAD`-specific (or shared by `LOAD`/`CHAIN`/`MERGE` but not `SAVE`,
  matching the base ROM's own per-verb dispatch this project's Milestone
  6 research already found), not a general disk-I/O problem. Useful
  scoping: whatever's wrong here cannot be the same bug that made `SAVE`
  fail before the sector-formula fix.
- **The real device-name-matching code was found, and it's not
  `L08A5`/`L0184` at all.** Tracing further back turned up the actual
  comparison loop at `0x07F8`: `LD DE,(0xFE0A)` (the real chain head),
  then a 3-byte compare of `(IX+11)`/`(IX+12)`/`(IX+13)` - exactly where
  this project's own earlier memory dump found the parsed `"DR0"` device
  name sitting in the command record - against each chain entry's own
  name field, advancing via `CALL L101D` on a mismatch. **This is what
  actually walks the device chain**, and it runs and succeeds *before*
  `0x0819`'s dispatcher (and therefore before `L084B`/`L088A`/`L08A5`)
  ever starts - meaning the code this project spent the previous round
  tracing is genuinely *post*-match logic, confirming (again, more
  concretely this time) that device-chain matching itself isn't the
  failure point.
- **`NOSUCH` correctly reaches `ERR 21` ("file not found") through a
  different path than `WPROT`'s `ERR 48`.** Both traces start identically
  at `0x085A`, but then diverge: `WPROT` goes straight to the `0x062E`
  error dispatcher with `A=0xB0` (48, "library error"); `NOSUCH` instead
  goes through `0x0010` (the `RST 10h`-style inline-error-byte mechanism)
  with `A=0x95` (21, "file not found") *twice* before *also* eventually
  reaching the same terminal state `WPROT` does. **Both cases end on an
  identical final step**: `A=0xA5` (37) reached via `top_of_stack=0xF603`
  - the same `L6E82` message-lookup address this project's previous round
  already identified. This is the single most useful fact from this
  comparison: **the on-screen `ERR 37` is not the real error in either
  case** - it's a uniform secondary failure (the DOS ROM's own attempt to
  look up nice Swedish text for whatever the *real* error code already
  was, itself failing and overwriting `A` with 37 right before printing)
  that happens identically whether the underlying problem is a
  legitimate "not found" (21, correct behavior for `NOSUCH`) or something
  else (48, for a file that demonstrably does exist and has real data,
  `WPROT`). Every `ERR 37` this project has reported in this whole
  investigation - going back to the very first `SAVE` test before the
  sector-formula fix - needs to be re-read as "some earlier error the
  message-lookup mechanism then failed to describe," not as "malformed
  record format" literally.

This means the real, still-open question has narrowed to two genuinely
separate problems rather than one: **(1)** why does `L6E82`'s
message-lookup always fail, regardless of the real underlying error
(likely fixable in isolation, and worth fixing first since it currently
hides every other error code behind an identical, misleading `ERR 37`);
and **(2)** why does `WPROT` - confirmed to exist, confirmed to have real
data at the correct block - hit `ERR 48` at all instead of loading
successfully, which is now understood to happen somewhere after the
`0x07F8` device match already succeeded, inside the `L084B`/`0x085A`
dispatch this project traced last round, whose exact purpose is still
not pinned down.

#### The real root cause: `disk003.img` stores sectors in physical, interleaved order - `LOAD` of a real file now works end-to-end

The user pointed at two more independent ABC80 projects while this
project was mid-investigation of problem 1 above -
[andersrcarlsson-stack/abc80-pico-public](https://github.com/andersrcarlsson-stack/abc80-pico-public)
(a real hardware ABC80 emulator running on a Raspberry Pi Pico, whose
own README claims "authentic ABCDOS - `SAVE`/`LOAD` the real ABC-bus
way") and Torfinn Ingolfsen's `abc80sim` notes page (archived via the
Wayback Machine, since the live site wasn't reachable from this
environment - `curl` to it times out on port 443/80; the archived copy
turned out to be pure build-troubleshooting logs with no protocol
detail, a dead end, but confirmed harmless to rule out). The pico
project's own `src/disk_controller.cpp` - fetched and read directly,
not summarized - was the real find.

Its own comments state plainly what this project had never modeled:
**a real ABC830 ("mo") floppy's 16 sectors per track are physically
interleaved (interleave factor 7), and the `.img` files abc80.net
distributes are stored in physical order** - meaning the *logical*
sector number this project's own `L6068`/`L60A1` calling-convention
research already correctly derived (`(D<<3) + (E>>5) + (E&31)`) is
**not** the same as a raw byte offset into the `.img` file. A logical
sector must be mapped to its physical position first:

```c
constexpr unsigned IL_FAC = 7, IL_MASK = 15;
inline unsigned phys_sector(unsigned s) {
    return (s & ~IL_MASK) | ((s * IL_FAC) & IL_MASK);
}
```

This project's own bypass (`abc80_disk_read_block()`/
`abc80_disk_write_block()`) had been indexing `disk003.img` directly by
the logical sector number since `--disk` was first built - every single
disk access in every investigation round this milestone has been
reading/writing the wrong physical sector, *except* when the logical
sector happened to be a track-boundary multiple of 16 (which map to
themselves under this permutation) - exactly the base directory copies
at sectors 8/16, and the boot-time scan's own sectors, both of which
*always* looked correct across every previous round precisely because
they're fixed points of the interleave. This explains, in hindsight,
why device-chain registration, the directory format, and the sector
formula itself all checked out under direct inspection while `LOAD`
still failed: everything *except* actual non-boundary file data was
being tested correctly by accident.

**Verified empirically before touching any code**: recomputed every one
of `disk003.img`'s 14 real directory entries' physical sector (not just
`DIRCOPY.BAC`, whose logical block 127 → physical 121 was the original
anomaly) and checked the real bytes there. Every single one now shows a
clean, consistent per-file header (`0x10 × directory-position`, `00`,
`00`, `0xFF`, ...) - unmistakable, structured real data, not a
coincidence.

Implemented as `abc80_disk_phys_sector()` in `abc80/emu/src/main.c`,
applied in both `abc80_disk_read_block()` and
`abc80_disk_write_block()` before computing the file offset. **Result,
verified by real execution**: `LOAD DR0:WPROT` now completes with no
error and `LIST` shows the genuine, real Luxor `WPROT.BAC` utility's
actual BASIC source (`10 REM ... WPROT.BAC ... Skriv/raderskyddar
enstaka filer för ABC80 ... DATORUTVECKLING ... LUXOR`, real Swedish
comments and all) - the first real file this project has ever
successfully loaded off a real ABC80 disk image. `LOAD DR0:DIRCOPY`
(the original blank-data anomaly) and `LOAD DR0:LIB`/`LOAD
DR0:MARKDISK` all now succeed too, with no error. `LOAD DR0:MAP`
correctly reports `HITTAR EJ FILEN` ("file not found") - genuinely
correct behavior, since `MAP` on disk is a `.ABS` machine-code file, not
a `.BAC`/`.BAS` BASIC program `LOAD`'s own default-extension search
would find. Also incidentally confirms **problem 1 is largely resolved
by the same fix**: the `L6E82` message-lookup mechanism reads from these
same interleaved sectors (the real Swedish error-message text this
project extracted earlier for `ABC80_REFERENCE.md`), so a `LOAD DR0:
<name-that-genuinely-exists-but-fails-for-another-reason>` test now
shows the real message `FEL I BIBLIOTEKET` on screen instead of a
numeric fallback `ERR 37` - the uniform-secondary-failure masking
problem this project's previous round diagnosed is gone for the cases
tested so far.

Full regression suite (`make test`) still passes unchanged - this fix
only touches the `--disk`-gated bypass path.

#### The last bug: a dual-purpose RAM cell this bypass never fully accounted for - full `SAVE`/`LOAD` round trip now works

Traced the `SAVE`-then-`LOAD` failure with the same live-tracing approach
as the rest of this milestone (temporary instrumentation, removed after)
rather than guessing again. The `SAVE` trace showed something
concrete: two late writes in the sequence - the free-space-bookkeeping
write to block 6 and, critically, the *second* write to the new file's
own directory-entry block - used `buf_addr = 0xF503`/`0xF703` instead of
the expected `0xF500`/`0xF700`, and that second write overwrote the
*correct* header this project's own `SAVE` had just written moments
earlier with garbage (`FF 17 61 FF FF ...`) - the real, concrete cause
of `ERR 48` on load-back.

`abc80_disk_trap()`'s `buf_addr` computation reads `ABC80_DOS_BUFPTR_ADDR`
(`0xFD12`/`0xFD13`) live from RAM on every call, on the reasoning
(documented in this project's own earlier comment) that "DOS init sets
it to `0xF500`... read live... in case some future ROM variant changes
it." Checking `abcdos80_dasm.txt` for every place that RAM cell is
written turned up several addresses *outside* `L6068`/`L60A1` - real,
un-bypassed ROM code this project's trap never touches - and one of them
(`0x61C0`, `LD (0xFD12),A`) writes *only the low byte*. Since a real
256-aligned buffer base's low byte is always `0x00`, that same RAM cell
is evidently dual-purpose in the real ROM: also reused as a live
byte-transfer progress counter inside the real per-byte transfer
routines this bypass replaces wholesale with an instant `fread`/
`fwrite`. Because this bypass never runs that real routine, whatever
partial count real, un-trapped ROM code left there earlier (observed
live: `3`) is never reset back to `0`, silently shifting every
subsequent buffer address by that leftover count and corrupting
whatever real data was already at the shifted destination.

Fixed by no longer trusting the low byte at all - `buf_base` is now
`(ram[ABC80_DOS_BUFPTR_ADDR + 1]) << 8` (high byte only, low byte forced
to `0`), keeping the live high-byte read for the legitimate reason it
was added (a future ROM/config might relocate the buffer region) while
discarding the part proven to be repurposed.

**Result, verified by real execution across a genuinely fresh, separate
run** (not the same process/session, so no in-memory state could paper
over a real bug): `SAVE DR0:FINAL` (`10 PRINT "HELLO WORLD"` / `20 PRINT
1+1`) completes with no error; a completely separate `bin/abc80` process
run afterward against the saved image does `LOAD DR0:FINAL` / `LIST` and
prints back the exact two lines saved, byte-for-byte. `LOAD DR0:WPROT`
(the real, disk-distributed file) still works identically after this
change. `make test` still passes unchanged - this fix, like the
interleaving one, only touches the `--disk`-gated bypass path.

**This closes the loop this milestone set out to prove**: a real,
unmodified ABC-DOS ROM, running on this project's shared Z80 core, can
genuinely `SAVE` a BASIC program to a virtual floppy and `LOAD` it back
in a later, independent session - the concrete goal stated all the way
back at this section's own "Milestone 6: ABCbus expansion" opening.

#### Confirmed: a real multi-block round trip, verified byte-for-byte rather than by eye

The short single-block programs tested above don't exercise the
multi-sector case at all - a 256-byte block holds an entire small
program, so `SAVE`/`LOAD` never needs to span a sector boundary, chain
multiple blocks, or exercise the channel-2 read-ahead buffer this
project's own tracing found earlier for real files. Built a 26-line
program (`10 PRINT "LINE0010_XXXX...XXX"` through `260 PRINT "END OF
PROGRAM"`, each line padded to ~70 characters) specifically to be large
enough to require several sectors.

Screen-scrollback inspection turned out to be the wrong verification
tool for this - `bin/abc80`'s batch mode only captures a single
end-of-run 40×24 screen snapshot, so a `LIST` longer than 24 lines only
shows its final visible page, not the full output. Used `--quicksave`
instead for a rigorous, complete check: dumped the tokenized program
from memory (`BOFA..EOFA`) immediately after typing it in but *before*
`SAVE`, then - in a completely separate `bin/abc80` process run,
against the saved disk image - `LOAD`ed it back and dumped memory again
with a second `--quicksave`. **The two dumps are byte-for-byte
identical**: 1725 bytes each, spanning 7 real 256-byte sectors (not a
single-block case at all). `make test` unaffected, no code changes were
needed - this was purely a verification exercise confirming the
existing fix generalizes.

**Remaining, smaller open items** (superseded - see the sub-step below,
which closed all three):
- ~~Re-examine the still-open items from before the interleaving fix -
  the exact `B` bits 0-3/7, independent transfer-size confirmation - now
  that real files load and save correctly, several may resolve quickly
  or turn out moot.~~
- ~~Consider committing `disk003.img` (or a small, purpose-built test
  image) into the repo now that it backs a real, working feature rather
  than a research artifact - still an open decision, not yet made.~~

#### Sub-step: closing the three remaining loose ends

Real, bounded reverse-engineering investigation, not a guaranteed code
change going in - the honest outcome turned out to be "confirmed already
correct, documented richly, no behavior change warranted" for two of the
three items.

**`B`'s bits 0-3/7 - confirmed genuinely unused, not just "not yet
decoded."** Disassembled the real ABC-DOS ROM directly (using the
just-shipped recursive-traversal `z80dasm` - sliced a temporary,
untracked copy of the already-committed `resources/rom/ABCDOS80.bin`
starting at each already-known real entry address's own file offset, so
each slice's own byte 0 is that real address, avoiding the tool's
deliberate single-entry-point limitation without touching its code) at
`L6068`/`L606B`/`L607D`/`L608F` (read) and `L60A1`/`L60A4`/`L60B4`
(write). `L6106` - the shared channel-decode routine both paths call -
is confirmed to be the *only* place either routine ever reads the
caller's original `B` (`LD A,B / AND 70h / RRCA x4`, i.e. exactly
`abc80_disk_trap()`'s own `(cpu->b >> 4) & 0x07`); reading completely
through both routines' real disassembled bodies found no other reference
to it anywhere - not a drive-select check, not a retry flag, nothing.
`disk.c`'s own comment updated from "not yet decoded" to state this
directly, with the disassembly evidence behind it.

**`L608F`'s two failure paths - real, richer behavior than this bypass
models, characterized in full, deliberately not reproduced.** The real
read and write paths turn out to differ from each other, and from what
"two failure paths" undersold: both poll a live hardware status byte
(port 0, cached at RAM `0xFD15`) against a 5-attempt retry counter (RAM
`0xFD18`, initialized by `L60D2`), but

- **Write** (`L60A1`'s inline failure handling at `0x60C1`-`0x60D1`):
  cleanly bounded - `BIT 7,A` on the status byte returns immediately
  (Carry set, `A` = the raw status byte) if set; if clear, retries up to
  5 times (`DEC (HL)` against the counter, `JR NZ,L60A4`), and even once
  retries are exhausted the return signature is identical (Carry set,
  `A` = the same raw status byte) - one real failure signature.
- **Read** (`L608F`): a genuinely different structure - if the polled
  status byte is exactly `0`, returns immediately (Carry set, `A=0`, a
  *different* signature than the bit-7-set case). Otherwise, if bit 7 is
  clear, it retries (`JR Z,L606B`) **without ever checking whether the
  retry counter has reached zero** - unlike the write path, this can loop
  indefinitely on a persistently nonzero, bit-7-clear status rather than
  eventually giving up. Only a nonzero status with bit 7 set produces the
  final `SCF`/`RET` (Carry set, `A` = raw status byte).

Deliberately not reproduced in `abc80_disk_trap()`: there's no real
per-attempt hardware condition in this bypass for a retry to plausibly
recover from (a host `fread`/`fwrite` failure here is either a genuine
out-of-range block or a real disk-full condition - not transient), and
distinguishing `A=0` from `A=`status-byte` from this bypass's own host-
side failures would mean inventing a mapping onto the real ROM's status-
port bit patterns with no principled way to derive it short of modeling
port 0/1 hardware this bypass intentionally doesn't (`disk.c`'s comment
now documents this finding and the reasoning in full). No real software
failure has ever been observed from the current single `A=0x01` signal -
matches this project's own precedent elsewhere (no SN76477 external-
voltage-input modes, no real keyboard scan-matrix PROM) for a real,
richer gap found and documented but not acted on absent a concrete
failing case.

**`disk003.img`: not committed - decided.** Unlike the ROM images (which
got an explicit checksum/provenance table before being committed) or
ZEXALL/ZEXDOC (explicitly GPLv2), `disk003.img` has no license statement
anywhere - it's a third-party-authored disk image from abc80.net's
archive. Re-downloaded it locally (untracked) for this investigation's
own live sanity check (a `SAVE`/`LOAD DR0:` round trip against the real
image, confirmed still clean, no regression) but it stays out of the
repo. To reproduce: `https://www.abc80.net/archive/luxor/sw/disk_images/
ABC80/160k/disk003.img` (163840 bytes, "System.diskett ABC80 Ver. 2.1"
per that directory's own `index.txt`, same source already documented
above in this section).

#### Disk-full behavior confirmed - two real, distinct capacity limits, both handled correctly and safely

Investigated whether `SAVE` correctly detects and reports a genuinely
full disk, and whether it leaves anything corrupted when it does.
First decoded the free-space bitmap's real format empirically (the
established method throughout this milestone) rather than guessing:
`SAVE`d one small file and diffed block 6 before/after, finding exactly
two bits flipped - at byte 23, bits 3 and 4, matching *precisely* the
new file's own two logical blocks (`187`, `188`: `187 = 23×8+3`,
`188 = 23×8+4`). Confirms a plain 1-bit-per-block bitmap (`byte =
block/8`, `bit = block%8`, `1 = used`) spanning exactly the 640 real
blocks (bytes `0`-`79` - `640/8` - a clean fit), with a separate
counter elsewhere in the same block (byte 239, incrementing by 1 per
`SAVE`) that isn't part of the bitmap itself and wasn't investigated
further.

**Two separate, real capacity limits exist on this disk, not one**:

1. **The directory itself is small - 15 real entries maximum, not 16.**
   `disk003.img` starts with 14 real files; one more `SAVE` succeeds
   (the 15th), but a second attempt immediately fails with `FEL I
   BIBLIOTEKET` (`ERR 48`, "error in the library") - the directory
   block's 16th and final 16-byte slot is evidently reserved (perhaps
   an implicit end-of-directory marker), not available for a real
   entry. Confirmed completely safe: three consecutive failed `SAVE`
   attempts produced a byte-for-byte identical disk image to a single
   successful save - no partial writes, no corruption, genuinely
   idempotent failure.
2. **Genuine block-space exhaustion reports the real `ERR 41`
   (`SKIVAN FULL`) correctly** - confirmed by hand-editing a copy of
   the bitmap to leave only 3 blocks genuinely free (a realistic
   "nearly full" state, not the initial all-`0xFF` "zero free
   anywhere" edge case tried first, which - worth recording rather
   than quietly discarding - produced the wrong, misleading `HITTAR EJ
   FILEN` / `ERR 21` instead, live-traced to a real `A=0x95` at the
   error dispatcher; evidently the ROM's own free-space search
   short-circuits into a different, incorrect path when *no* block
   anywhere is free, a state a real, gradually-filled disk likely
   never actually reaches given the directory limit above caps how
   much any single disk could ever fill through legitimate use before
   running out of file slots first - noted as a real quirk, not
   pursued further since it isn't a reachable real-world case). Against
   the 3-genuinely-free-block image, `SAVE`ing the earlier 7-block
   `BIGPROG` test program correctly live-traces to `A=0xA9` (masked
   `41`) at the error dispatcher and prints the real `SKIVAN FULL` text.

**Not fully atomic, but safely so - and this matches real legacy-DOS
behavior, not a bug in this project's bypass**: the failed `SAVE` does
leave a new, incomplete directory entry behind (`BIGPROG.BAC`, correctly
named, with only the 3 blocks that fit actually written) rather than
cleanly rolling back - inherent to any DOS that allocates and writes
sector-by-sector without a pre-flight "is there enough total space"
check, real hardware included, not something this bypass introduces.
Diffed the full disk image to confirm the blast radius is exactly what
it should be and no more: only the directory blocks, the bitmap block,
and the 3 newly-written data blocks changed - **zero overlap with any
of the 14 pre-existing real files' own data blocks**, confirmed by
direct comparison against their known physical block numbers. Loading
the resulting partial file back afterward fails safely and sensibly
too: `LOAD DR0:BIGPROG` reports `RECORDNUMMER UTANFÖR FILEN` ("record
number outside the file") - a genuinely correct description of a
directory entry whose claimed extent outruns its real data - and
`LIST` shows exactly the real partial content that made it to disk
(the program's first two lines) with no crash, no hang, and a clean
exit.

**Closing this item**: both real capacity limits this disk can produce
are now confirmed to behave correctly and safely. No code changes were
needed - this was a pure verification exercise, like the multi-block
round-trip check before it.

#### `UFD80V20.bin` examined (research only, not wired up) - a real, more general sibling driver, not a simpler fallback

Disassembled the alternate DOS ROM (`bin/z80dasm`, same `0x6000` base
assumption as `ABCDOS80.bin` - it decodes cleanly there too) to see
whether it might be a useful fallback or point of comparison, now that
`ABCDOS80.bin` itself is fully working. It's neither a dead end nor a
simpler alternative - it shares the exact same real hardware protocol
at the low level, confirmed concretely rather than assumed:

- **Same function codes.** Found its own read/write entry points (two
  variants each, differing only in an initial channel value) load `C=3`
  (read sector) and `C=0x0C` (write sector) into the same field
  position before transmitting - byte-for-byte the same real ABC830-
  family controller function codes this project's own `ABCDOS80.bin`
  research already found and `abc80sim`/the pico project's own
  controller code confirmed independently. Real, concrete proof the
  underlying hardware protocol is a genuine constant across DOS
  variants, not an `ABCDOS80.bin`-specific detail.
- **Same 4-byte command-packet mechanism**, transmitted one byte at a
  time via `OUT (0)` with the identical `IN (1)`/`RRCA`/wait-for-ready
  handshake this project already reverse-engineered - just a different
  register-to-packet-field order (`C,B,D,E` here vs. `ABCDOS80.bin`'s
  `B,C,D,E`).
- **Same buffer-address computation.** Its own equivalent of
  `ABCDOS80.bin`'s `L6106` (`L620A` here) reads the identical RAM cell
  (`0xFD12`) and extracts the caller's `B` bits 4-6 as a channel number
  added to the buffer base's high byte - the exact mechanism this
  project's own bypass implements, including (by the same reasoning
  that led to this project's own dual-purpose-low-byte bug) presumably
  the same low-byte-reuse hazard, unconfirmed but structurally
  identical.
- **Genuinely more general than `ABCDOS80.bin`, not simpler.** Where
  `ABCDOS80.bin` hardcodes a single `OUT (1),0x2Dh` ("mo" drive type)
  card-select, `UFD80V20.bin` computes its card-select value at runtime
  from a RAM-configurable per-unit table (`0xFD01` masked and looked up
  against a table near `0xFFC0`/`0xFFF9`), and branches to a *different*
  sector-address bit-mask depending on the resolved drive type (`H=1`
  vs. `H=4` selecting different constants at its own sector-encoding
  routine) - real support for multiple real controller/media types
  (`mo`/`mf`/`sf`) in one ROM, not just the single type `ABCDOS80.bin`
  assumes. It also uses the Z80's own `OUTI` block-transfer instruction
  for bulk output instead of `ABCDOS80.bin`'s manual byte-at-a-time
  polled loop - a real firmware-level optimization difference between
  the two.

**Not pursued further, and why**: none of `ABCDOS80.bin`'s specific
trap addresses (`0x6068`/`0x60A1`) apply here - `UFD80V20.bin` is laid
out completely differently, so wiring up a working `--disk` bypass for
it would mean re-deriving its own equivalent entry points and its own
per-drive-type sector formula from scratch, comparable in scope to this
whole milestone's `ABCDOS80.bin` effort, for a ROM that isn't gating
anything now that `ABCDOS80.bin` itself has a complete, verified
`SAVE`/`LOAD` round trip. Documented here as real, grounded research
(not "still open" in the sense of blocking anything) in case future
work specifically wants multi-drive-type support this project's current
bypass doesn't attempt.

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

**Known gap at the time, since fixed**: this milestone originally shipped
with `ISIG` left enabled, so host Ctrl-C always quit this tool rather than
reaching BASIC as a real break keystroke - see Milestone 9 immediately
below for the fix.

## Milestone 9: real Ctrl-C break through `--interactive` — done

**Goal**: close Milestone 8's own documented gap - let a genuine Ctrl-C
keystroke reach BASIC's own break handling, the way real hardware would,
instead of always just quitting this emulator tool.

**Fix**: rather than disabling `ISIG` outright (which would also silence
Ctrl-\ and Ctrl-Z), `abc80_console_init()` now disables only `VINTR` (the
specific control character that raises `SIGINT` - Ctrl-C on essentially
every terminal) via `_POSIX_VDISABLE`, a standard POSIX termios mechanism
for turning off one control character without touching `ISIG` itself.
`ISIG` stays enabled, so Ctrl-\ (`SIGQUIT`) still behaves as a real
signal - repurposed as this tool's own "quit cleanly" key now that Ctrl-C
is freed up for the emulated ROM. Both `SIGINT` (still reachable via an
external `kill -INT`, even though the terminal itself won't generate it
via Ctrl-C anymore) and `SIGQUIT` are handled identically (same flag, same
clean-exit path through the end of `main()`), and the final run summary
now reports which one actually fired.

Once `VINTR` is disabled, Ctrl-C simply arrives as a plain `0x03` byte
through `read()`, indistinguishable from any other keystroke - no special-
casing needed anywhere in `poll_stdin_byte()`/`abc80_keyboard_press()`,
since neither ever treated any byte value specially to begin with.

**Verified end-to-end against the real ROM, not just "the byte gets
through"**: piped a program defining an infinite loop
(`10 PRINT 1` / `20 GOTO 10`), started it with `RUN`, then sent a real
`0x03` byte partway through. Traced the full real consequence via this
ROM's own disassembly-confirmed code paths:
- The periodic interrupt handler (`0x031E`, Milestone 7) saw the
  keystroke and set the real break-pending flag at `0xFE07` to `0x83`.
- The BASIC line-execution dispatch loop (found via disassembly:
  `L0D6F`/`L0D90` at `0x0D6F`/`0x0D90`, called once per executed program
  line) calls a real check-and-clear routine at `0x033E`
  (`LD A,(0FE07h) / AND A / LD A,00h / LD (0FE07h),A / RET`, returning
  with the Z flag reflecting the flag's value *before* clearing it) and
  jumps to a real break handler (`0x2321`) when it finds the flag set.
- That handler executes `RST 10h` (a real BDOS-style restart vector) and
  eventually returns to the READY prompt - confirmed not by assumption
  but by the actual final screen content: **`STOP LINE 10`**, the
  authentic ABC80 BASIC break message, printed at exactly the line the
  loop was interrupted on.
- A first test run appeared to show no break at all (the loop just kept
  printing), which turned out to be a test-methodology artifact, not a
  bug: the process was killed and its output captured too early, before
  the ROM had finished printing `STOP LINE 10` - a longer observation
  window showed the correct behavior clearly.

Full regression suite (`make test`, plus every existing `--interactive`
and default-mode ABC80 check from Milestones 1-8) still passes unchanged.

## Milestone 10: left/right arrow keys — done

**Goal**: a real ABC80 owner's own account of the hardware - "the left
key worked as a backspace (delete left)" - prompted checking whether the
same could be done here, rather than assuming it and guessing at a
mapping.

**Grounded, not assumed**: disassembling this ROM's own line-editor
(`0x02BC`-`0x02CE`, the same routine Milestone 3's keyboard debounce work
and this document's own Milestone 9 write-up both already reference)
confirms real ABC80 hardware has no dedicated cursor-key escape sequence
for these two keys at all - it reuses two adjacent, pre-existing ASCII
control codes instead, unsurprising for 1978 hardware (full VT100-style
arrow-key escape sequences weren't yet a settled convention):

- **Left arrow = `0x08`** (ASCII Backspace/Ctrl-H). `CP 08h / JR Z,L02B9`
  in the line editor routes it to `L035B` (`0x035B`), which decrements
  the column counter, walks the line buffer pointer back one position,
  and writes a literal space into video RAM at the vacated screen cell -
  a genuine, destructive delete-left, not a mere cursor move. Confirms
  the real owner's own memory of the hardware exactly.
- **Right arrow = `0x09`** (ASCII Tab/Ctrl-I). `CP 09h / CALL Z,L0348`
  routes it to `L0348` (`0x0348`), the non-destructive counterpart: it
  just walks a lookahead pointer forward, re-displaying whatever was
  already there, unless it hits the line's own terminating CR.

**Implementation**: a modern terminal's arrow keys don't send `0x08`/
`0x09` - they send a 3-byte ANSI/VT100 CSI sequence (`ESC [ D` for left,
`ESC [ C` for right). `poll_keyboard_byte()` (`abc80/emu/src/main.c`), a
small state machine wrapping the existing `poll_stdin_byte()`, recognizes
exactly those two sequences as they arrive one byte at a time and
rewrites them to the real ABC80 byte codes above before anything reaches
`abc80_keyboard_press()`. Deliberately narrow, not a general VT100 input
parser: any other CSI sequence (e.g. up/down), or a lone ESC with nothing
recognizable following it within a short timeout
(`ABC80_ESC_SEQUENCE_TIMEOUT_SEC`, 50ms - generous for any real terminal,
whose own multi-byte sequences arrive within microseconds of each other,
while still resolving a genuine standalone Escape keypress promptly), is
simply dropped rather than forwarded - harmless either way, since the
real ROM's own line editor already silently ignores any control byte
below `0x20` it doesn't specifically recognize. Applies uniformly to both
`--interactive` and default (piped/scripted) input, since
`poll_stdin_byte()` never treated interactive and piped input
differently to begin with.

**Verified against the real ROM**: typed `10 PRINT 1X`, then two real
`ESC [ D` sequences (deleting `X` then `1`), then `2` - `LIST` afterward
showed the correctly-edited `10 PRINT 2`, not `10 PRINT 1X2` or any
corrupted variant. Full regression suite still passes; this only touches
`poll_stdin_byte()`'s one call site, wrapping it rather than changing its
own behavior.

## Milestone 11: a real GTK window — done

**Goal**: run `bin/abc80` in its own window instead of a host terminal, as
a real pixel framebuffer rather than routing through a terminal widget -
see the scoping rationale immediately below for why. Scoped via this
project's own `EnterPlanMode` workflow (research into `render.c`,
`chargen.c`/`.h`, `video_timing.c`/`.h`, `keyboard.h`, `sound.c`, and
`cpm/gtk/`'s existing GTK4 precedent, then two architecture questions
resolved with the user) before any code was written - the full plan is
preserved at `/Users/hans/.claude/plans/mellow-cooking-parrot.md` for
this session's own reference.

**The key architectural decision**: this shouldn't follow `cpm/gtk/`'s
own precedent (a thin launcher spawning the real CLI binary under a pty,
handed to a `VteTerminal` widget - see `cpm/gtk/README.md`). That
pattern is the right fit for CP/M specifically because CP/M output
really is just VT100/ANSI text a real terminal widget renders correctly
on its own. ABC80 is different: Milestone 2's own display-backend
decision (`abc80/emu/src/render.c`) *deliberately* chose a terminal-glyph
approach "for cheapness," explicitly deferring real pixel rendering -
GRAPHICS mode (the real 2×3 block-mosaic mode) is approximated today by
mapping onto Unicode's "Symbols for Legacy Computing" sextant block
rather than drawing real pixels, because a terminal cell can't address
individual pixels at all. A real GTK window - a `GtkDrawingArea` driven
by Cairo, rendering actual pixels rather than routing through a terminal
widget - closes that gap instead of working around it, building on
pixel-decode logic `bin/abc80-chargen-dump`, `bin/abc80-video-timing-
dump`, and `bin/abc80-render-demo` (Milestone 2) already independently
verify against known synthetic input.

Two architecture questions were resolved with the user before writing
any code (both the recommended option in each case): **execution
model** - single-threaded, GLib-timer-batched (a `g_timeout_add()`
callback runs a bounded instruction batch each time it fires, then
returns to GTK's own main loop - no new threading in a codebase that's
never needed any); **audio** - out of scope for this milestone
(`sound.c`'s current design has no incremental per-sample callback to
drive live playback from, and GTK4 has no built-in audio API of its
own - a separate, sizeable piece of scope for later).

**A real, honestly-flagged risk, not a reason to avoid starting**:
`cpm/gtk/`'s own GTK launcher is still intermittently blocked by a
confirmed macOS 26 OS bug - an allocator crash inside
`libsystem_malloc`'s "xzone" zone during `posix_spawn()` of a large,
many-dylib binary, matching a bug Apple's own engineers have
acknowledged on their Developer Forums (~2-3% of launches; see
`cpm/gtk/README.md`'s own citation). Not fixable from application code.
A framebuffer-based ABC80 window wouldn't need VTE/Pango/HarfBuzz at
all (just GTK4 + Cairo), which may make it somewhat less exposed simply
by linking fewer/smaller dylibs, but it's the same class of OS-level
risk either way, not something achievable to design around.

### Sub-step: extract a shared per-instruction step function — done

**Why first**: the CLI `--interactive` loop in `abc80/emu/src/main.c`
bundled together several `pc_before`-gated per-instruction checks that
have each been the site of a real, previously-fixed bug (keyboard
debounce timing - Milestone 3; the interrupt-interception hazard -
Milestone 7; the disk-trap sector/buffer bugs - Milestone 6). The
future `bin/abc80-gtk` needs the identical logic; duplicating ~250 lines
of carefully-derived, regression-tested correctness logic into a second
file would double the maintenance surface for exactly the kind of bug
this project has already paid real debugging cost to fix once.

Extracted into two new modules:
- **`abc80/emu/src/disk.c`/`.h`**: Milestone 6's floppy/DOS bypass
  (`abc80_disk_init()`, `abc80_disk_enabled()`, `abc80_disk_trap()`) -
  moved verbatim, not rewritten, with `abc80_disk_enabled`/
  `abc80_disk_file`'s old bare-static access replaced by clean accessor
  functions so the state stays properly encapsulated now that it's a
  separate translation unit.
- **`abc80/emu/src/step.c`/`.h`**: the actual per-instruction step
  (`abc80_step()`) - keyboard strobe consumption, sound-register write
  detection (both `OUT (n),A` and `OUT (C),r` forms), the disk-trap
  dispatch, and periodic PIO interrupt scheduling, all moved verbatim
  out of the CLI's own `while` loop. `main.c`'s loop now just calls
  `abc80_step(&cpu, ram, &sound_log, &total_cycles,
  &next_pio_interrupt_at)` once per iteration; keyboard byte reading
  (`poll_keyboard_byte()`/`abc80_keyboard_press()`) stays the caller's
  own responsibility, exactly as planned, since the CLI and the future
  GTK loop get key data from very different real sources.

`cpm/emu/src/main.c` needed no changes at all - confirmed by direct
inspection that its own loop has zero ABC80-specific logic (just a
plain `z80_step()` call), correcting an earlier draft of this plan that
assumed otherwise.

**Verified by real execution, not just a clean compile**: `make test`
passes unchanged; a real `--disk` `SAVE`/`LOAD` round trip
(`SAVE DR0:RCHK2`, then a fresh process `LOAD`/`LIST`s it back
correctly) and the original Milestone 3 `LEFT$`/`MID$`/`LEN`/`SIN`/`AND`
regression script both produce byte-identical output to before the
extraction.

### Sub-step: fold the PIO Port A alias sync into keyboard.c — done

Found while starting the GTK app itself: `sync_pio_port_a()` was still a
static function in `main.c`, called separately from the CLI loop right
before `abc80_step()` - the same duplication risk the previous sub-step
was meant to eliminate, just missed on the first pass. Moved the alias
table and both functions into `keyboard.c`/`.h` (where the PIO Port A
state they sync actually lives); `abc80_step()` now calls
`abc80_sync_pio_port_a()` itself as its first action, so callers don't
need to remember to call it separately at all. Verified the same way as
the previous sub-step: `make test` unchanged, the disk round trip and
BASIC regression script both still produce identical output.

### Sub-step: the real GTK app — working, real pixel rendering verified

`abc80/gtk/src/main.c` (new, mirrors `cpm/gtk/`'s own directory
convention) builds a real `GtkApplication`/`GtkDrawingArea` window,
duplicating only the small, low-risk ROM-loading code from `main.c`
(plain file I/O, not the kind of subtle logic the earlier extractions
were about) rather than exposing it from a file that has no header of
its own. `abc80_bus_read_hook()`'s floating-bus/RAM-expansion/disk-ROM
logic is likewise duplicated (a handful of address-range comparisons) -
see `abc80/gtk/README.md` for the full reasoning on what was and wasn't
worth sharing here.

**GRAPHICS-mode pixel geometry - grounded, not assumed**: a 6-pixel-wide
character cell splits evenly into two 3-pixel columns, but the 10-scanline
height doesn't divide evenly by 3. Fetched MAME's real
`src/mame/luxor/abc80_v.cpp` `draw_character()` directly rather than
guess at a split: `if (l < 3) r0 = 0; else if (l < 7) r1 = 0; else r2 =
0;` (`l` = scanline within the cell) - the three 2×3 mosaic rows are
scanlines 0-2 (top, 3 rows), 3-6 (middle, 4 rows), 7-9 (bottom, 3 rows),
confirmed from the real source rather than evenly dividing 10 by 3 and
hoping.

**Verified by a real screenshot, not just "it launched"**: built and ran
`bin/abc80-gtk` against the real committed ROMs (`macOS`'s own
`screencapture`, since this environment has no interactive display
access otherwise) and confirmed a real window titled "ABC80" showing the
ROM's own real sign-on banner rendered as genuine pixels - a blocky,
low-resolution letterform matching the real 6×10 chargen ROM scaled up,
*not* a Unicode sextant approximation - with a real solid cursor block
beneath it. This is the concrete deliverable this whole milestone exists
for, confirmed working, not assumed from a clean compile.

**Confirmed working by the user, hands-on**: keyboard input reaches
BASIC correctly in a real window session - the interactive gap this
project's own sandboxed environment couldn't close itself (no
Accessibility permission to script synthetic keystrokes) is closed by a
real human test instead.

**A real bug found on exit, fixed**: closing the window produced a
`Gtk-CRITICAL **: gtk_widget_queue_draw: assertion 'GTK_IS_WIDGET
(widget)' failed`. Root cause: the `g_timeout_add()` pacing timer was
never stopped when the window closed, so it could fire once more against
the drawing area after GTK had already started tearing it down. Fixed
with the standard GTK pattern for this: connect to the window's own
`"destroy"` signal, and have that handler call `g_source_remove()` on
the timer's saved source ID (plus clear `AppState.drawing_area` to
`NULL` as defense in depth - `on_timer_tick()` checks it first
regardless of whether the source removal already landed, in case a tick
was already queued before `"destroy"` ran). The halt-on-unimplemented-
opcode path also clears the saved source ID to `0` before returning
`G_SOURCE_REMOVE`, so a later window close can't call
`g_source_remove()` on an ID GLib already invalidated internally.
Couldn't mechanically reproduce the exact warning in this sandboxed
environment (no Accessibility permission to simulate clicking the close
button), so the fix itself relied on the pattern being standard/
well-understood rather than a before/after repro here - **confirmed
fixed by the user, hands-on**: the warning is gone on a real window
close.

### Sub-step: GRAPHICS-mode verified, plus a real SETDOT finding

To verify GRAPHICS-mode pixel rendering specifically (the one item the
above screenshot didn't cover - the boot banner is TEXT mode only), this
sandboxed environment's lack of Accessibility permission for synthetic
keystrokes meant `bin/abc80-gtk` needed a way to load a test BASIC
program without a human at the keyboard. Added optional,
`isatty(STDIN_FILENO)`-gated stdin scripted input to
`abc80/gtk/src/main.c` (`poll_stdin_byte()`, called from
`on_timer_tick()`), mirroring `abc80/emu/src/main.c`'s own identical
non-blocking `select()`-then-`read()` pattern exactly. Only activates
when stdin is piped/redirected - a real interactive session (stdin as a
tty) is completely unaffected, so this doesn't touch the already-
user-confirmed real GDK keyboard path at all.

First attempt (`SETDOT` in a `FOR` loop, drawing a box border) rendered as
garbled chargen-glyph text - `p`, `5`, `j`, etc. - instead of block
pixels, identically in *both* `bin/abc80-gtk`'s new Cairo renderer and
the pre-existing `bin/abc80 --interactive` terminal renderer
(`render.c`), confirmed by piping the identical input through both.
Identical behavior in both backends ruled out a GTK-specific rendering
bug immediately - whatever was wrong was upstream of both renderers, in
how the real ROM's `SETDOT` writes video RAM.

Root cause, confirmed by direct testing rather than guessed: `SETDOT`'s
real ROM routine only writes the target cell's dot-pattern byte - it
does *not* also write a `CHR$(151)` ("START GRAPHICS") marker byte into
the row first. Per MAME's own mode state machine (this file's
"GRAPHICS-mode pixel geometry" note above), a row's GRAPHICS/TEXT mode
is a persistent latch that resets to TEXT at the start of every row and
only changes when a byte with the right attribute bits is scanned - a
bare dot-pattern byte dropped into an still-TEXT-mode row renders
through the ordinary chargen path instead, which is exactly the garbled
text that appeared. This matches the `CHR$(151)` reference entry's own
wording ("starts graphics mode for **one line**") - it's a real,
faithfully-reproduced hardware behavior, not a bug in either renderer:
`SETDOT` alone, with no preceding `CHR$(151)` on that row, does the same
thing on real hardware. (A second, smaller real finding along the way:
`SETDOT`'s documented row range is `R: 0-72`, but `SETDOT 72,K` raised a
real `ERR 62` - the practical usable range is `0`-`71`, not `0`-`72` as
currently documented.)

Confirmed the fix for the *test program*, not the emulator: prefixing
each target row with `PRINT CUR(row,0);CHR$(151);` before its `SETDOT`
calls (`CUR(R,K)` moves the cursor to a given character row/column,
`ABC80_BASIC_REFERENCE.md`'s own documented API) made the identical box-
plus-diagonal program render as real sextant block glyphs in the CLI
backend with zero errors. Ran that same corrected program through
`bin/abc80-gtk` and captured a real screenshot (`screencapture -x`,
after bringing the window forward via `osascript`'s `tell application
"System Events" to set frontmost of process "abc80-gtk" to true` - the
one window-focusing AppleEvent this environment's permissions do allow,
unlike synthetic keystrokes): a genuine pixel box border and diagonal
line, built from real 2×3 sub-cell block-mosaic squares, not a Unicode
approximation and not garbled text. This is the last previously-open
verification item for Milestone 11 - GRAPHICS mode is now confirmed
working, using the exact same code path the TEXT-mode boot banner
already verified.

### Sub-step: `--quickload`/`--quicksave`, and a real perf check

Ported `abc80/emu/src/main.c`'s own cassette bypass to `bin/abc80-gtk`,
reusing `cassette.c`'s existing `abc80_cassette_quickload()`/
`abc80_cassette_quicksave()` unchanged (see cassette.h - Milestone 4)
rather than reimplementing anything. `--quickload` is injected from
`on_timer_tick()`'s per-instruction batch loop at the identical `PC ==
0x02AA` trigger point the CLI uses (the ROM's line-reading routine entry
- the one address confirmed safe against both the BOFA/EOFA boot-reset
race and the keyboard-read race, per that file's own extensive comment).

`--quicksave` needed a different trigger, though: the CLI has a bounded
instruction-count loop with a real end; this window instead runs until
closed, so `on_window_destroy()` (the same handler the earlier exit-bug
fix added) is this app's natural analogue of "end of run" and now flushes
`--quicksave` there. That exposed a real gap: without an explicit
`gtk_window_destroy()` (e.g. a plain `kill` from a script, or the host
system stopping the process some other way), a pending `--quicksave`
would silently never be written - the same way unplugging a real machine
would lose it. Added `g_unix_signal_add()` handlers for `SIGINT`/
`SIGTERM` that call `gtk_window_destroy()` on the real window, driving
the exact same `on_window_destroy()` path a close-button click would
(not a second copy of the save logic) - `g_unix_signal_add()`
specifically, not a raw `signal()` handler, since it delivers the signal
as a normal GLib main-loop callback, safe to touch GTK/Cairo state from.

**Verified by real execution, not just a clean compile**: quicksaved a
short typed program from `bin/abc80` (the CLI), quickloaded that same
file into `bin/abc80-gtk` (confirmed via a real screenshot: `LIST`/`RUN`
showed the exact program and correct output), then sent the GTK process
a real `SIGTERM` mid-run and confirmed the process exited cleanly with no
zombie and a real `.cas` file written. That file differed byte-for-byte
from the original CLI-saved file at first - traced to a real, benign
cause (not a bug in this new wiring): BASIC re-links each line's stored
"next line" address pointer on load, so a load-then-save round trip
naturally produces different bytes than a fresh save, even though the
program is functionally identical. Confirmed by reproducing the *same*
load-then-save round trip through the CLI itself and diffing: byte-for-
byte identical to the GTK output, proving the new `bin/abc80-gtk` wiring
behaves exactly like the CLI's own already-verified quickload/quicksave
path, not differently.

**Performance question resolved, empirically**: ran a worst-case test -
`SETDOT`-filling all 71×77 graphics dots across the entire 24×40 screen,
every cell in GRAPHICS mode at once, the densest possible per-pixel
`cairo_fill()` workload this renderer can produce - and watched real CPU
usage via `ps` while it ran: peaked around 22% of one core, comfortably
under the redraw budget at the existing ~30fps throttle. No glyph cache
needed; the original "not yet measured" open item is resolved in favor
of the simpler existing code, not added complexity.

### Sub-step: live cursor blink, ported from `--interactive`

`draw_screen()`'s cursor cell was solid/always-on from this milestone's
first working version onward (its own comment said so explicitly: "blink
not yet modeled, always shown solid"), unlike `--interactive`, which has
had a real, MAME-grounded `ABC80_BLINK_HZ` (3.125Hz) blink since
Milestone 8. User-reported: the GTK window's cursor doesn't blink like
the real machine's. Fixed by porting the identical mechanism
`render.c`/`main.c` already use rather than inventing a new one: computes
`fmod(elapsed_real, 1.0 / ABC80_BLINK_HZ) < (0.5 / ABC80_BLINK_HZ)` at
the same point `on_timer_tick()` already throttles redraws to ~30fps,
stores it in `AppState.cursor_blink_phase`, and `draw_screen()` gates the
cursor fill on it - the same `cursor && blink_phase` condition
`render.c`'s own `abc80_render_frame()` uses, just read from `AppState`
instead of a passed-in parameter (this renderer's draw function is a GTK
callback with a fixed signature, not called directly by the code that
computes the phase). Verified by real execution: three `screencapture`
frames taken ~0.2s apart at the idle `READY` prompt show the cursor
block solid, then absent, then solid again - a real on/off toggle at the
correct real-time cadence, not just a clean compile.

### Sub-step: `--amber`, an opt-in amber-phosphor palette

User-requested, explicitly *not* a claim about real ABC80 hardware - the
base ABC80 shipped with a white/green-ish monochrome display (this
project's own default rendering already models that, unchanged). The
Luxor ABC800, ABC80's direct successor, is well known for its amber CRT
option, which the user liked and asked for here. Added `--amber`,
swapping `draw_screen()`'s foreground color from white to `#FFB000` - a
commonly used amber-phosphor swatch (matching most terminal emulators'
own built-in "amber" themes) rather than a value sourced from an ABC800
hardware manual, since no primary source for the exact CRT phosphor
chromaticity was available; background stays black either way.
**Confirmed by the user, hands-on**: "it looks great."

### Sub-step: canvas margin and a File menu (Save/Load Program, Screenshot)

Automating `screencapture`/`osascript` against the user's real desktop
for verification (as the sub-steps above did) turned out to be
disruptive to their actual work - it steals focus and switches Spaces
while they're using the machine for other things. From here on, changes
to this app are verified by clean compilation, a non-visual smoke test
(launch, confirm no `Gtk-CRITICAL`/`Gtk-WARNING` output, confirm a clean
exit), and the user's own hands-on look, rather than an automated
screenshot.

**Canvas margin** - user-reported: the drawing area sat flush against
the window edges on every side, making the "monitor" look crammed into
its own frame. Fixed with a plain widget margin
(`ABC80_GTK_CANVAS_MARGIN`, 24px) applied to the `GtkDrawingArea` via
`gtk_widget_set_margin_*()` - real empty space outside `draw_screen()`'s
own `cairo_scale()`'d coordinate space, not part of the emulated picture,
centered within the window.

**File menu** - `Save Program…`/`Load Program…`/`Take Screenshot…`, a
real in-window `GtkPopoverMenuBar` built from a `GMenu` model (not
`gtk_application_set_menubar()`'s native-menu-bar integration, which
needs desktop-shell-specific settings to show anything at all on
X11/Wayland - the in-window bar is visible identically on every
platform). Directly answers a real gap the user ran into and asked about
first: BASIC's own interactive `SAVE`/`LOAD` hang forever, since real
cassette hardware isn't emulated (Milestone 4) - Save/Load Program
instead call the same `abc80_cassette_quicksave()`/
`abc80_cassette_quickload()` the CLI's own `--quicksave`/`--quickload`
flags already use, via the modern async `GtkFileDialog` (GTK 4.10+,
confirmed present in the installed 4.22.4) rather than the older,
deprecated `GtkFileChooserDialog`. `Take Screenshot` renders through the
identical `draw_screen()` the live window uses, against an offscreen
Cairo surface (`cairo_image_surface_create()`/`cairo_surface_write_to_png()`),
so the saved PNG always matches what's on screen rather than a second,
potentially-drifting implementation. `Load Program` shares `--quickload`'s
same real caveat: it overwrites `BOFA`..`EOFA` immediately regardless of
what BASIC is doing at that instant (safe from a memory-corruption
standpoint - menu actions and `abc80_step()` both run on the same GTK
main thread, never concurrently - but the natural place to use it is a
fresh/empty BASIC session, the same expectation loading a real cassette
program implies).

Verified via clean compilation (zero warnings) and a non-visual smoke
test: launched, ran for several seconds, confirmed no `Gtk-CRITICAL`/
`Gtk-WARNING` output, sent a real `SIGTERM`, confirmed a clean exit.
`make test` unaffected. **Confirmed by the user, hands-on**: "It works.
Load/Save too."

### Sub-step: a real `-h`/`--help`

User-reported: `--amber` was missing from `--help`'s own output. Root
cause wasn't a stale/incomplete flag list - this app never had a
dedicated `-h`/`--help` handler at all, unlike `bin/abc80`'s own
(`print_usage()`, called from `main()`'s own `-h`/`--help` case). An
unrecognized argument's terse one-line fallback message happened to be
the only "usage" text this app ever printed, and by the time this was
reported it already listed every flag correctly (generated from the same
source as the fallback, not actually out of date) - but there was no way
to see it without triggering what looked like an error, which is almost
certainly what actually happened. Added a real `print_usage()`,
mirroring the CLI's own style (one line per flag plus an indented
description) exactly, wired `-h`/`--help` to call it and exit `0`, and
had the unrecognized-argument path call the same function too instead of
keeping a second, separately-maintained copy of the flag list - the
actual root cause this bug traces to, fixed structurally rather than
just re-typing `--amber` into the old duplicate string. Verified by
direct execution (no display access needed for this one): `--help`/`-h`
both print the full flag list and exit `0`; an unrecognized flag prints
the same full listing after an "Unknown argument" line and exits `1`.

### Sub-step: real Ctrl-C break, and Cmd-Q/Cmd-S/Cmd-O

**Ctrl-C** - user-reported: it didn't break a running program the way it
does in `bin/abc80 --interactive`. Root cause: a real terminal's raw
mode pre-folds Ctrl-<letter> into a single control-code byte before
`--interactive`'s own `poll_stdin_byte()` ever sees it (see
`abc80/emu/src/main.c`'s own raw-terminal-mode comment), but GDK reports
the plain letter keyval plus a separate Control-modifier bit instead,
and `on_key_pressed()` was discarding that modifier state entirely
(`(void)state;`). Fixed by translating any Ctrl-<letter> chord to its
standard ASCII control-code byte (`Ctrl-A`-`Ctrl-Z` → `0x01`-`0x1A`,
checked before the existing plain-key switch) - real ABC80 hardware then
sees the identical `0x03` byte a real terminal's raw mode would have
produced for Ctrl-C, and Ctrl-X ("backspace the whole line," per
`ABC80_BASIC_REFERENCE.md`'s Keyboard section) now works for the same
reason.

**Cmd-Q/Cmd-S/Cmd-O** - user-requested app-level shortcuts for
quit/save/load. Bound via GTK's own portable `<Primary>` accelerator
modifier (`gtk_application_set_accels_for_action()`), which resolves to
Cmd on macOS and Ctrl elsewhere automatically, rather than a hand-rolled
`GDK_META_MASK` check - the idiomatic GTK4 approach, and it means
GtkApplication's own accelerator dispatch (which runs before a key event
would ever reach `on_key_pressed()`) can't interact with the
Ctrl-<letter> translation above at all (Cmd and Ctrl are different
modifier keys). `<Primary>S`/`<Primary>O` bind to the existing
`win.save-program`/`win.load-program` actions the File menu already
uses; `<Primary>Q` is a new app-level `app.quit` action (quitting isn't
really a per-window concept the way Save/Load are) whose handler
destroys the real window - the same `on_window_destroy()` path a
close-button click or `SIGTERM` already drives, so a pending
`--quicksave` still flushes from Cmd-Q exactly like every other exit
path, rather than a second copy of that logic.

Verified via clean compilation (zero warnings) and the same non-visual
smoke test as the prior sub-step; the Ctrl-C/Ctrl-X translation and the
Cmd-Q/S/O accelerators themselves need the user's own hands-on keyboard
to confirm, the same as every other real GDK-input change in this
milestone.

### Sub-step: an invisible canvas margin

Follow-up to the canvas margin above - user-reported: the margin showed
the default GTK theme background, a different color from the canvas's
own black, so it read as a visible border around the "screen" rather
than blending in. Fixed with a small `GtkCssProvider`
(`.abc80-canvas-area { background-color: black; }`) applied to
`layout_box`, the container `menu_bar`/`drawing_area` both sit in - both
children are opaque and paint over their own allocated area regardless,
so the CSS background only actually shows through in the margin area
itself. Always black, not conditional on `--amber`, since
`draw_screen()` never touches the background, only the foreground
palette. Verified via clean compilation and the same non-visual smoke
test as the prior sub-steps, run once in each palette (default and
`--amber`) - the visual result itself needs the user's own hands-on
look, same as every other real GDK-rendering change in this milestone.

### Sub-step: a Colors menu (text/background/border, via GtkColorDialog)

User asked whether GTK had a built-in color-picker dialog before
requesting this - it does: `GtkColorDialog` (GTK 4.10+, confirmed
present in the installed 4.22.4), the same async-dialog family as
`GtkFileDialog` already in use for Save/Load/Screenshot, with a real
palette grid plus custom RGB/hex entry built in, no custom picker UI
needed. Added a `Colors` menu (`Text Color…`/`Canvas Background
Color…`/`Border Color…`), each opening the dialog seeded with that
setting's current value.

Replaced the fixed `--amber` boolean with three `GdkRGBA` fields on
`AppState` (`text_color`/`canvas_bg_color`/`border_color`) so all three
are freely repickable at runtime rather than a fixed white/amber choice.
`--amber` still works as a launch-time convenience, now just seeding
`text_color`'s starting value. `draw_screen()` reads `text_color`/
`canvas_bg_color` directly instead of the old hardcoded white/black
constants; `border_color` feeds `update_canvas_css()`, which reloads the
same `GtkCssProvider` the earlier canvas-margin fix introduced (in
place, not re-registered) so the margin can independently blend with or
contrast against the canvas.

Verified via clean compilation (zero warnings on the first attempt) and
the same non-visual smoke test as every other sub-step here, run once
in default and once in `--amber`. The dialogs' own behavior and the
resulting colors need the user's own hands-on look, the same as every
other real GDK-rendering change in this milestone.

### Sub-step: menu text stayed legible regardless of Border Color

User-reported: picking a dark Border Color made the menu bar's own text
illegible (dark text on a now-black menu background) - they asked
"Does the menu have to follow the color of the border?" Root cause: the
`.abc80-canvas-area` CSS class from the canvas-margin sub-step above was
applied to `layout_box`, the outer container holding *both* `menu_bar`
and `drawing_area` - `GtkPopoverMenuBar`'s own default styling doesn't
give it a fully opaque background in at least one theme, so
`layout_box`'s custom background painted in behind the menu bar too,
fighting the menu's own text color (themed for a normal light/gray menu
background, not whatever the user just picked for the ABC80 canvas).
Fixed by scoping the CSS class to a new `canvas_wrapper` box holding
*only* `drawing_area`, as `menu_bar`'s sibling rather than its cousin -
`border_color` now can't reach anywhere near the menu bar's own
rendering, regardless of what color it's set to. Verified via clean
compilation and the same non-visual smoke test as every other sub-step
here; the actual visual fix needs the user's own hands-on look.

### Sub-step: Save Preferences (persisting the Colors menu)

User-requested: persist the three Colors-menu settings so they don't
reset on the next launch. `Save Preferences`, a new item at the bottom
of the Colors menu (in its own visually separated `GMenu` section),
writes `text_color`/`canvas_bg_color`/`border_color` to a `GKeyFile` at
`$XDG_CONFIG_HOME/abc80-gtk/prefs.ini` (`g_get_user_config_dir()`'s own
cross-platform resolution - `$HOME/.config` when `$XDG_CONFIG_HOME`
isn't set, on every platform GLib supports, not a hand-rolled
macOS-specific path). Each color round-trips through a single string
via `GdkRGBA`'s own `gdk_rgba_to_string()`/`gdk_rgba_parse()`, so no
custom serialization format was needed.

No separate "Load Preferences" menu item - `load_preferences()` runs
automatically once at startup, before `--amber` can override it (so an
explicit launch-time flag still wins over a saved preference), which is
the actual point of something being a *preference* rather than a
one-off action repeated by hand every launch. Verified via two
non-visual smoke tests: a normal launch with no `prefs.ini` present
(the common first-run case - the loader fails gracefully, defaults
apply, no warnings) and a second launch against a hand-written
`prefs.ini` in the real GKeyFile format (confirms the loader doesn't
warn/crash on genuine input, cleaned up afterward rather than left for
the user's next real launch). The actual Save Preferences menu click,
and confirming picked colors really do return on the next launch, needs
the user's own hands-on look.

### Sub-step: live audio, closing this milestone's last deferred item

User asked, once the rest of the window was solid, whether it was time
to add sound - the one item deliberately deferred since this milestone's
own original scoping decision. Scoped first via a written plan (see
`~/.claude/plans/mellow-cooking-parrot.md`, reused for this sub-step the
same way it was for the original GTK-window scoping) since it's this
app's first real architectural change: every feature so far ran
entirely on GTK's own main-loop thread, and real-time audio needs its
own callback thread. Added SDL2 (`sdl2`, confirmed available via
Homebrew's `sdl2-compat`, an actively-maintained SDL2-API shim over
SDL3) to `bin/abc80-gtk`'s own dependencies only, via the same
`pkg-config`-derived `ABC80_GTK_CFLAGS`/`ABC80_GTK_LIBS` pattern already
used for `gtk4` - `bin/abc80` stays SDL2-free, its own batch `--wav`
flag unchanged.

Plays the exact same one case `--wav` already models (Milestone 5): a
steady 640Hz square-wave VCO tone whenever port `0x06` selects it,
silence otherwise - no new modeled cases. `sound.c`'s previously
file-private gating decode is now public
(`abc80_sound_is_steady_vco_tone()`), and a new
`abc80_sound_live_sample()` generates one sample via an incremental
running phase (the standard real-time-audio technique to stay
click-free across buffer boundaries) - `abc80_sound_render_wav()` itself
was not touched or refactored to share this, zero regression risk to
the already-working `--wav` path. The one new concurrency surface this
introduces, deliberately kept as narrow as the plan called for: a
single `_Atomic uint8_t` on `AppState` (`live_sound_register`), written
once per timer tick by the main thread and read by SDL's own real-time
audio callback - no queues, no locks.

**Verified two different ways, honestly split**: the new
`abc80_sound_live_sample()` math is checked the same rigorous way
Milestone 5 already checks the batch renderer - a new `bin/abc80-sound-
demo --live` mode generates the same silence→tone→silence sequence
sample-by-sample through the live path, and zero-crossing frequency
analysis measured **640.02Hz against the 640.00Hz prediction** (0.003%
error, in the same range as the batch renderer's own 639.39Hz/639.95Hz
measurements from Milestone 5), both silence segments confirmed at 0
RMS. Real execution was also confirmed clean - launched, ran BASIC that
actually executes `OUT 6,64`/`OUT 6,1` to toggle the tone live, no SDL
error output, no crash, clean `SIGTERM` exit - proving `SDL_Init`/
`SDL_OpenAudioDevice` genuinely coexist with GTK's main loop rather than
just assuming so. What none of this can verify: whether it actually
*sounds* right - confirming a real audible tone, clean start/stop, and
correct pitch is the user's own hands-on job, the same honest split
already used for every perceptual (visual) change in this milestone,
just audio instead of video this time.

With this, Milestone 11 had no remaining open items - the sub-step below
is a genuine new feature added afterward, not a gap being closed.

### Sub-step: plain-text `.bas` Save/Load, alongside tokenized `.bac`

User-requested: the File menu's Save/Load Program only handled `.bac`
(BASIC's own tokenized/compressed format); real ABC80 BASIC's own `LIST
filename` also saves *uncompressed* plain text
(`ABC80_BASIC_REFERENCE.md`), which the user wanted supported too. Both
dialogs now offer `.bac`/`.bas` as `GtkFileFilter` choices (confirmed
with the user: one menu item each, extension picks the format, over
separate BAC/BAS menu entries) - whichever extension is chosen/typed
picks the format, `.bac` remaining the default for a bare filename.

**Why this needed real design work, not just a second file-format
branch**: `.bac` works by copying BASIC's own tokenized `[BOFA, EOFA)`
bytes verbatim - no token decoding needed at all. `.bas` is a genuinely
different problem: those bytes are opaque single-byte-tokenized
keywords, not ASCII, so producing real source text means either
reverse-engineering the full token table or getting the real ROM to do
the detokenization - the same "let the real ROM/DOS do the work at the
I/O boundary" principle this project has used successfully for SAVE/
LOAD, the disk bypass, and BDOS/BIOS interception throughout. Scoped via
a written plan first (reusing `~/.claude/plans/mellow-cooking-parrot.md`
again), grounded in two facts confirmed via direct execution before any
code was written, not assumed:
- `LIST n-n` (a range with identical start/end) lists exactly one line -
  confirmed against a real 4-line typed program in
  `bin/abc80 --interactive`. This is what makes per-line listing
  scroll-proof regardless of program length, unlike a bare `LIST`
  (which the ROM does clear the screen for first, but then scrolls
  through normally for a long program, losing earlier lines from view).
- `PRINT CHR$(12)\rLIST n-n\r` lands the command's echo and the listed
  line's own text on fixed, repeatable video RAM rows (3 and 4,
  0-indexed) every time, confirmed against three different line numbers
  in sequence via a programmatic frame-by-frame parse of the CLI's own
  terminal output, not eyeballed.

**Implementation** (`abc80/gtk/src/main.c`): `extract_line_numbers()`
walks `[BOFA, EOFA)` directly - the tokenized-line framing
(`[length byte][line number, little-endian][...tokens...][0x0D]`, length
including itself) was already documented from Milestone 4's own
investigation, not new reverse-engineering, but the walk itself was
still verified byte-by-byte against a real quicksave's raw bytes (a
4-line test program, hand-decoded offset by offset) before being
trusted. `build_bas_text()` drives `LIST n-n` per extracted line number
via `inject_line()` (the same `abc80_keyboard_press()`/
`abc80_keyboard_ready_for_next()` pair `on_key_pressed()` and the
stdin-scripting path already use, pumped synchronously via `abc80_step()`
rather than paced by `on_timer_tick()`'s real-time loop - a one-shot
bounded batch, the same style `--quickload`'s own trigger already works
within) and reads the result back via `capture_row_text()`, using the
identical, already-verified `abc80_charset_codepoint()` decode
`render.c`'s own `abc80_render_frame()` uses for TEXT mode - real UTF-8
output for ABC80's Swedish-alphabet substitutions (Å/Ä/Ö/etc.), not just
ASCII. `load_bas_text()` reverses this the low-risk way: no RAM-walking
or video capture needed, just re-typing each line (UTF-8-decoded back to
the correct ABC80 character byte via a small reverse table) through the
same real keyboard path a human retyping a printed listing on real
hardware would use - there's no ROM routine that ingests raw text as a
data blob, so this *is* the real mechanism.

**Verified via a real, comprehensive headless test** (temporarily added
to `main()` behind an env var, removed after confirming - none of
`inject_line()`/`build_bas_text()`/`load_bas_text()` touch any GTK
widget, only `AppState.cpu`/`.ram`/keyboard.c state, so this needed no
actual window): a 4-line program typed in, saved as text, reloaded after
`NEW`, and re-saved again produced byte-identical output; the reloaded
program was separately confirmed to actually `RUN` correctly (not just
`LIST` identically - real executed output, not just matching source
text); and a line containing Swedish Å/Ä/Ö round-tripped correctly
through real UTF-8 encode/decode. `make abc80-gtk`/`make test` both
clean; the usual non-visual smoke test (no `Gtk-CRITICAL`/
`GLib-CRITICAL` output, clean `SIGTERM` exit) unaffected.

**Known, stated limitation, not silently mishandled**: a source line
wider than 40 columns would wrap onto a second video row this doesn't
capture. Real menu-click UX (does the save feel responsive for a
reasonably-sized program? does the picked format/extension behave as
expected in the file dialog?) needs the user's own hands-on look, the
same honest split as every other GTK-input change this session.

### Sub-step: `--turbo N`, a command-line speedup flag

User-requested, after confirming `bin/abc80-gtk`'s real-hardware pacing
was working correctly rather than being slow by accident: the new
`abc80/examples/graphics_demo.bas` (added the same session, see above -
box border plus a `SIN`/`COS` circle) genuinely takes ~16-20 real
seconds to finish drawing at real speed, confirmed by bisecting
`bin/abc80`'s own batch-mode instruction cap against when the post-draw
caption appears in its final render (47.5M T-states: not yet drawn; 58.5M
T-states: drawn) - real 8-bit BASIC floating-point trig in a loop was
genuinely this slow on the genuine 1978 hardware. Wanted as a real
command-line flag, not a menu toggle - scoped via a written plan first
(`~/.claude/plans/mellow-cooking-parrot.md`), confirming with the user
which of two designs to build: a multiplier (`--turbo N`, chosen) versus
an uncapped "as fast as the host can go" boolean flag (rejected - would
blast a demo like this to its end state in well under a second on a fast
host, defeating the point of watching it draw).

**Implementation** (`abc80/gtk/src/main.c`): `on_timer_tick()`'s existing
real-time pacing loop already computed `target_cycles = elapsed_real *
ABC80_CLOCK_HZ` (2,995,200Hz, the real Z80 clock) every 5ms tick, plus a
`max_cycles_this_tick` catch-up bound for stalled ticks - both are now
multiplied by a new `app->turbo_multiplier` field (`1.0` default,
i.e. today's behavior unchanged with no flag). Nothing else needed
touching: the PIO interrupt period and every in-program BASIC/machine-
code timing loop are already driven by `total_cycles` (real emulated
T-states), so they speed up correctly right along with the CPU - exactly
how a real, faster-clocked ABC80 would behave, not something turbo mode
has to special-case. Cursor blink is already computed from `elapsed_real`
directly, independent of `total_cycles` - correctly unaffected by turbo,
since blink is a separate real hardware timer on the real board. The
30fps redraw cap is unaffected too - turbo's visible effect is each
redraw reflecting far more emulated progress, i.e. the picture visibly
fast-forwarding, not a higher redraw rate.

**Live audio is disabled whenever turbo is active** (`SDL_Init`/
`SDL_OpenAudioDevice` skipped entirely, `--turbo`'s own `print_usage()`
entry says so, plus a startup line saying so at launch) - a deliberate,
documented tradeoff rather than shipping something that would sound
broken and imply it's accurate. `live_sound_register` is only updated
once per unchanged 5ms GTK tick, sampled by the audio thread at a real
44.1kHz - already a real, accepted aliasing tradeoff at 1x speed (see
Milestone 11's live-audio sub-step above). At a turbo multiplier, far
more emulated tone changes land inside that same unchanged 5ms window,
so most of a program's real sound events would get silently aliased
away - materially worse than 1x, not a faithfully-sped-up sound. Muting
says nothing rather than something misleading.

`--turbo N` follows the file's existing argument-taking-flag pattern
(`--disk FILE`/`--quickload FILE`/`--quicksave FILE`), parsed via
`strtod()` with an endptr check - the first numeric flag in this file
that needs real validation (every other flag here is a bare boolean), so
it rejects anything unparseable or `<= 0` with a clear error plus
`print_usage()`, rather than silently doing something undefined.

**Verified via direct execution, not just written and assumed correct**:
non-visual smoke test (no `Gtk-CRITICAL`/`GLib-CRITICAL`, clean
`SIGTERM` exit) at `--turbo 1`, `--turbo 4`, and confirmed `--turbo 0`/
`--turbo abc`/`--turbo -3` are all correctly rejected. Real pacing
accuracy verified with a temporary `getenv`-gated debug hook in
`on_timer_tick()` (added, exercised, then fully removed - the same
discipline already used twice this session for headless `.bas`
verification, confirmed via a clean `git diff` afterward showing no
trace of it): measured real `total_cycles` growth per wall-clock second
at `--turbo 1` (2,995,201 cycles/real-sec - matches `ABC80_CLOCK_HZ`
almost exactly) versus `--turbo 5` (14,976,002 cycles/real-sec - exactly
5.0000x the 1x rate, not approximately). `make abc80-gtk`/`make test`
both clean. Real menu-click UX and whether the sped-up drawing actually
*feels* right needs the user's own hands-on look, the same honest split
as every other GTK-input/perceptual change this session.

### Sub-step: full SN76477 emulation (SLF, noise, one-shot, envelopes)

User-requested, after asking about the real SN76477 chip's capabilities
beyond the fixed-pitch beep `sound_demo.bas` already exercised: the real
board's R/C values for its other four subsystems (SLF, noise, one-shot,
attack/decay envelope) were grounded from MAME's real `abc80.cpp`/
`sn76477.cpp` source (fetched from `mamedev/mame` on GitHub) earlier
this session (`ab00a74`, `ABC80_REFERENCE.md`'s Sound section) - this
sub-step turns that grounding into a real implementation, scoped via a
written plan first (`~/.claude/plans/mellow-cooking-parrot.md`).

**Architecture**: a new `Abc80SoundState` struct + `abc80_sound_step_sample()`
(`sound.h`/`sound.c`) ports MAME's own `sound_stream_update()` per-sample
body - every subsystem is an independent RC charge/discharge integrator
against fixed voltage thresholds, with one real coupling (SLF-swept VCO
mode: the VCO's own charging ceiling tracks the SLF's current cap
voltage rather than a fixed value). This **replaces**
`abc80_sound_live_sample()` and unifies what were previously two
independent VCO-only implementations (`abc80_sound_render_wav()`'s
absolute-time phase math, and the live callback's incremental-phase
math) into one shared function both now drive - a real architecture fix
`abc80_sound_render_wav()` needed anyway once subsystem state became
genuinely history-dependent (a one-shot's running flip-flop, a cap
mid-charge) rather than a pure function of absolute time.

**Two real corrections found by reading MAME's actual source rather
than assuming, each significant enough to be worth recording**:

- **The one-shot doesn't trigger on writing `envelope_mode==1`** - a
  first-draft heuristic assumption, self-caught before it was ever
  trusted. Reading MAME's own `enable_w()` in full: the real one-shot
  triggers (and the attack/decay cap resets) on the **enable bit's
  0→1 transition specifically** (bit0 going from enabled to disabled) -
  "one-shot runs regardless of envelope mode" per that function's own
  comment. Real software fires a one-shot pulse by writing an enabled
  register value, then a disabled one, then re-enabling - not by
  writing `envelope_mode==1` and leaving it there. `sound.c` now tracks
  the previous call's bit0 to detect this real edge.
- **The VCO's own ceiling formula needed a special case for the fixed
  (non-swept) pitch this board actually uses.** MAME's literal
  `vco_cap_voltage_max = m_vco_voltage + VCO_TO_SLF_VOLTAGE_DIFF`
  formula, applied at this board's natural `vco_voltage=0`, gives a
  ceiling of just 0.35V - confirmed by direct testing to swing the cap
  in under one 44.1kHz sample, an inaudible near-Nyquist buzz, not
  640Hz. This directly contradicted the project's own already-verified
  (real ROM execution, FFT-checked) 640Hz figure, so - rather than trust
  a transcription that couldn't be reconciled after repeated re-reads -
  `sound.c` special-cases `vco_voltage<=0` to use the full
  `VCO_CAP_VOLTAGE_MAX` ceiling instead, anchored to the already-verified
  closed form. ABC80's board only ever drives `vco_voltage` to 0 or 2.5
  (never a continuous range - MAME's own general formula is built for
  hardware that can), so this two-value special case is complete for
  every register value this hardware can actually produce; 2.5V still
  correctly saturates silent via the existing ceiling-exceeds-max gate,
  unaffected by this.

**One real bug caught by the regression check itself, before it shipped**:
the first draft returned `0` for the mixer's "off" half-cycle instead of
`-amplitude`, producing a unipolar (0/+8000) square wave instead of the
original bipolar (±8000) one - audibly different, and not what MAME's
own `out_pos_gain`/`out_neg_gain`-around-a-center-voltage model does
either. Caught immediately by the very first regression render (raw
samples alternating `8000, 0, 8000, 0...` instead of `8000, -8000...`),
fixed before moving on to the new modes at all.

**Known, honest side effects of switching to a real per-sample model**,
not silently absorbed: the VCO's own measured frequency shifted from the
old continuous-phase implementation's ~639.95Hz to **~629.5-630.0Hz**
(confirmed via both a Goertzel-based frequency measurement and simple
zero-crossing counts) - a real, ~1.6% consequence of genuine 44.1kHz
per-sample discretization (each cap-voltage step either clamps exactly
to the ceiling or doesn't, so the simulation consistently rounds to 35
samples per half-cycle rather than averaging 34.44) that MAME's own
real algorithm would exhibit too, not a bug introduced here - `f =
0.64/(R×C) = 640Hz` remains the correct *ideal* closed form (unchanged
in `abc80_sound_vco_freq_hz()`, still used for the printed reference
figure), just not bit-exact to what a real discretized simulation at
this sample rate produces. Separately, alternating-polarity envelope
mode (`envelope_mode==3`) was found to settle into a steady near-max
amplitude rather than audibly swinging, once implemented and measured -
also not a bug: the underlying ~630Hz VCO oscillation toggles `ad_charging`
on/off roughly every 0.75ms, far faster than the ~22ms attack/~470ms
decay time constants can respond to, so the fast-charge/slow-decay
asymmetry keeps the envelope pinned near max under that rapid
micro-toggling - confirmed the underlying oscillation itself is still
genuinely present and correctly alternating in sign throughout.

**Verified via direct execution at every stage, not assumed correct
from the formulas alone**:
- Regression: `bin/abc80-sound-demo` (batch and `--live`) still
  produces the exact same envelope timing (silence/tone/silence at
  0.5s/1.0s/0.5s) as before this change, confirmed via WAV analysis
  before trusting any new mode.
- Each new mode independently verified via `bin/abc80-sound-demo --mode
  {slf,vco-swept,noise,one-shot,alt-polarity}` (new flag) and a Python
  WAV-envelope/frequency analysis script: SLF alone shows a clean
  120.4ms/130.8ms alternating toggle (predicted ~3.98Hz, confirmed
  exactly); VCO swept by SLF shows a genuine sweeping dominant frequency
  (650Hz-3300Hz, cycling at the SLF's own ~250ms period, not a fixed
  tone); noise alone shows highly irregular zero-crossing intervals
  (4-46 samples, stdev 4.6, vs. a clean tone's near-identical intervals
  every cycle); one-shot shows a clean ~20-25ms attack rising to full
  scale then a ~470ms decay tail, matching the predicted attack/decay
  constants almost exactly; alternating polarity's underlying ~630Hz
  oscillation confirmed present (see above for why the amplitude
  envelope itself doesn't audibly swing).
- **Real ROM-driven cross-check**, mirroring how the original VCO figure
  was cross-checked against both a synthetic sequence and real ROM
  execution: a small BASIC test program (`OUT 6,80` for SLF,
  `OUT 6,72` for noise, real delay loops between) run through
  `bin/abc80 --wav`, with zero `ERR` lines. The real ROM-driven SLF
  segment measured **120.4ms/130.8ms alternating - byte-for-byte
  identical** to the synthetic test's own numbers; the real noise
  segment's interval statistics (4-32 samples, mean 9.2, stdev 4.4)
  closely matched the synthetic noise test's (4-46, mean 9.3, stdev
  4.6). Incidental finding along the way: the real ROM appears to emit
  its own brief keyboard-click tone through the SN76477 while a program
  is being typed, visible in the WAV as activity before the test
  program's own first `OUT` statement ever executes - not investigated
  further (out of scope here), but worth knowing when reading a
  real-ROM sound capture's own timeline.
- `make abc80 && make abc80-gtk && make test` all clean; the usual
  non-visual smoke test (no `Gtk-CRITICAL`/`GLib-CRITICAL`, clean
  `SIGTERM` exit) unaffected.

**Explicitly not attempted**: MAME's exact analog output-stage gain-table
curve (`out_pos_gain`/`out_neg_gain`, `center_to_peak_voltage_out()`) -
a linear `attack_decay_cap_voltage`-fraction scaling is used instead
(full scale unchanged for Mixer-Only mode), consistent with this
model's pre-existing "plain square wave, not the real analog output
amplifier" simplification stance rather than a new one introduced here.
Whether the new modes actually *sound* right - real audible warble,
noise character, percussive punch - needs the user's own hands-on
listen, the same honest split as every other perceptual change this
session; what's verified here is that the real register-toggle timing
and spectral character match their hand-derived, MAME-grounded
predictions, not that a human has confirmed it's pleasant to hear.

### Sub-step: fixing a continuous boot-time tone the fuller model exposed

User-reported, immediately after the sub-step above: `bin/abc80-gtk`
played a continuous tone from launch that never stopped. Root-caused
via a temporary debug hook logging every real port-`0x06` write (added,
used, then fully removed): the real ROM writes to that port exactly
**once** during its own boot sequence - `data=0x00` at `PC=0x0098`,
T-state 223 - and never again during idle. Confirmed independently via
`bin/abc80 --wav` with *no* keyboard input at all: 100% nonzero samples
across a 77-second render before the fix, 0% after.

`0x00` decodes (correctly, per the real bit layout) to enabled=true,
mixer=VCO, `envelope_mode==0` ("VCO" - envelope tracks the VCO's own
flip-flop directly) - a real, valid SN76477 configuration that this
board's actual R/C values (fast ~1.5ms VCO cycle vs. slow ~470ms decay)
ramp up to full volume over roughly 50-150ms and then hold indefinitely,
the identical "fast-charge/slow-decay pins it near max" mechanism
already found and documented for alternating-polarity mode above - not
a new bug in that logic, just a second place the same real effect shows
up. The previous, narrower VCO-only model never produced audio for
`envelope_mode!=2` at all, so this boot write was always silently
harmless before - a real regression this fuller, more faithful model
newly exposed, not introduced by it.

Real ABC80 hardware humming continuously forever after every boot,
undocumented in any of this project's own extensive primary-source
research (service manual, BASIC manual, MAME driver, hobbyist forums),
was judged implausible for a shipped product - almost certainly this
write is a generic "clear the port" boot step with no real sonic
intent, not a genuine hardware quirk to faithfully reproduce. Fixed by
treating literal byte `0x00` as the same "nothing real written yet"
sentinel `0x01` already serves elsewhere in this codebase (the WAV
renderer's own log-empty default, live audio's own pre-first-tick
default) - silence for this one specific, non-intentional value, with
no change to genuine `envelope_mode==0` behavior for any other register
byte a real program might deliberately write.

**Verified**: the CLI's own no-input boot-only WAV render confirmed
silent (0/3,407,965 nonzero samples, previously 3,407,931/3,407,965).
Re-ran every existing check afterward to confirm nothing else moved -
the VCO-only regression sequence's envelope timing, the SLF mode's
120.4ms/130.8ms intervals, and the one-shot pulse's attack/decay shape
all came back byte-for-byte identical to their pre-fix values, since
the fix only special-cases the single literal value `0x00` and none of
those test sequences ever use it. `make test` and the usual non-visual
GTK smoke test both clean.

### Sub-step: Swedish character keyboard mapping (Å/Ä/Ö/Ü/É and lowercase)

Milestone 3's own "known gap" note and this file's "Planned next steps"
section both framed this as wiring up "the real Swedish scan-matrix
layout." That framing turned out to be a misnomer once actually
investigated: the ABC80's real keyboard scan-matrix PROM
(`abc80-keyboard.bin`, N82S141) isn't dumped or documented anywhere in
this repo, and MAME doesn't emulate it either — its `abc80_common()`
machine config wires a generic host-ASCII-keyboard device straight to
`kbd_w()`, the identical byte-plus-strobe forwarding
`abc80_keyboard_press(uint8_t ascii)` already does (see Milestone 3 and
`keyboard.c`'s own top comment). That's the correct, well-precedented
simplification to keep, not a gap.

The real gap was narrower: `abc80_keyboard_press()` takes a single 7-bit
ASCII byte, and neither consumer converted a host Å/Ä/Ö/Ü/É keystroke
into the correct ABC80 character-set byte for it first. In
`bin/abc80-gtk`, `on_key_pressed()`'s fallback only accepted GDK keyvals
in `[0x20, 0x7E]`, so these keys were silently dropped outright (GDK
keyvals equal Unicode codepoints for the Latin-1 range, and Å/Ä/Ö/Ü/É's
codepoints fall outside that window). In `bin/abc80 --interactive`, a
real terminal sends these as 2-byte UTF-8 sequences, which
`poll_keyboard_byte()` didn't decode at all — the two bytes would have
arrived as two separate, wrong keystrokes instead.

The target byte values needed no new research: `charset.c`'s
`abc80_charset_codepoint()` already documents ABC80's real TEXT-mode
character set (the Swedish/Finnish ISO 646 variant, SEN 850200 Annex B)
byte↔Unicode mapping, and `bin/abc80-gtk` already had an independently
written *reverse* table (`unicode_codepoint_to_abc80_char()`, used only
by the `.bas` file-load path) with the identical 10 mappings. Added the
inverse as a shared `abc80_charset_byte_for_codepoint()`
(`charset.c`/`.h`) instead of a third copy, and wired both consumers
through it:

- `bin/abc80-gtk`'s `on_key_pressed()` now falls back to the shared
  lookup for any keyval outside the plain-ASCII range; `.bas` loading's
  `unicode_codepoint_to_abc80_char()` became a thin wrapper around the
  same function (its own `'?'`-for-unmapped fallback behavior unchanged).
- `bin/abc80 --interactive`'s `poll_keyboard_byte()` gained a second,
  parallel state machine alongside its existing ESC/CSI arrow-key
  recognizer — buffer a 2-byte UTF-8 lead byte (`0xC2`-`0xDF`), wait
  (same timeout) for its continuation byte, decode the codepoint, and
  look it up via the same shared function. Only 2-byte sequences needed
  handling, since ABC80's entire character set lives in the Latin-1
  Supplement block.

**Verified against real ROM execution**: piping a UTF-8-encoded
`PRINT "ÅÄÖ åäö"` into `bin/abc80` produced a final screen showing the
ROM echoing that exact line as typed, then evaluating the string literal
and printing it correctly on the next line — a genuine round trip through
UTF-8 decode → ABC80 byte → keyboard press → ROM tokenizer → BASIC string
evaluation → video RAM → back to UTF-8 for terminal display, not just a
byte-level unit check. Re-ran the existing plain-ASCII (`PRINT 1+1`) and
arrow-key (backspace-and-retype via `ESC [ D`) regression cases
afterward and confirmed both unaffected.

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
  sub-step), the real periodic PIO interrupt (Milestone 7), real
  interactive keyboard input with a live, real-time-paced screen including
  a genuine Ctrl-C break to BASIC and real left/right arrow keys
  (Milestones 8-10, `bin/abc80 --interactive`) are done. Floppy/DOS
  controller support (Milestone 6's remaining second half) is also done:
  a working `--disk` bypass boots the real, unmodified ABC-DOS ROM and
  supports genuine `SAVE`/`LOAD` round trips against real ABC80 disk
  images, verified across independent process runs (including a
  multi-block file, byte-for-byte) - see that sub-step's own write-up
  above for the full derivation. `UFD-DOS` (the alternate real DOS ROM,
  `UFD80V20.bin`) has been examined and compared but not wired up - a
  genuinely more general multi-drive-type driver, not needed now that
  `ABC-DOS` itself works. Disk-full behavior (both the directory-capacity
  and block-space-exhaustion cases) is confirmed correct and safe. The
  exact meaning of `B`'s unused bits is the one narrower item still
  open - also covered in that write-up.
  Cassette storage is a host-file bypass of BASIC's own program-storage
  pointers, not real analog tape emulation. Sound was originally a single
  steady-tone case only, but Milestone 11's "full SN76477 emulation"
  sub-step closed that gap: SLF/noise/one-shot/envelope modes are all now
  synthesized, and live audio (via GTK/SDL, not just `--wav` rendering) is
  supported too — see that sub-step's own write-up.
- **Cursor blink is now live** (Milestone 8) in `--interactive` mode,
  computed from real elapsed time against the real 3.125Hz rate MAME's own
  `m_blink_timer` uses. Default (non-`--interactive`) mode is still a
  one-shot end-of-run snapshot with `blink_phase=1` hardcoded, unchanged -
  a deliberate difference between the two modes' purposes, not a gap.
- **Real Ctrl-C break now reaches BASIC** (Milestone 9, closing the gap
  Milestone 8 left open) - see that milestone's own write-up below.
- **Memory-map fidelity for `0x4000`-`0xBFFF`**: fixed by Milestone 6's RAM
  expansion sub-step (see above) — `0x4000`-`0x7BFF` and, by default,
  `0x8000`-`0xBFFF` now correctly float (fixed `0xFF` reads, matching MAME's
  own no-card `abcbus_slot_device` behavior) instead of being ordinary flat
  RAM, except for `0x6000`-`0x6FFF` when `--disk` is active (the real DOS
  ROM). `0x8000`-`0xBFFF` still has no real printer/IEC ROM card content —
  out of scope, no milestone currently targets those cards.
- **ROM write-protection**: `0x0000`-`0x3FFF` is writable in this model,
  matching this repo's existing flat-memory-model precedent for the CP/M
  target (`CLAUDE.md`'s Architecture section) rather than a new abstraction
  introduced early. No milestone yet — revisit only if something concrete
  needs it, same standard `cpm/docs/ROADMAP.md` applies elsewhere.

## Planned next steps

None currently — the last item tracked here (the floppy/DOS controller's
remaining loose ends: `B`'s unused bits, `L608F`'s failure paths, the
`disk003.img` commit decision) was closed out in Milestone 6's own
"closing the three remaining loose ends" sub-step above.

## Sources consulted

- MAME mainline driver: `src/mame/luxor/abc80.cpp` (memory map, I/O map,
  ROM filenames/CRC32 checksums, machine configuration), fetched from
  <https://raw.githubusercontent.com/mamedev/mame/master/src/mame/luxor/abc80.cpp>.
- MAME's `SN76477` sound device: `src/devices/sound/sn76477.cpp`/`.h`
  (per-sample RC-integrator formulas for the VCO/SLF/noise/one-shot/
  attack-decay subsystems, and the measured voltage-threshold constants
  they're built on), fetched from
  <https://raw.githubusercontent.com/mamedev/mame/master/src/devices/sound/sn76477.cpp>
  and the equivalent `.h` path — used to derive the real SLF/noise/
  one-shot/envelope timing values in `ABC80_REFERENCE.md`'s Sound
  section beyond the VCO's own (previously the only one grounded).
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
- Scandia Metric AB, *Kort beskrivning av ABC-80 BASIC* —
  <https://www.abc80.net/archive/luxor/ABC80/Kort-beskrivning-av-abc80-basic.pdf>
  (the primary source for `abc80/docs/ABC80_BASIC_REFERENCE.md`).
- Real ABC80 floppy disk images: <https://www.abc80.net/archive/luxor/sw/disk_images/ABC80/160k/>
  (14 real, dumped 160KB disk images with scanned labels/descriptions in
  that directory's own `index.txt` — corrected from this project's own
  earlier, mistaken count of 49; `disk003.img`, "System.diskett ABC80
  Ver. 2.1," used as real ground truth for Milestone 6's floppy/DOS
  bypass sub-step, not yet committed into this repo).
- sasq64/abc80sim, a real, independent open-source ABC80 emulator with
  working floppy support — <https://github.com/sasq64/abc80sim> (`src/
  disk.c`, `src/abcio.c`): source for the real ABC830/"mo" controller
  sector-addressing formula this project's own bypass had wrong (see
  Milestone 6's own write-up) — pointed at by the user rather than found
  independently.
- andersrcarlsson-stack/abc80-pico-public, a real ABC80 emulator running
  on Raspberry Pi Pico hardware — <https://github.com/andersrcarlsson-stack/abc80-pico-public>
  (`src/disk_controller.cpp`): source for the real sector-interleave
  mapping (`phys_sector()`, interleave factor 7) that turned out to be
  Milestone 6's actual remaining root cause — pointed at by the user
  rather than found independently.
- Torfinn Ingolfsen's `abc80sim` notes page —
  <https://tingo.homedns.org/emulators/abc80sim/> (not reachable
  directly from this environment; read via its Wayback Machine archive,
  <http://web.archive.org/web/20250622044221/http://tingo.homedns.org/emulators/abc80sim/>)
  — checked but turned out to contain only build-environment
  troubleshooting logs, no protocol detail; a dead end, noted here for
  completeness rather than silently omitted.

See `abc80/docs/ABC80_REFERENCE.md` for a consolidated hardware reference
(memory map, I/O ports, ROM/PROM inventory, per-subsystem register layouts)
pulled from all of the above plus this project's own code comments — this
section only lists sources, not the facts derived from them.
