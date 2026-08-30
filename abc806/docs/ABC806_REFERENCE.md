# ABC806 hardware reference

Consolidated hardware reference for the Luxor ABC806 (1983), the top of the
ABC800 family and the machine `bin/abc806` emulates. Companion to
[`ABC806_ROADMAP.md`](ABC806_ROADMAP.md) (status and open gaps),
[`ABC806_COMPLETED.md`](ABC806_COMPLETED.md) (how each fact below was
established, and what was wrong first), and
[`../../abc802/docs/ABC802_REFERENCE.md`](../../abc802/docs/ABC802_REFERENCE.md)
(the closely-related ABC802).

Facts here are either derived from MAME's own implementation
(`src/mame/luxor/abc80x.cpp` and `abc80x_v.cpp`, BSD-3-Clause, Curt Coder)
or confirmed empirically against the real ROM in this emulator. **Where the
two disagree, or where something is inferred rather than observed, it is
called out.** Several things below are in the first category: this target
implements at least one behaviour MAME describes only in a comment.

## Overview

| | |
|---|---|
| CPU | Z80A at 3 MHz |
| ROM | 32K in eight 4K sockets, overlaying the low half of the address space |
| RAM | 64K, plus 128K of video/high-resolution memory |
| Text | 80x25 or 40x25, eight colours, via a programmable MC6845 |
| Graphics | 240 rows x 128 bytes, four pixels per byte through a palette |
| Peripherals | Z80 CTC, SIO/2 and DART on an IM 2 daisy chain |
| Clock | E050-16 serial real-time clock, bit-banged |
| Storage | ABC-bus floppy controller (shared `abcbus/` card) |

### What separates it from the ABC802

Both run BASIC II from a 32K ROM over RAM, share the CRTC and the three Z80
peripherals, and mount the same ABC-bus card — so `abc806/emu/src/ports.c`
began as a near-copy of the ABC802's. The genuine differences:

- **A page map and an EME line**, where the ABC802 has one overlay bit.
- **A parallel 2K attribute plane plus the `RAD` PROM**, where the ABC802
  hides attributes in the character generator's own output byte. Nothing
  transfers between the two decodes.
- **A high-resolution plane** with its own colour lookup.
- **DTR-B means something else.** On the ABC802 that pin is LRS, selecting
  ROM or RAM in the low 32K; here it is KEYDTR. Same chip, same pin,
  different wiring.
- **An E050-16 real-time clock**, which the ABC802 has not.

## Memory map

| Range | Contents |
|---|---|
| `0x0000`-`0x77FF` | ROM — *or* the high-resolution plane, see below |
| `0x7800`-`0x7FFF` | ROM on an opcode fetch, 2K character RAM on a data access |
| `0x8000`-`0xFFFF` | RAM |

Writes below `0x8000` are discarded (the ROM is an EPROM overlay) except
through the high-resolution window.

### The character-RAM window is decoded by M1

An opcode fetch at `0x7800`-`0x7FFF` reads ROM; a data read at the same
address reads character RAM. The ROM genuinely has code in that window —
its keyboard-input routines, and the graphics plotter — so this is not a
corner case.

`abc806/emu/src/memory.c` reproduces it *without* touching the shared core:
z80core's `fetch_byte()` indexes the flat `ram` array directly and
deliberately bypasses `bus_read_hook`, so the selected 32K is kept
physically resident there and `abc806_note_instruction_fetch()` is called
from the step loop with each instruction's own PC — which is exactly what
MAME's `m1_r` latches, at the same granularity.

### The high-resolution window — where the code runs from is the switch

**When the instruction currently executing was fetched from
`0x7800`-`0x7FFF`, accesses below `0x7800` go to the high-resolution plane
instead of ROM.** Both reads and writes. Nothing is switched: no port is
written, no latch bit changes, no bank register moves.

The physical address is `(HRS & 0xF0) << 11 | (addr & 0x7FFF)` — the CPU's
window uses HRS's *high* nibble.

