# ABC80 Hardware Reference

Technical reference for the real Luxor ABC80 hardware this machine target
emulates: memory map, I/O ports, ROM/PROM inventory, and the per-subsystem
register/address formulas this project's own code implements. This is the
ABC80 counterpart to `docs/Z80_REFERENCE.md`/`CPM_REFERENCE.md` — a
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
optional `Z80.bus_read_hook` (`z80core/z80.h`/`z80.c`, `NULL` by
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

### ABCbus ROM cards

A separate real expansion card ("Minneskort ABC", `ABC80-minneskort-
bruksanvisning.pdf`, a Luxor-published user manual — primary source) holds
up to 4 EPROM/ROM circuits (max 8Kbytes total) for DOS, printer, and IEC-bus
routines, at a jumper-configurable base address — **24K (`0x6000`) by
default as shipped**, alternatively 16K (`0x8000` is *not* one of the
documented jumper options in that manual — the two shown are 16K/`0x4000`
and 24K/`0x6000`). This lives entirely within `0x4000`-`0x7BFF`, i.e. the
part of the ABCbus range this emulator otherwise floats.

`--disk` models the DOS case: it loads a real 4K DOS ROM at `0x6000` and
that window then reads as ROM rather than floating. Two real images are
committed and both work — `ABCDOS80.bin` (the default) and `UFD80V20.bin`,
selectable with `--dos-rom`. The printer and IEC-bus ROMs are not modeled;
no image for them is committed.

### ABCbus floppy controller

The bus registers sit at post-mask ports `0x00`-`0x05` and `0x07` (see the
I/O map below). A transaction is a four-byte command header written to the
OUT port — command, unit, and two sector-address bytes — followed by a
256-byte transfer in whichever direction the command asked for, each byte
gated on status bit 0.

The DOS ROM sends the header straight out of `B`/`C`/`D`/`E` (`0x6136`-
`0x6142`). The command byte is a bitmask (`0x01` read sector, `0x02` to
host, `0x04` from host, `0x08` write sector), of which this ROM only ever
issues two combinations: `0x03` for a read (`0x6071`) and `0x0C` for a
write (`0x60AA`).

**This ROM talks to exactly one kind of drive.** It writes a hardcoded
`0x2D` — the ABC830 select code — to the CS port at `0x60F1`, the only
`OUT (01h)` anywhere in the image, so it can never address a 640K ABC832.
`UFD80V20.bin` is the more general driver: it masks its select to six bits
and reads the value from a variable (`0x61C1`).

The status byte is shared with the ABC800 family; see
`abc802/docs/ABC802_REFERENCE.md`'s ABC-bus section for the bit table. Two
of those bits are pinned by *this* ROM rather than the ABC802's, which
never reads them: bit 3 must be set while a command is proceeding
(`0x6118`) and must still be set when it completes, which the write path
returns on as success (`0x60E9`, `0x60C1`). Bit 2 must always be clear,
because `0x6120` loads it directly into the low byte of the transfer
address (`AND 04h` ... `LD L,A`).

The controller itself is `abcbus/disk.c`, shared with the ABC802 target.

## I/O ports

Global address mask `0x17` (MAME's `map.global_mask(0x17)`): only bits
{0,1,2,4} of the port address are hardware-decoded, so any port `P` with
`(P & 0x17) == target` aliases the identical register. Real software
exploits this — e.g. this ROM addresses PIO Port A via `IN A,(38h)`
(`0x38 & 0x17 == 0x10`). `z80_io_in()`/`z80_io_out()`
(`z80core/z80.c`) are a plain flat 256-entry array by default, with no
device logic of their own, so `abc80/emu/src/main.c` keeps every PIO alias
in sync itself (`init_pio_port_a_aliases()`/`sync_pio_port_a()`) rather
than the CPU core knowing anything about the mask. The ABC-bus ports are
the exception: they are a real device, and reach it through the core's
`io_in_hook`/`io_out_hook`, which `abc80/emu/src/abcbus.c` installs and
which fall through to the flat array for every other port.

| Port(s) (post-mask) | Function |
|---|---|
| `0x00`-`0x01` | ABCbus data (INP/OUT) and status/select (STAT/CS) |
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

## Periodic PIO interrupt (timer)

Real hardware repurposes the Z80 PIO's Port A *strobe* input pin (normally
a keyboard handshake line) as a free-running clock: MAME's
`scanline_tick()` toggles it once per scanline. Since the PIO's `MODE_INPUT`
only fires an interrupt on a *rising* edge, and the toggle produces one
rising edge every two scanlines, the real interrupt rate is:

| Quantity | Value | Derivation |
|---|---|---|
| Pixel clock | 5,990,400 Hz | `XTAL(11'980'800)/2` |
| Line rate | 15,600 Hz | pixel clock / `ABC80_HTOTAL` (384) |
| T-states/scanline | 192 | CPU clock (2,995,200 Hz) / line rate |
| Interrupt period | 384 T-states | 2 scanlines (one full toggle cycle) |
| Interrupt rate | 7800 Hz | 1 / interrupt period |

**Boot configuration**, confirmed via this ROM's own disassembly
(`0x0068`-`0x00C5`):

| Setting | Value | How set |
|---|---|---|
| Interrupt mode | IM 2 | `IM 2` at `0x006A` |
| `I` register | `0x00` | `LD I,A` (A=0) at `0x008C` |
| Port A mode | Input (hardware reset default) | Never overridden — no mode-control word sent to Port A |
| Interrupt vector | `0x34` | `OUT (39h),A` (port `0x11`, Port A control) — any control-word byte with bit 0 clear loads the vector |
| Interrupt control word | `0xB7` | Enable=1, mask-follows=1 |
| Mask byte | `0x7F` | Required by mask-follows above; inert for Port A's Input mode |

With `I=0` and vector `0x34`, the real IM2 vector-table entry is at
`0x0034`; this ROM's own bytes there (`0x1E 0x03`, little-endian) point to
the real interrupt handler at `0x031E` (confirmed by its own `RETI` at
`0x0336`):

```
0x031E: PUSH AF
0x031F: IN A,(38h)        ; read Port A directly
0x0321: CP 83h             ; Ctrl-C-style break combo?
0x0323: JR NZ,L032C
0x0325: LD (0FE07h),A      ; latch break-pending flag
0x0328: LD A,80h
0x032A: OUT (06h),A        ; beep (SN76477)
L032C:  LD (0FDF5h),A      ; unconditional: pre-latch last-read Port A byte
0x032F: LD A,46h
0x0331: LD (0FDF7h),A      ; unconditional: reload keyboard debounce counter
0x0334: POP AF
0x0335: EI
0x0336: RETI
```

`0xFDF7` is `IX+4` in the keyboard poll loop's own `IX=0xFDF3` base
(confirmed at `0x02A5`) — this refresh is what lets that loop's debounce
(`DEC (IX+3)` / `DEC (IX+4)`) actually converge on real hardware.
`0xFDF5` (`IX+2`) is read by the poll loop's very first check
(`BIT 7,(IX+2)`); if the interrupt has already seen a strobed key, the poll
loop skips its own decrement-based debounce entirely and consumes the key
immediately — an interrupt-driven fast path alongside the direct-polling
fallback, both funneling through the same real consumption point,
`0x0316`.

This emulator delivers the interrupt via a plain periodic scheduler
(`z80_request_int()`, already built and proven for CP/M's own interrupt
handling) rather than modeling `scanline_tick()`/PIO strobe edges
individually — functionally equivalent for every real interrupt this ROM
observes, since nothing here depends on the toggle's *falling* half.

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

**Scope**: as of `abc80/docs/ABC80_ROADMAP.md`'s "full SN76477 emulation"
sub-step, this emulator synthesizes every real subsystem below — VCO
(fixed or SLF-swept), SLF alone, the noise generator, one-shot, and the
attack/decay envelope (including Mixer Only, One-Shot, and
VCO-with-Alternating-Polarity envelope modes), all via
`abc80_sound_step_sample()` (`sound.c`) — not just the single VCO-alone/
Mixer-Only case this section originally covered. One deliberate
simplification remains: amplitude uses a linear
`attack_decay_cap_voltage`-fraction scale rather than MAME's exact
analog output-stage gain-table curve (`out_pos_gain`/`out_neg_gain`,
`center_to_peak_voltage_out()`) — full scale, unchanged, for Mixer Only
mode; see that sub-step's own write-up for the two real corrections
found while porting MAME's algorithm (the one-shot's real
enable-transition trigger, and a VCO-ceiling special case) and its full
verification results.

