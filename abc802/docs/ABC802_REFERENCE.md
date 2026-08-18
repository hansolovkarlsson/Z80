# ABC802 hardware reference

Consolidated hardware reference for the Luxor ABC802, the machine
`bin/abc802` emulates. Companion to `ABC802_ROADMAP.md` (status and open
gaps) and to `abc80/docs/ABC80_REFERENCE.md` (the older, quite different
ABC80).

Facts here are either derived from MAME's own ABC802 implementation
(`src/mame/luxor/abc80x.cpp` and `abc80x_v.cpp`, BSD-3-Clause, Curt
Coder) or confirmed empirically against the real ROM in this emulator.
Where the two disagree, or where something is inferred rather than
observed, it is called out explicitly.

## Overview

The ABC802 (1983) is a Z80A machine in the ABC800 family — a later,
consolidated single-board design. Compared with the ABC80 target already
in this repository it is a substantially more complex machine: three Z80
peripheral chips on an interrupt daisy chain, a programmable CRTC instead
of fixed video timing, and a ROM/RAM overlay instead of a fixed memory
map.

| | ABC80 (1978) | ABC802 (1983) |
|---|---|---|
| CPU | Z80 @ 3 MHz | Z80A @ 3 MHz |
| RAM | 16K (32K option) | 64K |
| ROM | 16K BASIC | 32K BASIC II + DOS |
| Video | fixed-timing, 40×24 | MC6845 CRTC, 80×24 or 40×24 |
| Graphics | block mosaic | character cells only (no bitmap) |
| Keyboard | scanned matrix | serial, on the DART |
| Interrupts | one periodic PIO | CTC/SIO/DART daisy chain, IM 2 |

The absence of a bitmap graphics mode is the main thing separating the
ABC802 from the ABC800M/806, and is why a text-mode renderer is a
complete rendering of its screen rather than an approximation.

## CPU and clock

Z80A at 3 MHz, derived as X01/4 where X01 is a 12 MHz crystal.

## Memory map

Two 32K halves that genuinely overlay each other:

| Range | Contents |
|---|---|
| `0x0000-0x7FFF` | 32K ROM **or** the low 32K of RAM, selected by LRS |
| `0x7800-0x7FFF` | 2K character RAM, overlaid under the ROM (see below) |
| `0x8000-0xFFFF` | 32K RAM, always |

**LRS** ("Low RAM Select") is driven by the DART's DTR-B output, not by
any memory-mapped register. The machine powers up with ROM selected,
which is what makes the reset vector at `0x0000` fetch ROM.

**The character-RAM overlay is decoded by the M1 line**, not by an
address bit or a bank register. Within `0x7800-0x7FFF`:

- an **instruction fetch** reads ROM;
- a **data read** reads character RAM;
- a **write** always goes to character RAM (an EPROM has nothing to
  write to, so there is no ambiguity).

This is not a detail that can be skipped. The boot ROM has real code in
that window — the routines that handle received keyboard characters live
at `0x7EB0`-`0x7F40` — so without the M1 distinction that code reads back
as character RAM and the machine never runs. See `emu/src/memory.c` for
how this emulator reproduces it without modifying the shared CPU core.

## I/O port map

Only the low 8 bits are listed; the "mirror" column gives the bits the
real hardware does not decode, so each device answers at many addresses.

| Port | Direction | Device | Mirror |
|---|---|---|---|
| `0x00` | r/w | ABC-bus INP / OUT | `0xff18` |
| `0x01` | r/w | ABC-bus STAT / CS | `0xff18` |
| `0x02`-`0x05` | w | ABC-bus C1-C4 | `0xff18` |
| `0x05` | r | "pling" speaker strobe | `0xff18` |
| `0x07` | r | ABC-bus RST | `0xff18` |
| `0x20`-`0x23` | r/w | Z80 DART | `0xff0c` |
| `0x31` | r | MC6845 register read | `0xff06` |
| `0x38` | w | MC6845 address latch | `0xff06` |
| `0x39` | w | MC6845 register write | `0xff06` |
| `0x40`-`0x43` | r/w | Z80 SIO/2 | `0xff1c` |
| `0x60`-`0x63` | r/w | Z80 CTC | `0xff1c` |

**The mirrors are load-bearing.** The real boot ROM writes the CTC's
interrupt vector to port `0x64`, not `0x60`. Decoding only the literal
ranges silently drops that write and leaves every CTC interrupt
vectoring through address `0x0000`. Found by tracing real ROM I/O, not by
reading the masks.

## Interrupts

IM 2 throughout, with the daisy chain **CTC → SIO → DART** (highest
priority first). The ROM sets `I` to `0xFF`, so the vector table lives at
`0xFF00-0xFFFF` in RAM.

