# ABC802: completed milestones

Finished work on `bin/abc802`, moved here out of
`ABC802_ROADMAP.md` so that file stays a short answer to "what works, what
doesn't, and what's next" rather than a growing archive. Nothing here has
been rewritten — these are the milestone write-ups as they were recorded
when the work was done, including the "found the hard way" notes, which
are the part most worth keeping.

For the hardware being modeled see `ABC802_REFERENCE.md`; for current
status and open gaps see `ABC802_ROADMAP.md`; for the cross-cutting
lessons see `../../docs/JOURNAL.md` and `../../docs/postmortems/`.

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


## Milestone 3: pixel rendering from the character ROM — done

The screen is now decoded into actual pixels, not just into which
character codes sit where. `bin/abc802 --screenshot FILE` writes a real
PNG of the display, and `bin/abc802-chargen-dump` verifies the decode
against a synthetic screen.

```
$ bin/abc802 --columns 80 --type "PRINT 6*7
" --screenshot boot.png
Screenshot: boot.png (480x240)
```

480x240 is the real geometry: 80 columns of 6-pixel-wide cells by 24 rows
of 10 scanlines, drawn in the machine's own amber phosphor on black
(MAME's `abc802_video()` asks for `rgb_t::amber()`, defined as
`(247, 170, 0)`). 40-column mode produces the same 480 pixels, each
character drawn double-width.

This closes the gap that blocked a GTK front-end: `abc802_render_pixels()`
in `chargen.c` is a pure function taking an `Abc802Screen` struct, so any
front-end — the PNG writer, a future Cairo widget, a test — feeds it state
and gets pixels back, with no path able to drift from another.

### How the attributes actually work

Grounded in MAME's `abc802_update_row()` (`src/mame/luxor/abc80x_v.cpp`),
which carries the real PAL16R4 equations from the video board, then
cross-checked byte-for-byte against the committed ROM.

The scheme is not guessable from a memory map: **the character generator
ROM's own output byte decides whether a cell is a character or an
attribute command.** If bit 7 (ATE) of the fetched byte is set, it is not
pixel data at all but an instruction — bit 6 (ATD) is the new value and
bits 1:0 select which attribute (0 = Row Graphic, 1 = Row Flash, 2 = Row
Clear). So the *font* defines which character codes act as attribute
codes. In this ROM exactly 17 do: `0x01`-`0x09` and `0x11`-`0x18`.
An attribute cell draws nothing.

All three attributes work by substituting the scanline address rather
than post-processing pixels:

| Attribute | Mechanism | Verified against the ROM |
|---|---|---|
| Row Graphic | ORs `0x800` into the ROM address, selecting the alternate 2K half — a block-mosaic font | exactly 63 codes (`0x21`-`0x3F`, `0x60`-`0x7F`) differ between halves; all others are byte-identical |
| Row Flash | forces scanline `0x0E` | `0x0E` is `0x00` for every printable code |
| Row Clear | forces scanline `0x0E` | same |
| Cursor | forces scanline `0x0F` | `0x0F` is `0x3F` — a solid 6-pixel bar — for every printable code |

So the real cursor is a **solid block substituted for the glyph**, not an
inversion of it. The cursor is applied first and flash/clear can then
override it, matching MAME's ordering. Pixels are bits 5..0 of the ROM
byte, most significant first.

The FLSH clock is derived rather than modeled: real hardware counts
vertical syncs and toggles every 33 fields, which at 50 Hz is 0.66s per
phase (~0.76 Hz). This emulator has no vsync, so the same rate is
expressed in T-states — 3,000,000/50 = 60,000 per field × 33 = 1,980,000.

### Why a separate verification tool

`--screenshot` alone cannot validate any of the above: the ROM's own boot
screen uses no Row Graphic, no Row Flash and no Row Clear, so a completely
broken attribute state machine would render it perfectly.
`bin/abc802-chargen-dump` (`make abc802-chargen-dump`) drives a synthetic
character RAM that uses all of them and prints the result as ASCII art,
with an optional `--png`. It shares the same decode, needs no CPU core,
and prints the ROM's attribute-code inventory so the table above can be
re-derived rather than trusted:

```
$ bin/abc802-chargen-dump
  0x08 -> Row Flash    = 1
  0x09 -> Row Flash    = 0
  0x11 -> Row Graphic  = 1
  0x18 -> Row Clear    = 1
  (17 attribute codes)
```

