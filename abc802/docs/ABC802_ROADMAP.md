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

A multi-line program is entered, stored and run correctly too, which
exercises BASIC's program storage and control flow rather than just its
immediate-mode evaluator:

```
$ bin/abc802 --columns 80 --cycles 400000000 --screen \
      --type "10 FOR I=1 TO 5
20 PRINT I;I*I
30 NEXT I
RUN
"
|ABC802
|10 FOR I=1 TO 5
|20 PRINT I;I*I
|30 NEXT I
|RUN
| 1  1
| 2  4
| 3  9
| 4  16
| 5  25
|ABC802
```

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

## Milestone 2: live interactive keyboard and screen — done

`bin/abc802 --interactive` is a real session: keystrokes reach BASIC as
they are typed, the screen redraws continuously at ABC802 speed, and the
cursor blinks. Previously input arrived only through `--type`, a fixed
string fed at a paced rate, and the screen was a single dump printed
after the run ended.

What it does:

- **Raw terminal input.** `termios` with `ICANON`/`ECHO` off (the ROM
  does its own echo through character RAM), `ICRNL`/`INLCR`/`IGNCR` off
  so a real Enter arrives as `0x0D`, and `IXON` off so `Ctrl-S`/`Ctrl-Q`
  are not swallowed as flow control. `VINTR` is set to `_POSIX_VDISABLE`
  so `Ctrl-C` reaches BASIC as a plain `0x03` byte instead of killing the
  emulator; `Ctrl-\` (SIGQUIT) is the tool's own quit key. The original
  terminal mode is restored through `atexit()`, and SIGINT/SIGQUIT are
  caught rather than defaulted so an external `kill -INT` cannot leave a
  user's shell in raw mode.
- **Real-time pacing.** Execution is throttled to the real 3 MHz clock,
  corrected every 500 instructions rather than every one — `clock_gettime()`
  and `nanosleep()` are real syscalls and three million of each per second
  would swamp the emulation. `--cycles` is uncapped in this mode unless
  given explicitly.
- **A live screen**, redrawn at up to 30 fps, with per-character inverse
  video (bit 7) and the real cursor — neither of which the static
  `--screen` dump shows, because neither means anything without motion.
  `--screen`'s own output is byte-for-byte unchanged.
- **Swedish letters.** Å/Ä/Ö/Ü/É and lowercase arrive from a host
  terminal as 2-byte UTF-8 and are translated to the machine's ISO 646
  codes. One table in `render.c` drives both this and the screen decode,
  so a letter the display can show is always a letter the keyboard can
  type.

Verified end to end, at both column widths:

```
$ printf 'PRINT "\xc3\x85\xc3\x84\xc3\x96"\r' | bin/abc802 --interactive --columns 80
ABC802
PRINT "ÅÄÖ"
ÅÄÖ
ABC802

$ printf '10 FOR I=1 TO 3\r20 PRINT I;I*I\r30 NEXT I\rRUN\r' | bin/abc802 --interactive
ABC802
10 FOR I=1 TO 3
20 PRINT I;I*I
30 NEXT I
RUN
 1  1
 2  4
 3  9
