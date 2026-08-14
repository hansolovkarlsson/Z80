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
| `A`, Carry (out) | Success: Carry clear, `A=0`. Failure: Carry set (`SCF`), via one of a couple of distinct error paths in `L608F` - specific error codes not yet enumerated. |
| Transfer size | Strong circumstantial evidence of a full 256 bytes/block: the transfer-count register is loaded via a classic Z80 `LD B,0` / `DJNZ` idiom (0 wraps to 256 iterations) from a polled status byte, consistent with a real 256-byte sector, though not independently confirmed against a real disk image. |

**Still open, not yet resolved — the real remaining work**:
- `B`'s other bits (0-3, 7) - only bits 4-6 (channel select) are decoded.
- The exact error codes possible from the two failure paths in `L608F`
  (Carry is set either way; the specific meaning of each path isn't
  pinned down).
- Independent confirmation of the 256-byte transfer size against a real
  disk image, if one can be found/verified, rather than relying solely on
  the `LD B,0`/`DJNZ` circumstantial evidence.
- `UFD80V20.bin` (the alternate real DOS variant, also committed) hasn't
  been examined at all yet - unknown whether it shares this same
  low-level protocol/calling convention or differs.

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

**Remaining, smaller open items**:
- Re-examine the still-open items from before the interleaving fix -
  the exact `B` bits 0-3/7, independent transfer-size confirmation - now
  that real files load and save correctly, several may resolve quickly
  or turn out moot.
- Consider committing `disk003.img` (or a small, purpose-built test
  image) into the repo now that it backs a real, working feature rather
  than a research artifact - still an open decision, not yet made.

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

## Milestone 11: a real GTK window — in progress

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

**Remaining open items**:
- Explicit verification that a real GRAPHICS-mode program renders true
  2×3 block pixels (only the TEXT-mode boot banner and general keyboard
  input have been confirmed hands-on so far).
- Glyph-cache-vs-per-pixel-`cairo_fill()` performance at real frame
  rates - not yet measured; the current renderer fills one rectangle per
  set pixel bit, which may or may not be fast enough for smooth 30fps
  redraws of a full 960-character screen.
- `--quickload`/`--quicksave` aren't supported by `bin/abc80-gtk` yet
  (not essential for "run in a window," deferred).
- Live SN76477 audio, per this milestone's own earlier scoping decision
  - still out of scope.

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
  pointers, not real analog tape emulation, and sound only synthesizes a
  single steady-tone case (no noise/SLF-warble/envelope shaping) rendered
  to a WAV file rather than played live - see each milestone's own
  write-up.
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