One trap the tool itself hit: an early version demonstrated "Row Graphic
switched off mid-row" using uppercase letters, which are byte-identical in
both ROM halves — it would have passed with the attribute ignored
entirely. It uses digits now, which genuinely differ.

The PNG writer (`png.c`) is hand-written, using DEFLATE *stored* blocks so
no compressor is needed, for the same reason `abc80/emu/src/sound.c`
writes its own WAV header: the default build of this project has no
third-party libraries. Output was validated against a strict decoder —
chunk CRCs, zlib adler32, filter bytes and palette all check out.


## Milestone 4: a GTK window — done

`bin/abc802-gtk` (`make abc802-gtk`) is a real GTK4 window: a Cairo pixel
framebuffer running the core in-process, the same shape as
`bin/abc80-gtk` and deliberately not the VTE-launcher shape of
`bin/z80-gtk` — a character-cell machine's screen is a bitmap, not a
terminal. See `../gtk/README.md` for the full write-up.

Two things made it small. First, Milestone 3's decode is a pure function,
so the app only turns its output into a Cairo surface — the mosaic font,
all three row attributes, inverse video and the hardware cursor come along
for free and cannot drift from what `--screenshot` produces. Second,
`abc802_step()` was extracted into `emu/src/step.h` so the CLI's
`--interactive` loop and the window share the per-instruction machine
logic (the M1 notification, the CTC tick, interrupt delivery) instead of
each keeping a copy — the same extraction ABC80's Milestone 11 did.

No SDL2 and no threads: `bin/abc80-gtk` needs both for live SN76477 audio,
and this machine's only sound is a speaker strobe the emulator does not
sound.

### Verification, and the flag that makes it possible

Automating `screencapture` against the user's real desktop is disruptive —
it steals focus and switches Spaces — a lesson `abc80/gtk/README.md`
already records. So this app was built to verify itself:
`bin/abc802-gtk --screenshot FILE` opens no window and never creates a
`GtkApplication`, but renders one frame through the *identical*
`draw_screen()` the live window uses, against an offscreen surface. Real
evidence about the real renderer, with no desktop involved.

Verified: clean `-Wall -Wextra` build; headless renders at both column
widths, including a multi-line BASIC program and Swedish letters; and a
launch/terminate cycle exiting 0 with an empty log (`SIGTERM` routed
through `g_unix_signal_add()` so shutdown runs on the main loop and a
`kill` cannot be mistaken for a clean exit).

### A bug this milestone found

`--type` fed its argument to the keyboard as **raw bytes**, so a shell
argument containing Å arrived as the two bytes of its UTF-8 encoding and
BASIC answered `Error 234.` — while the interactive keyboard paths, which
decode properly, handled the identical text correctly. The two disagreed
about what typing the same thing meant. Found by rendering
`PRINT "ÅÄÖ"` through the new headless path and looking at the result.
Fixed with one shared `abc802_utf8_to_chars()` (`render.c`, next to the
charset table it uses) now called by both `bin/abc802`'s `--type` and
`bin/abc802-gtk`'s — the pre-existing CLI defect included, since it was
the same bug.

## Milestone 5: ABC-bus floppy support — done

`bin/abc802 --disk FILE` and `bin/abc802-gtk --disk FILE` attach a real
160KB ABC830 floppy image on the ABC-bus. The machine boots from it and
runs real ABC800-family software, and BASIC can `SAVE` and `LOAD`
programs.

Built as the **synthetic ABC-bus controller** that
`ABC802_FLOPPY_SCOPING.md` recommended: `abcbus/disk.c` (then
`emu/src/disk.c`, before the ABC80 target began sharing it) models the six
bus ports and the controller's command state machine, serving 256-byte
sectors from an image file. It is a device model, not a PC-address trap
like the ABC80 target's — anything that talks to the bus correctly works,
including code paths nobody thought to intercept.

### Verified, at each of the scoping document's own four gates

**Gate 1 — the ROM's controller scan finds a device.** Previously the scan
probed selects `0x24`/`0x2C`/`0x2D`/`0x2E` twelve times each and read
`0xFF` every time. Now:

```
[out] 01 <- 2D      ; select the ABC830
[in ] 01 -> 81      ; idle + ready  (was FF = no card)
[out] 02 <- AD      ; C1 pulse
[out] 00 <- 03      ; command header: READ SECTOR + SECTOR TO HOST
```

