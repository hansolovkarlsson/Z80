# ABC80 Hardware Reference

Technical reference for the real Luxor ABC80 hardware this machine target
emulates: memory map, I/O ports, ROM/PROM inventory, and the per-subsystem
register/address formulas this project's own code implements. This is the
ABC80 counterpart to `cpm/docs/Z80_REFERENCE.md`/`CPM_REFERENCE.md` — a
technical reference, not a status log. For milestone status, what's
implemented vs. still missing, and the story behind each design decision
(false starts, real bugs found and fixed, exact verification numbers), see
`abc80/docs/ABC80_ROADMAP.md` instead. Every fact below is grounded against
a primary source (MAME's current mainline driver, an independently
downloaded and checksum-verified ROM/PROM image, a real magazine article or
service manual, or this project's own disassembler/emulator output) — see
each section's own citation and this document's "Sources" section at the
end.

## System overview

- **CPU**: Z80A @ 2.9952 MHz (11.9808 MHz crystal / 2 / 2 — MAME's
  `abc80_state::abc80_common()`: `Z80(config, m_maincpu,
  XTAL(11'980'800)/2/2)`).
- **ROM**: 16KB BASIC interpreter, `0x0000`-`0x3FFF`, four 4Kx8 chips.
- **RAM**: 16KB onboard (`0xC000`-`0xFFFF`) in the base configuration; a
  real, well-documented 16KB expansion mod exists (see "RAM expansion"
  below) bringing the total to 32KB.
- **Video RAM**: 1KB, `0x7C00`-`0x7FFF`, 40 columns × 24 rows.
- **I/O**: ABCbus (external expansion), Z80 PIO (keyboard/cassette/serial),
  SN76477 Complex Sound Generator.

## Memory map

Cross-checked between MAME's current mainline driver
(`src/mame/luxor/abc80.cpp`) and an independent Swedish ABC80 hobbyist-forum
mention of the screen memory address (31744–32767 decimal = `0x7C00`–
`0x7FFF`, matching MAME exactly) — this resolved a discrepancy against an
older/outdated MESS driver fork, which is why both sources are recorded
here rather than trusting either alone.

| Range | Contents |
|---|---|
| `0x0000`-`0x3FFF` | ROM: four 4Kx8 chips — `3506_3.a5`/`3507_3.a3`/`3508_3.a4`/`3509_3.a2` at `0x0000`/`0x1000`/`0x2000`/`0x3000`. CRC32 `e2afbf48`/`d224412a`/`1502ba5b`/`bc8860b7`. |
| `0x4000`-`0x7BFF` | ABCbus-delegated. Floats (fixed `0xFF` reads, writes discarded) — no DOS/printer/IEC ROM card modeled yet (see "ABCbus ROM cards" below). |
| `0x7C00`-`0x7FFF` | Video RAM (1KB — 40×24 char cells). Always real RAM regardless of what's on the ABCbus — real hardware wires it to dedicated onboard video RAM. |
| `0x8000`-`0xBFFF` | ABCbus-delegated. Floats by default; real RAM if the 16KB expansion mod (`bin/abc80 --ram32k`) is modeled. |
| `0xC000`-`0xFFFF` | Onboard RAM (16KB, always present). `BOFA`/`EOFA`/`HEAD` (BASIC's program-storage pointers) live at `0xFE1C`/`0xFE1E`/`0xFE20` within this range. |

**Floating-bus modeling**: MAME's `abcbus_slot_device` forwards every read
in the delegated range to the attached card's `abcbus_xmemfl()`; its
default (no card) implementation is `return 0xff;` unconditionally — a
fixed value, not "whatever was last written" — confirmed directly from
MAME's `abcbus.h` (`device_abcbus_card_interface::abcbus_xmemfl()` /
`abcbus_slot_device::xmemfl_r()`). This emulator matches that via an
optional `Z80.bus_read_hook` (`cpm/emu/src/z80.h`/`z80.c`, `NULL` by
default — the CP/M target never sets it) that `abc80/emu/src/main.c`
installs to force `0xFF` for the unpopulated parts of this range,
regardless of the backing array's actual contents. Writes are left
un-intercepted (harmless, since reads are already forced).

### RAM expansion (16KB → 32KB)

Real, well-documented modification: Christer Ekman, "Bygg ut din ABC 80
till 32K RAM," *Mikrodatorn* nr 7, 1982 (a primary source — the actual
magazine article, not a secondhand summary) — the same "Mikrodatorn" RAM
expansion MAME's own driver TODO list names but has never implemented. Two
banks of eight 4116 DRAM chips are piggybacked onto the machine's existing
eight; an added OR gate fools the address decoder into starting the
RAS/CAS generator across `0x8000`-`0xFFFF` (32K-64K) instead of only
`0xC000`-`0xFFFF` (48K-64K), and two more OR gates steer `CAS` so the *new*
bank answers `0x8000`-`0xBFFF` while the *original* bank keeps
`0xC000`-`0xFFFF`. Not a separate ABCbus expansion card physically — a
direct motherboard modification — but it occupies exactly the address
range this emulator's ABCbus-delegated region covers, so `--ram32k` models
it there.