**Full board component values, and the real timing they produce** —
pulled directly from MAME's own `src/mame/luxor/abc80.cpp` machine_config
(fetched from `mamedev/mame` on GitHub) and hand-derived using
`src/devices/sound/sn76477.cpp`'s own documented charge-rate formulas
and measured voltage-threshold constants (`ONE_SHOT_CAP_VOLTAGE_RANGE`,
`SLF_CAP_VOLTAGE_MIN/MAX`, `NOISE_CAP_VOLTAGE_RANGE`,
`AD_CAP_VOLTAGE_RANGE`, etc. — the same per-subsystem RC-integrator
formulas the VCO's own 640Hz figure above was derived from). VCO's own
figure recomputed here too, as a sanity check on unit handling before
trusting the rest: came out to exactly 640.00Hz, matching):

```
m_csg->set_noise_params(RES_K(47), RES_K(330), CAP_P(390));
m_csg->set_decay_res(RES_K(47));
m_csg->set_attack_params(CAP_U(10), RES_K(2.2));
m_csg->set_amp_res(RES_K(33));
m_csg->set_feedback_res(RES_K(10));
m_csg->set_vco_params(0, CAP_N(10), RES_K(100));
m_csg->set_pitch_voltage(0);
m_csg->set_slf_params(CAP_U(1), RES_K(220));
m_csg->set_oneshot_params(CAP_U(0.1), RES_K(330));
```

