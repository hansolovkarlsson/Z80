# ABC802 emulator roadmap

Status and known gaps for `bin/abc802`, the Luxor ABC802 machine target.
Hardware facts live in `ABC802_REFERENCE.md`; this file is about what
works, what does not, and what was actually verified.

## Why a third machine target

The ABC802 shares this repository's proven Z80 core (`z80core/z80.o` +
`alu.o` via `z80_execute()`) with `bin/z80` and `bin/abc80`, on exactly
the terms `abc80/` already established. It is a genuinely different
machine from the ABC80 rather than a variant: a programmable CRTC, three
Z80 peripheral chips on an IM 2 daisy chain, a ROM/RAM overlay, and a
serial keyboard. See `ABC802_REFERENCE.md`'s comparison table.

It was chosen over the ABC800M and ABC806 deliberately: no bitmap
graphics mode, a single board, and a flat 64K of RAM make it the
simplest member of the family to bring up. MAME's own driver still lists
`abc806 30K banking` as an open TODO, which is a fair signal about where
that machine's documentation thins out.

## Milestone 1: boot the real ROM to a working BASIC — done

`bin/abc802` boots the real, unmodified ABC802 firmware (see
`../resources/rom/README.md` for provenance: every image verified
byte-for-byte against MAME's published CRC32 **and** SHA1), reaches its
sign-on banner, accepts typed keyboard input, and runs real BASIC.

Verified end to end, not asserted:

```
$ bin/abc802 --columns 80 --type "PRINT 6*7
" --screen
+--------------------------------------------------------------------------------+
|ABC802                                                                          |
|PRINT 6*7                                                                       |
| 42                                                                             |
|ABC802                                                                          |
```

That is the real ROM's own banner, its own echo of the typed line, its
own evaluation of the expression, and its own return to the prompt.

What that exercises: the 32K ROM image and its LRS/M1 memory overlay, the
MC6845 register file, the Z80 CTC (including its real 93.75 Hz system
tick and IM 2 vectoring to the ROM's own clock handler at `0x3A76`), the
Z80 DART's receive path and the two control outputs the ABC802 rewires as
machine control lines, the configuration DIP switches the ROM reads
through DART modem-status inputs, and the character-cell renderer.

### Things this milestone found the hard way

- **Block I/O instructions were missing from the shared core.** The boot
  ROM configures its DART and CTC with `OTIR`/`OUTI`, which halted the
  emulator immediately on an unimplemented opcode. `INI`/`INIR`/`IND`/
  `INDR` and `OUTI`/`OTIR`/`OUTD`/`OTDR` are now implemented in
  `z80core/alu.c`. ZEXALL/ZEXDOC could never have caught this: they
  exercise no I/O at all — the same blind spot
  `asm/examples/gaps_test.asm` exists to cover. Its **check 7** is the
  permanent regression test.
- **The I/O port mirrors are load-bearing.** The ROM writes the CTC's
  interrupt vector to port `0x64`, not `0x60`. Decoding only the literal
  `0x60-0x63` range silently dropped it and left every CTC interrupt
  vectoring through `0x0000`, which showed up as the machine resetting in
  a loop roughly 25 times per run.
- **The M1-decoded character-RAM overlay is not optional.** The ROM's own
  keyboard-input routines live at `0x7EB0`-`0x7F40`, inside the window
  where character RAM shadows the ROM.
- **The 40/80-column DIP switch is read through the DART**, so the
  emulator's DART status bytes decide the screen layout. With it clear,
  the ROM writes text into every other character cell — which is correct
  40-column behavior, and matches MAME's own default for this machine.
- **The Makefile did not track header dependencies.** Adding fields to
  `struct Z80` left every already-built object compiled against the old,
  smaller struct while `z80.o` used the new one, so `Z80 cpu = {0}`
  under-allocated and every CP/M program silently produced no output.
  `make clean` hid it. Now fixed with `-MMD -MP` plus `-include`, and
  verified by touching a header and watching the right objects rebuild.

## Known gaps

Real, understood, and deliberately not solved yet — not oversights.

- **No interactive mode.** Input arrives through `--type`, which feeds a
  fixed string at a paced rate; there is no live-keyboard mode like
  `bin/abc80 --interactive`, and no GTK front-end. The keyboard path
  itself is real (DART receive plus interrupt), so this is a front-end
  gap rather than a hardware-modeling one.
- **No pixel rendering.** `--screen` prints character codes as text. The
  character generator ROM is loaded and verified but not yet decoded into
  pixels, so the row attributes (Row Graphic, Row Flash, Row Clear) and
  per-character inverse video are not reproduced. The ABC802 has no
  bitmap mode, so a text dump is a complete rendering of *which*
  characters are on screen — just not of how they look.
- **The CRTC is a register file, not a timing model.** Rendering reads
  character RAM on demand rather than reproducing the real scanline fetch,
  and no vertical-sync interrupt is generated. The ROM's cursor and
  flash-clock behavior therefore is not animated.
- **The SIO is a stub.** Reads report "transmit buffer empty, no receive
  data" so ROM polling loops exit. Nothing is attached to either channel,
  so the RS-232 ports and cassette do not work.
- **No ABC-bus card.** Every ABC-bus read returns `0xFF`, so the ROM's
  controller scan finds nothing and no disk is available. The ABC802 uses
  the same bus and the same ABC830/832/834-class drives the ABC80 target
  already talks to, so `abc80/emu/src/disk.c`'s protocol work is the
  obvious starting point if this is picked up.
- **The "pling" speaker strobe is decoded but silent.** No audio.
- **The DART/SIO/CTC are modeled only as far as the boot path needs.**
  Baud-rate generation, transmit interrupts, and the SIO's own vectors
  are absent.
- **Frame frequency is ambiguous** — MAME's DIP label and the code
  comment that consumes it disagree; see `ABC802_REFERENCE.md`. Nothing
  currently depends on it.

## Planned next steps

None committed. The natural candidates, roughly in order of how much
they would add:

1. **Live interactive keyboard**, matching `bin/abc80 --interactive` —
   the hardware path already works, so this is mostly host-terminal
   plumbing that target has already solved once.
2. **Pixel rendering from the character ROM**, which would bring the row
   attributes and inverse video to life and is the prerequisite for any
   GTK front-end.
3. **ABC-bus floppy support**, reusing the ABC80 target's existing,
   disassembly-grounded understanding of the same drives.