Consequences worth knowing before touching anything here:

- The ROM's own plane-clearing memset (`LD (HL),0` then `LDIR` at `0x7CB2`)
  lives at `0x7CAC`, *inside* the window, so its reads and writes both
  land in the plane and the propagate works.
- `FGLINE`'s plotter at `0x7E31` is in the window; `FGPOINT`'s executor at
  `0x763B` is not, and `FGPOINT` correctly draws nothing.
- **Bound the fetch PC on both sides.** Testing only `>= 0x7800` admits all
  of high RAM and diverts ordinary RAM code's reads.

**This rule exists in MAME only as a commented-out TODO** in
`read_pal_p4()` ("0..30k read from videoram if fetch opcode from
7800-7fff"), and that sketch contradicts itself — its condition tests
`!m1l`, meaning *this* access is an opcode fetch, while its comment
describes diverting because the opcode *was* fetched from the window. The
comment matches the hardware.

### The page map and EME

A 16-entry map at port `0x34`, indexed by the **high address byte** (which
for `OUT (C),r` is register B, not the port number). **Entries are stored
inverted**: MAME reads them as `m_map[page] ^ 0xff` before testing ENL in
bit 7, so a raw entry of zero — what the map holds at reset — means "do not
divert". With the polarity reversed, enabling EME sends every access to
video RAM and the machine dies thousands of instructions later on an
illegal opcode, nowhere near the cause.

In practice this ROM writes all 256 index values once at boot, every one
zero, and never touches the map again. EME is enabled once
(`LD A,80h / OUT (36h),A` at `0x00DC`) and never disabled.

### KEYDTR

DART channel B's DTR. With it low, `0x0000`-`0x7FFF` is the
high-resolution plane rather than ROM. This ROM writes WR5 once (`0x68`,
deasserting DTR) and never changes it, so the path is modelled but unused.

## I/O port map

| Port(s) | Device |
|---|---|
| `0x00`-`0x07` | ABC-bus. On *write*, `0x06` is HRS and `0x07` is HRC |
| `0x20`-`0x2F` | Z80 DART (keyboard, and channel B's modem-status inputs) |
| `0x31`, `0x38`, `0x39` (+mirrors) | MC6845 CRTC address/data |
| `0x34` | Page map, indexed by the high address byte |
| `0x35` | Attribute latch |
| `0x36` | 74ALS259 addressable latch |
| `0x37`, `0x3F` | HRU II PROM low nibble + RTC data line (read); config (write) |
| `0x40`-`0x5F` | Z80 SIO/2 |
| `0x60`-`0x7F` | Z80 CTC |

**The mirrors are load-bearing.** The boot ROM writes the CTC's interrupt
vector to port `0x64`, not `0x60`, so decoding only the literal documented
ranges silently drops it. Equally, `0x34`-`0x36` have **no** low-byte
mirror — MAME gives them `select(0xff00)`, meaning the high byte varies and
carries a register index while the low byte is exact. Decoding them as
`port & 0x3F` also claims `0x74`, `0xB4` and `0xF4`, which are CTC mirrors.

### The 74ALS259 at `0x36`

One `OUT` sets one bit. **The bit index is `value & 7` and the state is bit
7** — confirmed from the ROM's own idiom at `0x7543` (`LD A,08h / RRA`
rotates the carry into bit 7 and leaves `0x04` beneath it), not inherited
from the ABC802, which the family does not use one convention across.

| Bit | Signal | Modelled |
|---|---|---|
| 0 | EME | yes |
| 1 | 40-column select | yes |
| 2 | HRU II address A8 | yes — but see below |
| 3 | PROT INI | no |
| 4 | TXOFF | no — but see below |
| 5 | RTC chip select (inverted on the way to the chip) | yes |
| 6 | RTC clock | yes |
| 7 | RTC data out | yes |

**Bits 2 and 4 are probably both HRU II address lines.** The option PROM's
only latch writes (`0x7546`, `0x754D`) set exactly those two from two bits
of a value and then read port `0x37`. Recorded rather than acted on: nothing
reads the palette PROM for real yet, so a correct change would be
indistinguishable from a plausible one.

### Port `0x37`

On read, one port serving two devices: the HRU II PROM supplies the low
nibble and the real-time clock's data line bit 7. **Returning a constant
here is not "unimplemented" but a data line stuck high**, which is what made
the DOS print `19é5-é5-é5`.

On write, a boot-time configuration value. At `0x0418` the ROM reads DART
channel B's RR0, tests bit 5 (CTS), and writes `09` or `0A` accordingly —
the same channel and one of the same pins the ABC802 uses for its two
configuration DIP switches. Currently dropped.

## Video

### Text

A programmable MC6845; the ROM configures 80x25. Character cells are 6
pixels wide and 10 scanlines tall. Character RAM is 2K at `0x7800`, with a
**parallel 2K attribute plane** written as a side effect of a character-RAM
write through a latch on port `0x35` — the attribute plane is not
addressable directly.

**An attribute byte whose foreground and background match is a command, not
a colour.** `(attr & 7) == ((attr >> 3) & 7)` selects a command in bits
7:6: keep previous, reserved, blank, double width. Black on black is
therefore unreachable as an ordinary attribute, which is what makes the
encoding work.

Three further rules, none guessable:

- **Underline, flash and double height are never drawn.** They index the
  `RAD` PROM, which answers with a *scanline address*, and the character
  generator is addressed with that instead of the real row. The cursor is
  the same trick at scanline `0x0F`.
- **Double width is described by the cell before it.** A command-3
  attribute takes e5/e6 from its own low bits and the colours from the
  *next* cell's attribute byte, and the renderer then skips a column.
- **The glyph is six bits from the top of the font byte after a two-place
  left shift.** The low two bits are not pixels.

### The eight pens

Black, red, green, yellow, blue, magenta, cyan, white — **which is exactly
ANSI's own order**, so a pen index is `30 + index` for a terminal
foreground with no mapping table. A coincidence, and worth knowing before
someone "fixes" it.

### High resolution

240 rows of 128 bytes in the 128K video RAM, with **HRS's low nibble
selecting the displayed bank** (VM15-VM18) and its high nibble the bank the
CPU writes through (F15-F18). They are independent on purpose: the machine
can draw into one area while showing another.

**One byte becomes four pixels, through two lookups.** Each nibble indexes
`hrc[]` (16 entries, written to port `0x07` and indexed by register B), and
each `hrc` entry is *itself* two pixels of four bits: bit 3 opaque, bits 2:0
the pen. So **the palette carries the horizontal resolution** — program both
halves of an entry alike and the plane is 240 wide, differently and it is
480.

**The layer is not simply on top.** A dot is drawn where its opaque bit is
set *or* where the text layer left black, so text punches through its own
foreground and neither plane needs a mask. A zero `hrc` therefore makes
every dot transparent and disables the layer, which is the state the machine
boots in — and why `FGCTL`, which programs the palette, is required before
anything drawn becomes visible.

The plane sits **16 pixels left of text column 0**.

### The BASIC graphics commands

`FGPOINT`, `FGLINE`, `FGFILL`, `FGCTL`, `FGPAINT`, `FGPICTURE`, read out of
the option PROM's own keyword table.

Coordinates are y-flipped (`239 − y`) with a +8 viewport origin the ROM
keeps at `0xFEF8`. **A pen argument is masked to two bits and selects the
plane nibble `0xC | (pen & 3)`** — a four-colour mode, so pen 4 wraps onto
pen 0's nibble. `FGCTL 1` programs `hrc[D..F]` all to `FF`, which is white
for every pen; `FGCTL 2` gives pens 1, 2 and 3.

## Real-time clock

An E050-16, with **no bus at all**. Three bits of the 74ALS259 drive chip
select, clock and a bidirectional data line, and the line reads back as bit
7 of port `0x37`. One register read is thirty-odd `OUT`s and `IN`s.

CS low arms a four-bit command — address in bits 3:1, read/write in bit 0.
Address 7 is not a register but a mode: continuous read-out of all seven
registers as one 56-bit transfer, and it is the one the DOS uses.

Two details are not tidy-uppable. **Reads move on the falling clock edge and
writes on the rising one** — get it wrong and the value is plausibly shaped
and off by one bit position. And **CS is inverted between the latch and the
chip**, so a *set* latch bit deselects it.

The ABC806 ties OUTSEL high, deleting the chip's high-impedance read state.
That simplification belongs to this board, not to the chip.

## Interrupts

Z80 CTC, SIO/2 and DART on an IM 2 daisy chain. The block I/O instruction
group (`INI`/`INIR`/`OUTI`/`OTIR` and friends) is genuinely needed here —
the ROM drives its DART and CTC through `OTIR`/`OUTI` — and was missing from
the shared core until this family's ROMs exercised it. ZEXALL/ZEXDOC test no
I/O at all and could never have caught it.

**DART channel B's RI must be driven** or the machine hangs having booted
perfectly. The option PROM's routine at `0x7617` is a delay loop that spins
until RR0 bit 4 *changes* (`IN A,(C) / XOR B / AND 10h / JR Z`). `ports.c`
drives it from a 50 Hz square wave; that is **inference**, not read out of
MAME, which drives channel A's RI and channel B's CTS and leaves this alone.

## Character set

The Swedish/Finnish ISO 646 variant (SEN 850200 Annex B), as on the ABC80
and ABC802: the positions ASCII uses for `[]\^` and `` `{|}~ `` carry
ÄÖÅÜ / äöåü, and `@` carries É.

## ROM and PROM inventory

Eight 4K sockets make the 32K: six BASIC PROMs at `0x0000`-`0x5FFF`, a DOS
PROM at `0x6000` and an option PROM at `0x7000`. Plus a 4K character
generator, the 512-byte `RAD` and `HRU-II` PROMs, `HRU-I`, `V50`, and two
PALs.

See [`../resources/rom/README.md`](../resources/rom/README.md) for
provenance — **all sixteen** images are verified byte-for-byte against
MAME's published CRC32 *and* SHA1, the two PALs included. Those two needed
converting first: the archive ships JEDEC ASCII while MAME stores the
260-byte binary its `jedparse` produces, so
[`scripts/jed2bin.py`](../../scripts/jed2bin.py) does the conversion and
prints the checksums.

**The memory map is decided by a PAL16L8** (`ABC-P4-1.bin`, a well-formed
JEDEC fuse map). `emu/src/memory.c` follows MAME's behavioural form of it
instead, which is also where MAME's own `abc806 30K banking` TODO lives, so
evaluating the real fuse map is an open opportunity rather than a settled
question.

Its pinout, from MAME's own comment (outputs marked `>`):

| Pin | Signal | | Pin | Signal |
|---|---|---|---|---|
| 1 | I3 | | 11 | XML |
| 2 | A15 | | 12 | >ROMD |
| 3 | A14 | | 13 | HRAL |
| 4 | B13 | | 14 | HRBL |
| 5 | B12 | | 15 | KDL |
| 6 | B11 | | 16 | >HRE |
| 7 | M1L | | 17 | RKDL |
| 8 | EME | | 18 | >MUX |
| 9 | ENL | | 19 | >RAMD |
| 10 | GND | | 20 | Vcc |

Two details there are suggestive and unconfirmed. The address inputs are
split **`A15`/`A14` but `B13`/`B12`/`B11`** — two different prefixes for
what MAME's own model feeds from one address — and `M1L` is an input
alongside them. Between them those are the ingredients of the
fetch-window rule this emulator implements, which was derived from
behaviour rather than from the fuse map, so evaluating the array would be
an independent check on it.