| Subsystem | Board R/C | Derived real value |
|---|---|---|
| VCO | R=100kΩ, C=10nF | 640.00 Hz (already known — recomputed as a cross-check) |
| SLF (super low frequency oscillator) | R=220kΩ, C=1µF | ~3.98 Hz triangle wave (130.8ms rise / 120.4ms fall — asymmetric, not a symmetric triangle) |
| Noise generator clock | R=47kΩ | ~24,888 Hz (the underlying LFSR clock rate feeding the noise filter below, not the audible noise "color" by itself) |
| Noise filter | R=330kΩ, C=390pF | charge/discharge ~145,000 V/s — fast relative to the 24.9kHz noise clock, so the filter passes most of that clock's variation through rather than smoothing it heavily |
| One-shot pulse | R=330kΩ, C=0.1µF | ~28.56 ms (envelope_mode==1 — a short, single percussive envelope per trigger, not continuous) |
| Attack/decay envelope | attack R=2.2kΩ, decay R=47kΩ, shared C=10µF | attack ~22.0ms, decay ~470.0ms (envelope_mode==3, VCO-with-Alternating-Polarity — a fast-rising, slow-fading envelope shape, not symmetric) |

Confirmed the SLF/VCO-sweep ("warble/siren"), noise, one-shot, and
alternating-polarity-envelope modes are all real, physically-grounded,
implementable subsystems on *this* board specifically — not previously
known because only the VCO's own R/C values had been pulled from the
driver before now. Now implemented: `sound.c`'s `abc80_sound_step_sample()`
ports `sn76477_device::sound_stream_update()`'s per-sample integrator
loop directly (each subsystem an independent RC charge/discharge state
machine against fixed voltage thresholds — the SLF's own cap voltage
drives the VCO's own charge ceiling when VCO mode is swept, the one
real coupling between subsystems), replacing the old VCO-only
`abc80_sound_live_sample()` and unifying it with
`abc80_sound_render_wav()` into one shared implementation both now
drive — see `ABC80_ROADMAP.md`'s own sub-step for the full
implementation, correction, and verification write-up.

## BASIC error codes