Observed vectors from a real boot:

| Source | Vector | Table entry | Handler |
|---|---|---|---|
| CTC channel 3 | `0xD6` | `0xFFD6` | `0x3A76` |
| DART channel B receive | `0xB4` | `0xFFB4` | (keyboard input) |

The CTC's base vector is written to channel 0 with bit 0 clear; the chip
supplies bits 1-2 to identify the interrupting channel. The DART's base
vector is written to channel B's WR2 and modified with a 3-bit condition
code in bits 1-3 — channel B receive-character-available is code 2, hence
`0xB0 | 0x04`. The ROM copies ten bytes (five word-sized entries) to
`0xFFB0`, which is what confirms this encoding rather than leaving it as
datasheet inference.

### System tick

CTC channel 3, timer mode, prescaler 256, time constant 125 →
3 MHz / (256 × 125) = **93.75 Hz**. The ROM's own handler at `0x3A76`
counts to `0x5D` (93) before rolling over into the seconds counter, which
independently confirms the rate: it expects very close to 93 ticks per
second.

## Video

MC6845 CRTC driving a character-cell display. No bitmap mode.

Register values the real ROM programs at boot:

| Reg | Value | Meaning |
|---|---|---|
| R0 | `0x7F` | horizontal total |
| R1 | `0x50` | horizontal displayed (80) |
| R6 | `0x18` | vertical displayed (24) |
| R9 | `0x09` | scanlines per character row − 1 (10 rows) |
| R12/R13 | `0x7800` | display start address |

The display start address is `0x7800` — the same address the CPU sees the
character RAM at. Character RAM is 2K, so the CRTC's address is used
modulo `0x800`.

### 40 vs 80 columns

The CRTC always counts 80 character cells per row. In **40-column mode**
the video hardware draws every *other* cell at double width and skips the
one between, so the ROM lays its text out in the even cells only. Reading
all 80 cells in that mode shows a space after every character — which is
exactly how this was diagnosed.

Which mode is active comes from a **configuration DIP switch (S3) that
the ROM reads through a DART modem-status input**, not from software:

| Switch | Delivered via | Meaning |
|---|---|---|
| S1 | SIO channel B DCD | clear-screen time out |
| S2 | SIO channel B CTS | unknown (undocumented in MAME too) |
| S3 | DART channel A RI | 0 = 40 columns, 1 = 80 |
| — | DART channel B CTS | frame frequency |

MAME's own default for S3 is 40 columns; `bin/abc802` matches that
default and offers `--columns 80`.

**Unresolved**: MAME's frame-frequency DIP labels `0x08` as "50 Hz" while
the code comment on the line that delivers it reads `0 = 50Hz, 1 = 60Hz`.
The two disagree, and nothing in this emulator currently depends on
which is right, so it is recorded here rather than guessed at.

### Attributes

Attributes are not stored in a separate cell. The *character generator
ROM's output byte* carries them: bit 7 (`ATE`) marks the byte as an
attribute rather than pixel data, bit 6 (`ATD`) is the value, and bits
0-1 select which attribute — Row Graphic, Row Flash, or Row Clear. Bit 7
of the character code itself (`INV`) is per-character inverse video.
`bin/abc802`'s text renderer honors the `INV` bit's separation from the
character code but does not yet reproduce the row attributes; see
`ABC802_ROADMAP.md`.

## Character set

The same Swedish/Finnish ISO 646 variant (SEN 850200 Annex B) as the
ABC80: the positions ASCII uses for `[ \ ] ^ \` { | } ~` carry
`Ä Ö Å Ü é ä ö å ü` instead, and `@` carries `É`.

## Keyboard

A **serial** device on DART channel B — an ABC55 or ABC77 keyboard with
its own microcontroller — not a scanned matrix like the ABC80's. A
keypress arrives as a received character plus a receive interrupt.

The boot ROM shows its sign-on banner and then waits indefinitely for a
key: the loop at `0x03F5` polls bit 7 of `0xFFE2`, which the keyboard
handler at `0x7Exx` sets. The first keypress is consumed to dismiss the
banner and is not echoed. The ROM also discards input while still
initializing immediately afterward, so keystrokes have to arrive at
roughly human speed to survive.

## ABC-bus

The same expansion bus as the ABC80, with the same ABC830/832/834-class
floppy drives. `bin/abc802` models no card: every ABC-bus read returns
`0xFF`. The boot ROM's controller scan (write `0x24` to CS, read STAT,
write C4) therefore finds nothing, which is the correct behavior for a
machine with no expansion card fitted.