ABC802
```

### Things this milestone found the hard way

- **The cursor blink comes from the ROM, not from the emulator.**
  Tracing the real ROM's CRTC writes shows it toggling MC6845 R10
  between `0x09` (cursor mode 00, non-blink/displayed) and `0x29`
  (mode 01, non-display) continuously, driven by its own 93.75 Hz clock
  interrupt — it blinks the cursor in *software*. So `--interactive`
  needs no blink-rate constant at all, unlike `bin/abc80`, which has to
  supply its own `ABC80_BLINK_HZ` (3.125 Hz, from MAME's blink timer)
  because that machine blinks in hardware. Honoring R10 and pacing
  execution correctly is the whole implementation; the measured result
  is a ~2.7 Hz blink that the firmware, not this code, decides.
- **Live input has to be paced exactly like `--type` is.** The DART holds
  a single receive byte. A human at a keyboard cannot outrun that, but a
  pipe or a paste delivers a whole line at once, and the bytes then
  overwrite each other. Piping `PRINT 6*7` into the first version of
  `--interactive` reached BASIC as *nothing at all*. The fix is the same
  ~0.1s (300,000 T-state) inter-key gap `--type` already used; unsent
  input simply waits in the host's own terminal/pipe buffer, so nothing
  is dropped, it is just drained at the speed the real machine could
  accept it.
- **That gap must not apply to multi-byte sequences.** A UTF-8 lead byte
  returns "nothing yet" and needs its continuation promptly — those
  sequences time out after 0.05s of *real* time, which is shorter than
  the key gap is at real ABC802 speed. Restarting the gap on a
  partial read would expire every one of them and no accented letter
  would ever arrive. The gap therefore restarts only when a byte is
  actually delivered.
- **The host Backspace key sends DEL, and this ROM does not want DEL.**
  The line editor implements a real destructive delete on `0x08`
  (`PRINT 12` + two `0x08` + `3` evaluates to `3`) but treats `0x7F` as
  an ordinary printable character, echoing a blank into the line
  (`PRINT 12` + two `0x7F` + `3` leaves `PRINT 12  3`). Untranslated, a
  user's Backspace would silently corrupt what they typed rather than
  erase it, so DEL is rewritten to BS. Both behaviors were established by
  probing the real ROM, not assumed from the ABC80's.
- **The ABC802's line editor is not the ABC80's.** The ABC80 target
  translates host arrow keys to `0x08`/`0x09`, grounded in a disassembly
  of *that* ROM's editor. Probing this one with the obvious candidates
  found no non-destructive cursor-right: `0x09` and `0x1F` are ignored
  and `0x0C` clears the screen. Rather than invent a mapping, arrow keys
  are dropped and this is recorded as a known gap below.


## Known gaps

Real, understood, and deliberately not solved yet — not oversights.

- **No GTK front-end.** `--interactive` (Milestone 2) covers live
  keyboard and a live screen in a terminal, but there is no equivalent of
  `bin/abc80-gtk`. That needs pixel rendering first — see the next gap.
- **Arrow keys are dropped.** A host terminal's cursor keys arrive as ESC
  sequences and are discarded rather than translated: probing the ROM
  found no byte that acts as a non-destructive cursor-right (see
  Milestone 2's findings). Backspace works. Closing this properly means
  disassembling the ROM's own line editor the way the ABC80 target's was,
  rather than guessing.
- **No pixel rendering.** Both renderers print characters as text. The
  character generator ROM is loaded and verified but not yet decoded into
  pixels, so the row attributes (Row Graphic, Row Flash, Row Clear) are
  not reproduced. Per-character inverse video *is* now shown, in
  `--interactive`'s live frame, as terminal reverse video. The ABC802 has
  no bitmap mode, so a text dump is a complete rendering of *which*
  characters are on screen — just not of how they look.
- **The CRTC is a register file, not a timing model.** Rendering reads
  character RAM on demand rather than reproducing the real scanline fetch,
  and no vertical-sync interrupt is generated. The cursor *does* animate
  now — the ROM blinks it in software through R10, so it needs no
  scanline model (Milestone 2) — but anything genuinely tied to field
  timing, including the hardware cursor-blink modes (R10 bits 6:5 = 10/11)
  and the character generator's Row Flash attribute, still is not.
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

None committed. Milestone 2 closed the first item that stood here. The
remaining candidates, roughly in order of how much they would add:

1. **Pixel rendering from the character ROM**, which would bring the row
   attributes (Row Graphic, Row Flash, Row Clear) to life and is the
   prerequisite for any GTK front-end.
2. **ABC-bus floppy support**, reusing the ABC80 target's existing,
   disassembly-grounded understanding of the same drives.
3. **The ROM's line editor**, disassembled the way the ABC80's was, to
   settle what its cursor keys actually want and close the arrow-key gap
   above.