Extracted directly from a real, dumped ABC80 system disk
(`disk003.img`, "System.diskett ABC80 Ver. 2.1" — see
`ABC80_ROADMAP.md`'s Milestone 6 section for provenance and how this was
found), not from any manual — the Swedish error-message text lives in
that disk's own files (blocks 18-31), each message delimited by a marker
byte equal to `0x80 + error code`. Confirmed against two independently
cross-checked cases: `0xA9 & 0x7F = 41` → `SKIVAN FULL` ("disk full"),
matching this project's own real `ERR 41` result testing `SAVE` against
that same disk image; `0x95 & 0x7F = 21` → `HITTAR EJ FILEN` ("file not
found"), matching a real `ERR 21` result testing `LOAD`. English
translations below are this project's own, not official.

| Code | Swedish | English |
|---|---|---|
| 0 | EJ TILLÅTET ÖKA "DIM" | Not allowed to re-`DIM` |
| 1 | FEL ANTAL INDEX | Wrong number of indices |
| 2 | OTILLÅTET SOM KOMMANDO | Not allowed as a direct command |
| 3 | MINNET FULLT | Out of memory |
| 4 | FÖR STORT FLYTTAL | Floating-point number too large |
| 5 | FÖR STORT INDEX | Index too large |
| 6 | HITTAR EJ DETTA RADNUMMER | Line number not found |
| 7 | FÖR STORT HELTAL | Integer too large |
| 8 | FINNS EJ I DETTA SYSTEM | Not present in this system |
| 9 | INDEX UTANFÖR STRÄNGEN | Index outside the string |
| 10 | TEXTEN FÅR EJ PLATS I STRÄNGEN | Text doesn't fit in the string |
| 11 | FÖRSTÅR EJ | Doesn't understand (syntax error) |
| 12 | FELAKTIGT TAL | Malformed number |
| 13 | FEL ANTAL ELLER TYP AV ARGUMENT | Wrong number/type of argument |
| 14 | OTILLÅTET TECKEN EFTER SATSEN | Illegal character after the statement |
| 15 | "=" SAKNAS ELLER PÅ FEL PLATS | `=` missing or misplaced |
| 16 | RADNUMMER SAKNAS | Line number missing |
| 17 | OTILLÅTEN BLANDNING AV TAL OCH STRÄNGAR | Illegal mixing of numbers and strings |
| 18 | ")" SAKNAS ELLER PÅ FEL PLATS | `)` missing or misplaced |
| 19 | KAN EJ ÖPPNA FLER FILER | Can't open more files |
| 20 | FÖR LÅNG RAD (>120 TKN) | Line too long (>120 characters) |
| 21 | HITTAR EJ FILEN | File not found |
| 22 | OTILLÅTEN SATS | Illegal statement |
| 23 | "TO" SAKNAS | `TO` missing |
| 24 | "NEXT" SAKNAS | `NEXT` missing |
| 25 | FELAKTIG SATS EFTER "ON" | Malformed statement after `ON` |
| 26 | FEL I ON-UTTRYCK | Error in `ON` expression |
| 27 | "NEXT" UTAN "FOR" | `NEXT` without `FOR` |
| 28 | FEL VARIABEL EFTER "NEXT" | Wrong variable after `NEXT` |
| 29 | "RETURN" UTAN "GOSUB" | `RETURN` without `GOSUB` |
| 30 | DATA SLUT | Out of `DATA` |
| 31 | FEL DATA TILL KOMMANDO | Wrong data for the command |
| 32 | FILEN EJ ÖPPEN | File not open |
| 33 | "AS FILE" SAKNAS | `AS FILE` missing |
| 34 | SLUT PÅ FILEN | End of file |
| 35 | CHECKSUMMANFEL VID LÄSNING | Checksum error on read |
| 36 | CHECKSUMMAFEL VID SKRIVNING | Checksum error on write |
| 37 | FELAKTIGT RECORDFORMAT | Malformed record format |
| 38 | RECORDNUMMER UTANFÖR FILEN | Record number outside the file |
| 39 | FILEN SKRIVSKYDDAD | File is write-protected |
| 40 | FILEN RADERSKYDDAD | File is delete-protected |
| 41 | SKIVAN FULL | Disk full |
| 42 | SKIVAN EJ KLAR | Disk not ready |
| 43 | SKIVAN SKRIVSKYDDAD | Disk is write-protected |
| 44 | LOGISK FIL EJ ÖPPNAD | Logical file not opened |
| 45 | FEL LOGISKT FILNUMMER | Wrong logical file number |
| 46 | FEL ENHETSNUMMER | Wrong device number |
| 47 | FEL TRAP-NUMMER | Wrong trap number |
| 48 | FEL I BIBLIOTEKET | Error in the library |
| 49 | FELAKTIGT FYSISKT FILNUMMER | Wrong physical file number |
| 50 | KVADRATROT UR NEGATIVT TAL | Square root of a negative number |
| 51 | ENHETEN UPPTAGEN | Device busy |
| 52 | EJ TILL DENNA ENHET | Not [applicable] to this device |
| 53 | FELAKTIG RAD | Malformed line |
| 54 | IEC BÅDE SÄNDARE OCH MOTTAGARE | IEC both transmitter and receiver |
| 55 | IEC-MOTTAGARE EJ AKTIV | IEC receiver not active |
| 56 | IEC-SÄNDARE EJ AKTIV | IEC transmitter not active |
| 57 | FUNKTIONEN EJ DEFINIERAD | Function not defined |
| 58 | OGILTIGT TECKEN INLÄST | Invalid character read |
| 59 | FEL PROGRAMFORMAT | Wrong program format |
| 60 | BIT ADRESS >16 BITAR | Bit address >16 bits |
| 61 | KOMMA SAKNAS | Comma missing |
| 62 | DOT-ADRESS UTANFÖR SKÄRMEN | Dot address outside the screen |
| 63 | "AS" SAKNAS | `AS` missing |

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
- Real ABC80 floppy disk image `disk003.img` ("System.diskett ABC80 Ver.
  2.1"): <https://www.abc80.net/archive/luxor/sw/disk_images/ABC80/160k/> —
  source for the BASIC error-code table above and for the real directory/
  file layout this project's Milestone 6 disk research uses as ground
  truth; **decided not to commit it** into this repo — unlike the ROM
  images (explicit checksum/provenance table before committing) or
  ZEXALL/ZEXDOC (explicitly GPLv2), it has no license statement anywhere.
  Re-download from the URL above if reproducing this project's own disk
  research (see `ABC80_ROADMAP.md`'s Milestone 6 section).
- This project's own tooling: `bin/z80dasm` (real ROM disassembly, used to
  find the keyboard poll loop and debounce-convergence address),
  `bin/abc80-chargen-dump`/`bin/abc80-video-timing-dump`/
  `bin/abc80-render-demo`/`bin/abc80-sound-demo` (standalone verification
  against known synthetic input before trusting real ROM execution).