**Gate 2 — reading real media.** Three real images from the abc80.net
ABC800 archive boot and run: `disk001` starts *ORD 800 Version 2.4*
("Ordbehandling med ABC 800", a word processor), `disk003` starts
*PROMMIS Ver 6.2*, an EPROM programmer with full Swedish text, and
`disk002` runs a Luxor game-menu program.

That the software genuinely came off the disk was checked rather than
assumed: the string `ORD 800` appears at sector 57 of the image and in
**none of the six committed ROM images**, and with no `--disk` the machine
still boots to the plain `ABC802` BASIC prompt.

**Gate 3 — the write path, across separate process runs.**

```
$ bin/abc802 --disk rt.img --type 'NEW / 10 PRINT "DISK ROUND TRIP OK" / 20 PRINT 6*7 / SAVE "MO0:RT"'
$ bin/abc802 --disk rt.img --type 'NEW / LOAD "MO0:RT" / LIST / RUN'
LOAD "MO0:RT"
LIST
10 PRINT "DISK ROUND TRIP OK"
20 PRINT 6*7
RUN
DISK ROUND TRIP OK
 42
```

**Gate 4 — interleave, settled by experiment.** The scoping document
recorded a genuine contradiction: this project verified ABC830 media is
sector-interleaved (factor 7) during ABC80's Milestone 6, while abc80sim
ships with interleave compiled *out*. Both cannot be right for the same
media. Rebuilding with the interleave disabled and changing nothing else,
the same image **stops booting entirely** and the machine falls back to
the bare BASIC prompt. With factor 7 it boots. The contradiction is
resolved in favour of this repository's own earlier finding, now confirmed
on a second machine and a different DOS ROM.

### Things this milestone found the hard way

- **A status of `0xFF` or `0x00` means "no device", and that is load-
  bearing.** The ROM's poll loop at `0x6196` does `IN A,(01h)` then
  `INC A / JR Z` and `DEC A / JR Z` — either value aborts the poll
  immediately. This is exactly why the previous "every ABC-bus read
  returns `0xFF`" behavior read as *no card fitted*, correctly and by
  accident. An idle, ready controller must report `0x81`: bit 7 for
  "ready for a command header" (what `L616F` waits on) and bit 0 for
  "ready to move a byte" (what the `INI`/`OUTI` loops at `0x612D`/`0x6140`
  test with `RRCA / JP NC`).
- **`LIB` does not exist on this machine, and the error was right.**
  Typing it returns `Error 220`, which looked like a controller failure.
  It is not: the DOS ROM's own command table at `0x6F80` contains exactly
  four entries — `BYE`, `KILL`, `NAME`, `AS`. Its device-name table at
  `0x6E40` (`DR1`, `DR2`, `UFD`, `MF0`-`MF2`, `MO0`, `MO1`, `SF…`) is
  where `MO0:` comes from. Read out of the ROM rather than guessed at
  after three wrong guesses at the syntax.
- **`--type` needed `--type-at`.** The ROM reports the keyboard ready very
  early in boot, but a program loading from disk is not listening yet and
  discards anything typed at it. Typing `LIB` at a disk-booted prompt
  arrived as `B`. `--type-at N` holds the text back until N T-states have
  run, which is what makes any scripted disk test reproducible.
- **A muddy test is worth redoing.** The first round-trip test typed a
  program into a session where the disk's own autoboot program was still
  resident, so `SAVE` stored a *merge* of both and `LIST` came back
  showing someone else's Luxor game menu with one line of mine in it. The
  round trip was genuinely correct, but nothing about that output
  demonstrated it. Redone with `NEW` first.

## Milestone 6: the ABC832/834 640K drive — done

`--disk` now takes 640KB images as well as 160KB ones, so the ABC802's own
native drive works and not just the ABC80-era one it inherited.

```
$ bin/abc802 --columns 80 --disk mf001.img --screen
ABC-bus: mf floppy controller, drive 0 = 'mf001.img'
| ADMINISTRATION 800   * företagsnamn saknas! *          2000-00-00 00.00.00 |
|              Datum  (ååmmdd) : ______                                      |
|              Tid    (ttmmss) : 000000                                      |
```

That is a real Swedish business application booting off a real 640KB dump
from the abc80.net ABC800 archive, asking for the date and time.

**Which controller is fitted is decided by the image's size**, not by a
flag. The two formats differ by a factor of four, a real dump is always
exactly one of the two sizes, and asking the user to restate something the
file already says is a good way to collect bug reports about the wrong
geometry. A size matching neither is refused with both expected sizes
named, since that is nearly always a truncated download.