The article verifies its own mod by reading `BOFA` (`PEEK(65052)+
PEEK(65053)*256`, BASIC's boot-time RAM-size-detection result): `49152`
(`0xC000`) on the base 16K machine, `32768` (`0x8000`) once expanded. This
emulator reproduces both values exactly (see `ABC80_ROADMAP.md`'s
Milestone 6 section for the full before/after measurement).

### ABCbus ROM cards (not yet modeled)

A separate real expansion card ("Minneskort ABC", `ABC80-minneskort-
bruksanvisning.pdf`, a Luxor-published user manual — primary source) holds
up to 4 EPROM/ROM circuits (max 8Kbytes total) for DOS, printer, and IEC-bus
routines, at a jumper-configurable base address — **24K (`0x6000`) by
default as shipped**, alternatively 16K (`0x8000` is *not* one of the
documented jumper options in that manual — the two shown are 16K/`0x4000`
and 24K/`0x6000`). This lives entirely within `0x4000`-`0x7BFF`, i.e. the
part of the ABCbus range this emulator always floats — not yet implemented
here; folded into the still-open second half of Milestone 6 (floppy/DOS
controller).

## I/O ports

Global address mask `0x17` (MAME's `map.global_mask(0x17)`): only bits
{0,1,2,4} of the port address are hardware-decoded, so any port `P` with
`(P & 0x17) == target` aliases the identical register. Real software
exploits this — e.g. this ROM addresses PIO Port A via `IN A,(38h)`
(`0x38 & 0x17 == 0x10`). `z80_io_in()`/`z80_io_out()`
(`cpm/emu/src/z80.c`) are a plain flat 256-entry array with no device
logic of their own, so `abc80/emu/src/main.c` keeps every alias in sync
itself (`init_pio_port_a_aliases()`/`sync_pio_port_a()`) rather than the
CPU core knowing anything about the mask.

| Port(s) (post-mask) | Function |
|---|---|
| `0x00`-`0x01` | ABCbus data/status |
| `0x02`-`0x05` | ABCbus control lines C1-C4 |
| `0x06` | SN76477 sound chip control byte |
| `0x07` | ABCbus reset |
| `0x10`-`0x13` (mirrored `0x14`-`0x17`) | Z80 PIO |

## ROM/PROM inventory

All images at `abc80/resources/rom/`, independently downloaded from
abc80.net's archive and CRC32-verified byte-identical against MAME's
mainline driver checksums (see that directory's own `README.md` for the
full per-file provenance and download URLs).

| File | Board position | Chip | Size | CRC32 | Contents |
|---|---|---|---|---|---|
| `3506_3.a5.bin` | A5 | — | 4096 | `e2afbf48` | BASIC ROM, `0x0000` |
| `3507_3.a3.bin` | A3 | — | 4096 | `d224412a` | BASIC ROM, `0x1000` |
| `3508_3.a4.bin` | A4 | — | 4096 | `1502ba5b` | BASIC ROM, `0x2000` |
| `3509_3.a2.bin` | A2 | — | 4096 | `bc8860b7` | BASIC ROM, `0x3000` |
| `chargen.bin` | H2 | SN74S263N | 2560 | `9e064e91` | Character generator (Swedish/Finnish variant) |
| `hsync.bin` | K5 | 82S129 | 256 | `e4f7e018` | Horizontal sync/attribute-select timing |
| `vsync.bin` | K2 | 82S131 | 512 | `445a45b9` | Vertical sync/attribute-select timing |
| `attr.bin` | J3 | 82S129 | 256 | `6c46811c` | Attribute (BLANK/TEXT/GRAPHICS/VERSAL) |
| `line.bin` | K1 | 82S131 | 512 | `74de7a0b` | Chargen row address per scanline |

`chargen.bin` carries a real caveat worth repeating here: MAME's own source
marks it `BAD_DUMP`/`"created by hand"`, since the SN74S263 is
mask-programmed with no electronically readable contents — this is the
de facto standard hand-reconstructed copy every ABC80 emulation project
(this one included) relies on, not a genuine ROM dump. Independently
verified correct anyway: decoding it produces clean letterforms, and
character `0x5B` decodes to a distinct "Ä" glyph, confirming both the
address formula and the chip's documented Swedish/Finnish identity.

## Keyboard (Z80 PIO Port A)

Bit layout, grounded against MAME's `abc80_state::pio_pa_r()` and confirmed
against this ROM's own disassembly (its steady-state poll loop at
`0x02F1`-`0x0300` does `IN A,(38h)` / `ADD A,A` / `JR C,...`, i.e. shifts
bit 7 into carry — genuine evidence of polling, not interrupt-driven input):

| Bit(s) | Meaning |
|---|---|
| 0-6 | Last key's ASCII code |
| 7 | Strobe (1 = unread key pending) |

**Real hardware**: the PIO strobe is pulsed for ~50ms per keystroke
(MAME's `abc80_state::kbd_w()`/`m_keyboard_clear_timer`). **This
emulator**: edge-triggered instead of timed — the strobe clears when
`abc80/emu/src/main.c` observes `PC == 0x0316`, the exact address (found
via disassembly) where this ROM's own multi-poll debounce loop genuinely
finishes consuming a key, rather than an arbitrary instruction-count guess
(see `keyboard.h`'s own comment for why a fixed-duration hold doesn't work
for both the ~500,000-instruction wait before the poll loop is ever
reached *and* the near-immediate re-poll for a line's next character).
Neither this emulator nor MAME implements the real hardware scan-matrix
PROM (`abc80-keyboard.bin`/N82S141) — both forward host-ASCII keypresses
directly, a well-precedented simplification.

## Video generation

### Character-generator ROM addressing

Ported from MAME's `sn74s262_device::read()`:

```
u8 sn74s262_device::read(u8 character, u8 row) {
    if ((row & 0xf) > 8) return 0;
    return m_char_rom[((character & 0x7f) * 10) + (row & 0xf)];
}
```

128 characters (7-bit code) × 10 bytes each; only rows 0-8 hold real pixel
data (row 9 is always blank — free inter-row spacing). Each byte's top 6
bits are the pixel row, MSB-first/leftmost-first (confirmed against
`draw_character()`'s `data <<= 1` shifted exactly 6 times); the low 2 bits
are unused.

### Video RAM addressing

Ported verbatim from MAME's `abc80_state::get_videoram_addr()` (the base
ABC80's 40-column variant — not TKN80's 80-column extension), a real
hardware bit-interleaving scheme:

```
int a = (col >> 3) & 0x07;
int b = ((row >> 1) & 0x0c) | ((row >> 3) & 0x03);
int s = (a + b) & 0x0f;
addr = ((row & 0x07) << 7) | (s << 3) | (col & 0x07);
```

`row` is the character row (0-23), `col` the character column (0-39).
Verified a genuine bijection over all 24×40 = 960 real character cells
(zero address collisions, brute-force enumerated) before being ported.

### Sync/timing PROMs

One byte per horizontal character-slot (`hsync.bin`, `sx` = 0-63 — only the
first 64 of the physical 256-byte ROM are ever addressed, MAME's
`draw_scanline()` loops `sx < 64`) or per scanline (`vsync.bin`, `y` =
0-312):

| PROM | Bit 0 | Bit 1 | Bit 2 | Bit 3 |
|---|---|---|---|---|
| `hsync.bin` (K5) | `HSYNC` | `DH` (display horizontal — active display area) | `LINE_END` | `ROW_START` |
| `vsync.bin` (K2) | `VSYNC` | `DV` (display vertical — active display area) | `FRAME_END` | `FRAME_RESET` |

Verified against the real committed ROM bytes: `ROW_START` set for exactly
`sx`=15..54 (the real 40-column visible width); `FRAME_END` fires 23 times
across the 313-line frame at 10-line intervals (24 character rows × the
chargen ROM's 10-scanline cell height); `line.bin`'s values cycle
0,1,2,...,9,0,1,... once per character row, feeding the chargen `row`
parameter directly.

### Attribute PROM

Ported from MAME's `draw_character()`:

```
u8 attr_addr = ((dh & dv) << 7) | (videoram_data & 0x7f);
u8 attr_data = m_attr_prom->base()[attr_addr];
```

`dh`/`dv` ("display horizontal/vertical", from the sync PROMs above) both
mean "currently within the active display area" — despite the name,
`dh & dv` is true for nearly the entire visible 40×24 area (only the
border/blanking region has it false).

| Bit | Meaning |
|---|---|
| 0 | `BLANK` |
| 1 | `TEXT` |
| 2 | `GRAPHICS` |
| 3 | `VERSAL` |

Per-character mode (TEXT vs. GRAPHICS/block) is a state machine carried
across a row, reset at `LINE_END`, ported from `draw_scanline()`/
`draw_character()`:

```
if (!TEXT && GRAPHICS) mode = 0;
if (TEXT && !GRAPHICS) mode = 1;
if (TEXT &&  GRAPHICS) mode = !mode;
// if (mode & VERSAL) → graphics/block mode, else → text mode
```

### Character set (TEXT mode)

Swedish/Finnish national variant of ISO 646 (SEN 850200 Annex B), **not**
plain ASCII — confirmed by decoding every one of the nine positions where
the two differ, not assumed from the standard alone:

| Code | ASCII | ABC80 | Code | ASCII | ABC80 |
|---|---|---|---|---|---|
| `0x40` | `@` | É | `0x60` | `` ` `` | é |
| `0x5B` | `[` | Ä | `0x7B` | `{` | ä |
| `0x5C` | `\` | Ö | `0x7C` | `\|` | ö |
| `0x5D` | `]` | Å | `0x7D` | `}` | å |
| `0x5E` | `^` | Ü | `0x7E` | `~` | ü |

Every other 7-bit code renders as plain ASCII; control codes (`0x00`-`0x1F`,
`0x7F`) render blank.

### GRAPHICS mode (2×3 block mosaic)

Six independently-settable sub-cells per character, from `draw_character()`'s
own `c0`..`c5` derivation:

| Videoram bit | Sub-cell |
|---|---|
| 0 | top-left |
| 1 | top-right |
| 2 | mid-left |
| 3 | mid-right |
| 4 | bottom-left |
| 6 | bottom-right |

(Bit 5 unused; bit 7 is the cursor flag, independent of graphics mode.)
This emulator's terminal renderer maps the 6-bit cell pattern onto
Unicode's "Symbols for Legacy Computing" sextant block (U+1FB00-U+1FB3B),
whose `BLOCK SEXTANT-<cells>` characters use the identical reading-order
numbering (1=top-left..6=bottom-right) and 2×3 layout — cross-checked
against Unicode's own published codepoint table at multiple points, not
assumed from the bit pattern. Four combinations (all-blank, all-filled,
left-column-only, right-column-only) reuse pre-existing SPACE/FULL
BLOCK/LEFT HALF BLOCK/RIGHT HALF BLOCK codepoints instead of a dedicated
sextant one.

## Cassette storage (program-storage pointers)

Fixed RAM addresses, grounded against MAME's `abc80.h`:

| Address | Name | Meaning |
|---|---|---|
| `0xFE1C` | `BOFA` | Beginning Of File Area — start of the current program's tokenized bytes |
| `0xFE1E` | `EOFA` | End Of File Area — inclusive end (a terminator byte `RUN` depends on) |
| `0xFE20` | `HEAD` | Byte immediately past `EOFA` |

Verified empirically: typing a numbered line moves `EOFA` forward by
exactly the stored line's length, and the bytes decode as genuine BASIC
tokens (length byte, little-endian line number, keyword/operator tokens,
numeric-literal encodings, `0x0D` terminator per line).

`bin/abc80 --quicksave FILE` writes one reserved header byte (real meaning
unconfirmed by any primary source found — see `cassette.c`'s own comment;
written as `0x00`) followed by `ram[BOFA..EOFA]` **inclusive**. `--quickload
FILE` skips that header byte and injects the rest at the current `BOFA`,
then fixes up `EOFA`/`HEAD`. This bypasses MAME's own (and real hardware's)
analog cassette path entirely, mirroring MAME's own `quickload_cb`
mechanism rather than the real Kansas-City-FSK tape encoding.

## Sound (SN76477 Complex Sound Generator, port `0x06`)

Register bit layout, grounded against MAME's `abc80_state::csg_w()`:

| Bit | Meaning |
|---|---|
| 0 | Enable (active low: `0` = enabled) |
| 1 | VCO voltage (`0` = 0V "natural" pitch; `1` = 2.5V — saturates the VCO silent, exceeding the real chip's 2.35V max) |
| 2 | VCO mode (`0` = fixed pitch; `1` = swept by the SLF oscillator — a warble/siren effect) |
| 3, 4, 5 | Mixer select (B, A, C) — `mixer_mode = (bit5<<2)\|(bit3<<1)\|bit4` |
| 6, 7 | Envelope select (2, 1) — `envelope_mode = (bit6<<1)\|bit7` |

| `mixer_mode` | Meaning | `envelope_mode` | Meaning |
|---|---|---|---|
| 0 | VCO | 0 | VCO |
| 1 | SLF | 1 | One-Shot |
| 2 | Noise | 2 | Mixer Only |
| 3 | VCO/Noise | 3 | VCO-with-Alternating-Polarity |
| 4 | SLF/Noise | | |
| 5 | SLF/VCO/Noise | | |
| 6 | SLF/VCO | | |
| 7 | Inhibit | | |

(Mixer/envelope bit-packing and mode tables taken directly from MAME's
`src/devices/sound/sn76477.cpp`.)

**Board component values** (VCO oscillator): `R = 100kΩ`, `C = 10nF`
(MAME's `machine_config`: `m_csg->set_vco_params(0, CAP_N(10),
RES_K(100));`). **VCO frequency**, hand-derived from MAME's general
per-sample charge-rate formula for this board's specific constant-voltage
(50% duty cycle) case: `f = 0.64 / (R × C)` — 640 Hz for these component
values. Measured (FFT/zero-crossing) against both a synthetic register-event
sequence and real ROM-driven `OUT` execution: 639.39 Hz and 639.95 Hz
respectively, both within 0.1% of predicted.

**Scope**: this emulator only synthesizes real audio for the single most
common case — `mixer_mode==0` (VCO alone), `envelope_mode==2` (Mixer Only,
continuous output), VCO mode 0 (fixed pitch) — a steady tone at a fixed
frequency, output as a plain square wave via `bin/abc80 --wav FILE` (no
live audio in this environment). Every other register combination (noise,
SLF, one-shot, alternating polarity, SLF-swept warble) produces silence in
this model rather than an attempt at incorrect audio. Full analog
SN76477 simulation (MAME's own `sn76477.cpp`) is a genuine per-sample
simulation of four interacting RC-timed subsystems — out of scope for what
this project's own roadmap treats as its lowest-priority, purely cosmetic
milestone.

## Sources

- MAME mainline driver: `src/mame/luxor/abc80.cpp`, `abc80.h`,
  `abc80_v.cpp` (memory map, I/O map, ROM checksums, machine
  configuration, video/attribute logic).
- MAME device sources: `src/devices/video/sn74s262.cpp` (chargen),
  `src/devices/sound/sn76477.cpp` (sound), `src/devices/bus/abcbus/
  abcbus.h`/`.cpp` (ABCbus default/no-card behavior).
- *Mikrodatorns ABC* (Gunnar Markesjö) — block diagrams and partial
  circuit schematics; full text at
  <https://archive.org/stream/microdatorns_abc/microdatorns_abc_djvu.txt>.
- Christer Ekman, "Bygg ut din ABC 80 till 32K RAM," *Mikrodatorn* nr 7,
  1982 — <https://www.abc80.net/archive/luxor/ABC80/ABC80-32K-mod-Mikrodatorn.pdf>.
- *Bruksanvisning Minneskort ABC* (Luxor) —
  <https://www.abc80.net/archive/luxor/ABC80/ABC80-minneskort-bruksanvisning.pdf>.
- ABC80 service manual, PC/M Personal Computer Museum archive:
  <https://www.abc80.net/archive/luxor/ABC80/ABC80-servicemanual.pdf>.
- ROM/PROM images: <https://www.abc80.net/archive/luxor/Prom/fw/ABC80/> —
  see `abc80/resources/rom/README.md` for the exact files and checksum
  cross-check against MAME.
- This project's own tooling: `bin/z80dasm` (real ROM disassembly, used to
  find the keyboard poll loop and debounce-convergence address),
  `bin/abc80-chargen-dump`/`bin/abc80-video-timing-dump`/
  `bin/abc80-render-demo`/`bin/abc80-sound-demo` (standalone verification
  against known synthetic input before trusting real ROM execution).
