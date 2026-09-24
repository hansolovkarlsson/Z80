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

### Line editing

The ROM's line editor recognizes **five control codes, and every byte with
bit 7 set ends the line**. Established by feeding it every byte
`0x00`-`0x1F` plus a sample across `0x80`-`0xFF` and reading back what each
did to a typed line, rather than by disassembly. That routine is only
entered indirectly, so a reachability-based disassembler cannot reach it.

| Code | Key | Effect |
|---|---|---|
| `0x03` | Ctrl-C | terminates the line (break) |
| `0x08` | Backspace | destructive delete-left |
| `0x0C` | Ctrl-L | clears the screen |
| `0x0D` | Return | terminates the line |
| `0x18` | Ctrl-X | discards the whole line |
| `0x80`-`0xFF` | | terminates the line, keeping what was typed |

Every other byte is either ignored or, if printable, appended. `0x0A` is
ignored.

**Corrected 2026-09-24.** This table first listed `0x0A` as a terminator
and said `0x80`-`0xFF` behave like their low equivalents. A resweep through
`--interactive`, which hands the DART each byte unaltered, shows `0x0A`
ignored and every sampled high byte (`0x80`, `0x88`, `0xC1`, `0xE0`,
`0xFF`) ending the line. The ROM programs the keyboard channel for 8-bit
characters (`WR3 = C1`), so bit 7 reaches it on real hardware too.
`--type` turns `0x0A` into `0x0D` and drops a raw high byte, so a sweep
through it could test neither, and it would produce exactly the `0x0A`
result first recorded. The original commit does not say which path it
used. `0x7F`
(DEL) is **not** a delete: it is treated as an ordinary character and
echoes a blank into the line, which matters because that is what a modern
terminal's Backspace key actually sends.

Two consequences worth stating plainly:

- **There is no cursor movement.** No non-destructive left, no right,
  and the high byte range only ends the line. This is a simpler editor than the
  ABC80's, which does have a non-destructive cursor-right at `0x09`. An
  emulator front-end has nothing to map a right-arrow key to.
- **Editing is delete-and-retype.** Backspace to the mistake and type the
  rest of the line again, or Ctrl-X and start over.

## Z80 SIO/2

Ports `0x40`-`0x43`, with bit 1 selecting the channel and bit 0 selecting
control vs data — the same layout as the DART.

| Channel | Attached on real hardware |
|---|---|
| A | the machine's second RS-232 port |
| B | the **cassette** interface |

MAME wires the cassette's input to the SIO's `rxb`, drives its output from
`txdb`/`rtsb`, and runs the motor from `dtrb`. `bin/abc802` models the
chip's registers but attaches neither device, so nothing is ever received
and transmitted bytes are discarded.

**Two configuration DIP switches arrive as channel B modem-status inputs**,
not through anything memory-mapped, which is why this chip cannot simply
report a constant:

| Switch | Line | RR0 bit | Meaning | Default |
|---|---|---|---|---|
| S1 | channel B DCD | 3 | clear-screen time out | off |
| S2 | channel B CTS | 5 | unknown (undocumented in MAME too) | on |

So an idle channel B reads RR0 = `0x24`: transmit buffer empty (bit 2)
plus CTS from S2.

Registers behave as the Z80 SIO datasheet describes. WR0 carries the next
register pointer in bits 0-2 and a command in bits 3-5 (only channel
reset, command 3, has an observable effect here); reading a status
register or writing a non-WR0 register returns the pointer to 0. RR0 is
status, RR1 reports `all sent` and no error conditions, and **RR2 is valid
on channel B only** — channel A returns 0 rather than a plausible-looking
vector, so reading the wrong channel gives an obviously wrong answer
instead of a subtly right-looking one.

Transmit is always reported empty. With nothing attached, a byte written
to the data port has by definition gone as far as it is ever going to, and
a ROM polling loop waiting on that bit must see it set or it never exits.

The boot ROM programs channel B only — WR1, WR3 and WR5, then a
reset-external-status command — and reads RR0 once. It never touches
channel A.

Because no device can raise one, **the SIO never generates an interrupt**,
so its position in the CTC → SIO → DART daisy chain is currently inert.

## ABC-bus

The same expansion bus as the ABC80, with the same ABC830/832/834-class
floppy drives.

The host drives it through six ports (see the I/O map): CS selects a card
by its address, OUT/INP move bytes, STAT reports readiness, and C1/C3
pulse attention and reset. A transaction is a four-byte command header
followed by a 256-byte sector transfer, with `INI`/`OUTI` block moves
gated on the status byte.