Verified: both 640KB images boot or reach a prompt, a `SAVE`/`LOAD` round
trip across separate process runs works on `MF0:` (which exercises the
four-sectors-per-cluster address arithmetic, different from the ABC830's),
the 160KB path is unchanged, and a truncated image is rejected.

### The two drives interleave differently, and neither predicts the other

The ABC830 needs sector interleave factor 7. **The ABC832/834 needs
none** — and this was established the same way, by booting real media both
ways rather than by carrying the other drive's value across:

| Drive | Identity | Interleave 7 |
|---|---|---|
| ABC830 (160K) | does not boot | **boots** |
| ABC832/834 (640K) | **boots** | does not boot |

Exactly opposite results. Had either value been assumed from the other,
that drive would have been silently broken — and the failure looks like
"the disk does nothing", with no error to trace.

There is a trap worth naming here for whoever adds the `SF` or `HD` types
next. Both images have their directory at sector 16, and sector 16 reads
correctly under *either* mapping, because track-boundary sectors map to
themselves. So "I can see the filenames in a hex dump" proves nothing
about interleave. Only booting does. This is the same trap ABC80's
Milestone 6 documented, met again on different media.

### A cross-milestone check that came for free

The `ADMINISTRATION 800` screen is the first time **real** software has
exercised Milestone 3's row-attribute decode. Its title bar is
inverse-video and its rules are Row Graphic mosaic characters — the
attribute path that, per
[the boot-screen postmortem](../../docs/postmortems/2026-08-28-boot-screen-cannot-validate.md),
the ROM's own boot screen could never have tested and which until now had
only ever been driven by `bin/abc802-chargen-dump`'s synthetic screen. It
renders correctly, on real 1983 output that knew nothing about this
emulator.

## Milestone 7: a second drive — done

`--disk` is repeatable. Images take drives 0, 1, … in the order given, or
a drive can be pinned explicitly with a `N:` prefix:

```
$ bin/abc802 --disk system.img --disk games.img      # drives 0 and 1
$ bin/abc802 --disk 1:games.img                      # drive 1 only
ABC-bus: mf floppy controller, 2 drives attached
```

The controller already tracked eight units and the ROM already addresses
them as `MO1:`/`MF1:`; the only thing missing was a way to attach one. No
second flag was invented for it — repeating the existing one is what
two-drive software expects and keeps every single-`--disk` invocation
working unchanged.

### Verified, with a negative control

Saving to the second drive touches only the second drive:

```
$ md5 d0.img d1.img          -> df2b6449…  df2b6449…   (identical copies)
$ bin/abc802 --disk d0.img --disk d1.img --type 'NEW / 10 PRINT "DRIVE ONE" / SAVE "MF1:D1TEST"'
$ md5 d0.img d1.img          -> df2b6449…  4933f5aa…   (only drive 1 changed)
```

and reading it back in a separate process finds it on drive 1 and **not**
on drive 0:

```
LOAD "MF1:D1TEST"
LIST
10 PRINT "DRIVE ONE"
RUN
DRIVE ONE
LOAD "MF0:D1TEST"
Error 21.
```

That last line is the point of the test. A round trip that only ever reads
back what it wrote cannot distinguish "the drives are independent" from
"the unit number is ignored and everything lands on drive 0" — both give a
successful load. The failing read on `MF0:` is what rules the second one
out.

Also checked: pinning with `1:` attaches drive 1 with no drive 0; two
images of the same type work; and mixing types is refused, since one
controller is fitted and its drives are all of its own kind —

```
Disk image 'mf001.img' is a mf image, but a mo controller is already fitted
```

## Milestone 8: the line editor's actual vocabulary — done

The arrow-key gap Milestone 2 left open is closed, though not the way it
was expected to be. The **Left** arrow now works in both `--interactive`
and `bin/abc802-gtk`. **Right does nothing, correctly** — because there is
nothing on this machine for it to do.

### What the editor actually accepts

Established by sweeping the ROM's line editor with every byte `0x00`-`0x1F`
and a sample across `0x80`-`0xFF`, typing `ABCDE<code>X` in each case and
reading back what the editor did to the line. The whole vocabulary:

| Code | Key | Effect on the line |
|---|---|---|
| `0x03` | Ctrl-C | terminates it (break) |
| `0x08` | Backspace | destructive delete-left |
| `0x0A` | Ctrl-J | terminates it |
| `0x0C` | Ctrl-L | clears the screen |
| `0x0D` | Return | terminates it |
| `0x18` | Ctrl-X | discards the whole line |
| everything else | — | ignored, or appended if printable |

**There is no cursor movement of any kind.** Not a non-destructive left,
not a right, and nothing in the high byte range either — `0x80`-`0xFF`
behave exactly like their low equivalents. This is a genuinely simpler
editor than the ABC80's, which does have a non-destructive cursor-right
(`0x09`).

So Left maps to `0x08` — the only leftward motion that exists, and the
same key that literally *is* backspace on the ABC80's own keyboard — and
Right is dropped, harmlessly, since the editor ignores control bytes it
does not recognize.

### Why this was a finding rather than a fix

Milestone 2 recorded this as a known gap on the assumption that the
mapping existed and had not been found yet: "closing this properly means
disassembling the ROM's own line editor." That framing was wrong. The
honest closure was to establish that **the machine has no such function**,
which turns an open item into a documented hardware fact.

The method mattered. A disassembly would have had to find the editor
first — it sits in a region the reachability-based `bin/z80dasm` cannot
reach, since it is only entered indirectly. A 47-run behavioral sweep
answered the same question completely, and answered it about *behavior*
rather than about code that might have had paths the reader missed.

Verified end to end:

```
$ printf 'PRINT 12\x1b[D\x1b[D3\r' | bin/abc802 --interactive --columns 80
PRINT 3
 3
$ printf 'PRINT 4\x1b[C\x1b[C2\r' | bin/abc802 --interactive --columns 80
PRINT 42
 42
$ printf 'GARBAGE\x18PRINT 7*6\r' | bin/abc802 --interactive --columns 80
PRINT 7*6
 42
```

Left deletes; Right is swallowed rather than typed as garbage; Ctrl-X
discards the line, which had always worked and had never been documented.

## Milestone 9: a real Z80 SIO — done

The SIO was the last stub in the machine: every read returned one of two
constants regardless of what the ROM had programmed. It is now a real
register model — WR0-WR7, RR0-RR2, the register pointer, and the reset
command — and, more to the point, the two configuration DIP switches that
reach the ROM *through it* are finally delivered.

### Why a constant was not good enough

S1 and S2 are not memory-mapped. They arrive as **channel B modem-status
inputs**: S1 ("Clear Screen Time Out") on DCD, S2 (undocumented in MAME
too) on CTS. A stub reporting `0x04` told the ROM both switches were off,
which is a claim about the machine's configuration, not a neutral
placeholder. An idle channel B now reads `0x24` — transmit empty plus CTS
from S2 — matching MAME's own defaults of S1 off, S2 on.

Channel A is the second RS-232 port and channel B is the **cassette**
(MAME wires the cassette input to `rxb`, its output from `txdb`/`rtsb`,
and the motor from `dtrb`). Neither has a device attached here, so nothing
is received and transmitted bytes are discarded — but transmit is still
reported empty, because with nothing attached a byte written to the data
port has by definition gone as far as it is ever going to, and a ROM
polling loop waiting on that bit must see it set or it never exits.

### Verified from inside the machine

ABC802 BASIC has `INP()` and `OUT`, so the chip could be tested by the
emulated machine itself rather than by an assertion in C — the strongest
form of check available here, since it exercises the whole path the ROM
uses.

```
PRINT INP(67)          ->  36    RR0 = Tx empty (0x04) + CTS from S2 (0x20)
OUT 67,1
PRINT INP(67)          ->   1    the pointer works: RR1 "all sent", not RR0
OUT 67,2 : OUT 67,88
OUT 67,2
PRINT INP(67)          ->  88    the interrupt vector round-trips
OUT 65,2
PRINT INP(65)          ->   0    RR2 is channel B only; A gives no answer
OUT 67,1 : OUT 67,24
PRINT INP(67)          ->  36    channel reset returned the pointer to RR0
```

Before this milestone every one of those reads returned `4`.

That `0` on channel A is deliberate. The datasheet makes RR2 valid on
channel B only, and returning the vector on A as well would have been
harmless-looking and quietly wrong — a caller reading the wrong channel
should get an obviously wrong answer rather than a subtly right one.

### What is deliberately still missing

No device is attached to either channel, so **the SIO never raises an
interrupt** and its slot in the CTC → SIO → DART daisy chain stays inert.
Cassette is the interesting absence, and it is a milestone in its own
right rather than an oversight: the real interface is bit-level, with the
signal modulated through the SIO's synchronous clocks and demodulated by
frequency detection. The machine has working disk storage, so a cassette
would be fidelity rather than capability.