**Device-select codes.** They come from a device table in the DOS ROM,
four bytes per entry (a select byte, one byte not yet identified, then the
two-letter device prefix), which the DOS's init at `0x6C85` copies with
`LDIR` into RAM at `0xFDE2`. It sits at `0x6ED3` in v.20
(`ABC802-dos.32-31.bin`) and `0x6EDB` in v.19 (`ABC802-dos.32-21.bin`),
whose copy is `LD HL,6EDBh` at `0x6C8D`. The select routine at `0x6184`
indexes that RAM copy and `0x6172` masks the result with `AND 3Fh` before
`OUT (01h),A`, so the upper two bits of each select byte never reach the
bus:

| Entry | Select byte | On the bus | Device |
|---|---|---|---|
| `DR` | `0x08` | (none) | the default: an offset to the `MF` entry |
| `HD` | `0x24` | `0x24` | hard disk |
| `MF` | `0x6C` | `0x2C` | ABC832/834 floppy, 640K |
| `MO` | `0xAD` | `0x2D` | ABC830 floppy, 160K |
| `SF` | `0x6E` | `0x2E` | 8-inch floppy |
| `??` | `0x00` | | two unnamed entries |
| `RM` | `0x00` | | |

The byte at `0xFD01` chooses the entry: `0x61B2` takes its low five bits
(or `0xFFF9` when they are `0x1E`), and `0x6184` masks those with `0x1C`,
so each entry is a multiple of four from `0xFDE2`. A zero index reads the
`DR` entry's select byte as the offset instead, which is how `DR` becomes
`MF`. What the second byte of each entry means, and what the top bits of
`MF`/`MO`/`SF`'s select bytes carry, are not known.

(These codes were once recorded as coming from a "select table" at
`0x61DA`-`0x61FB`. That range is code in both DOS images, where the `2Ch`
bytes are `INC L`; corrected on 2026-09-24.)

With no card answering, the boot ROM selects `0x24`, `0x2C`, `0x2D` and
`0x2E` twelve times each, then `0x00` twenty-four times, observed at
`OUT (01h),A` (`0x6177`) with `bin/abc802 --break 6177`. With an `MO` or
`MF` image attached it stops at that drive's code. **A status byte of `0x00`
or `0xFF` means "no device"** — the poll loop at `0x6196` aborts on either
(`INC A / JR Z`, `DEC A / JR Z`) — so a present card must never report
either.

**Status byte:**

| Bit | Meaning | Pinned by |
|---|---|---|
| 0 | ready to move a byte | `0x612D`/`0x6140` (`IN A,(01h) / RRCA / JP NC`) |
| 2 | must be clear | `0x610D` (`AND 05h / XOR 01h`) |
| 3 | the current command has **not** failed | ABC80 `0x6118`, `0x60E9` — see below |
| 7 | idle, ready for a command header | `0x6107` (`BIT 7,A`), `0x616F` |

An idle, healthy controller is therefore `0x89`, and one that has just
failed a command is `0x81`; failures themselves are reported through the
auxiliary status byte on the INP port, which the ROM tests with `OR A` at
`0x6158`.

**Bit 3 cannot be derived from this machine's ROM at all** — it never
reads the bit. It was modeled here as an error flag, on no evidence, and
is in fact the exact complement: the ABC80's ROM requires it *set* while a
transfer is pending (`BIT 3,A / JR Z` at `0x6118` treats clear as "this
command produced nothing") and *still set* once the command has finished,
which its write path returns on as success (`IN A,(01h) / CPL / AND 08h`
at `0x60E9`, `RET Z` at `0x60C1`). Sharing the controller with the ABC80
target is what exposed it.

`bin/abc802` models a synthetic controller for `MO` and `MF` (see
`ABC802_COMPLETED.md`'s Milestone 5); it lives in `abcbus/disk.c` and is
shared with the ABC80 target. With no `--disk` image attached no card is
fitted and every bus read floats high, which is the correct behavior for a
bare machine — and is exactly the `0xFF` the ROM reads as "nothing there".

**Sector interleave differs per drive and cannot be inferred**: `MO` needs
factor 7 (mask 15), `MF` needs none. Both were established by booting real
media both ways; a directory sector reads correctly under either mapping,
because track-boundary sectors map to themselves.

**But the factor is a property of the dump, not only of the drive.** Those
defaults are correct for abc80.net's `.img` archive, which stores ABC830
sectors in physical order. Other archives ship `.dsk` images of the same
media in plain logical order, and those need factor 0 — verified against
ten such images, where all nine 160K ones fail and the one 640K one works
untouched (`MF` defaulting to no interleave happens to match that
convention). Nothing inside an image says which it is, so `--interleave N`
overrides the default on both `bin/abc802` and `bin/abc80`. The symptom of
the wrong choice is worth memorizing, because it does not look like a
broken disk: the card is found, the directory lists correctly, and every
real file read returns `Error 37`.
