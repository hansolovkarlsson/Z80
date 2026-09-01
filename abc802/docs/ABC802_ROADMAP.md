# ABC802 emulator roadmap

Status and known gaps for `bin/abc802`, the Luxor ABC802 machine target.
Hardware facts live in `ABC802_REFERENCE.md` and the BASIC II language
itself in `ABC802_BASIC_REFERENCE.md`; this file is about what works, what
does not, and what was actually verified.

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

## Completed work

Milestones 1-12 are done. Their full write-ups — including what each one
found the hard way — live in [`ABC802_COMPLETED.md`](ABC802_COMPLETED.md).

| # | Milestone | Outcome |
|---|---|---|
| 1 | Boot the real ROM to a working BASIC | `bin/abc802 --type "PRINT 6*7"` reaches the real ROM's prompt and answers `42` |
| 2 | Live interactive keyboard and screen | `--interactive`: raw-terminal input, real 3 MHz pacing, a live screen, a ROM-driven cursor blink |
| 3 | Pixel rendering from the character ROM | `--screenshot` writes a real 480x240 PNG in the machine's own amber; all three row attributes decoded |
| 4 | A GTK window | `bin/abc802-gtk`, a Cairo framebuffer sharing the same decode and the same `abc802_step()` |
| 5 | ABC-bus floppy support | `--disk` boots real 160KB ABC830 images and round-trips `SAVE`/`LOAD`, via a synthetic bus controller |
| 6 | The ABC832/834 640K drive | the ABC802's own native drive; controller type auto-detected from image size |
| 7 | A second drive | `--disk` repeats for drives 0, 1, …; `MO1:`/`MF1:` work and are independent |
| 8 | The line editor's vocabulary | swept every control code; Left arrow works, Right correctly does nothing — the machine has no cursor movement |
| 9 | A real Z80 SIO | registers, commands and the two DIP switches that reach the ROM through channel B's modem-status inputs |
| 10 | An automated regression suite | `abc802/tests/run_tests.sh`, 27 checks — 20 need no disk images (2 of those need the opt-in GTK build), 7 need media — part of `make test` |
| 11 | Row attributes in the terminal render | `--screen` and `--interactive` run the pixel renderer's own attribute walk and draw Row Graphic as Unicode sextants |
| 12 | Cassette | `--cassette` gives a real `SAVE`/`LOAD` round trip on SIO channel B, with a genuine receive interrupt and bisync hunt |

## Known gaps

Real, understood, and deliberately not solved yet — not oversights.

- **The GTK window has no disk dialog and no color picker**, both of
  which `bin/abc80-gtk` has an equivalent of. `--disk` attaches images at
  launch and BASIC's `SAVE`/`LOAD` work against them, but there is no
  in-window way to swap a disk; the amber phosphor is fixed at the
  machine's real value. See `../gtk/README.md`.
- **The GTK app is still not *built* by `make test`.** It needs `gtk4`,
  which the default build deliberately does not depend on. It now has two
  headless checks (`gtk-headless-boot`, `gtk-headless-type`) that run when
  the binary exists and skip loudly when it does not — so a broken render
  or keyboard path is caught, but a build break is not. That gap is real:
  `bin/abc80-gtk` stopped compiling for part of a day in August 2026
  because nothing built it.
- **The line editor has no cursor movement, and that is the hardware.**
  Left arrow maps to backspace and Right does nothing, because a full
  sweep of every control code (Milestone 8) established the editor's whole
  vocabulary as backspace, discard-line, clear-screen and three line
  terminators. Listed here so nobody re-opens it as a missing feature: it
  is a documented property of this ROM, not a gap in the emulator.
- **The CRTC is a register file, not a timing model.** Rendering reads
  character RAM on demand rather than reproducing the real scanline fetch,
  and no vertical-sync interrupt is generated. The cursor *does* animate
  now — the ROM blinks it in software through R10, so it needs no
  scanline model (Milestone 2) — but anything genuinely tied to field
  timing, including the hardware cursor-blink modes (R10 bits 6:5 = 10/11)
  and the character generator's Row Flash attribute, still is not.
- **Channel A's RS-232 port has nothing attached.** Transmitted bytes are
  discarded and nothing is ever received.
- **Two drive types, one card.** `MO` (ABC830, 160KB) and `MF`
  (ABC832/834, 640KB) both work, on as many as eight drives; the ROM also
  scans for `SF` (8-inch) and `HD` (hard disk), which `abcbus/disk.c`'s
  geometry table is shaped to take but which have no verified geometry and
  no test media here. All drives must be of one type, since one controller
  is fitted — a real machine could have both an ABC830 and an ABC832 on
  the bus. No printer or RTC card either.
  **Note for whoever adds `SF`/`HD`:** interleave cannot be inferred. The
  two working drives need *opposite* settings, and a directory sector is
  readable under either mapping, so only booting real media settles it.
  **Real software hits this**, which is why it is worth the warning.
- **`DOSGEN`'s bad-sector output past the end of the media is the DOS, not
  a defect** — this was carried here as an open question and is now
  settled; see [`ABC802_COMPLETED.md`](ABC802_COMPLETED.md). It runs to
  completion, reports the correct `2528 användbara sektorer`, and the
  filesystem it writes is usable. The `Sektor NNNN är dålig` lines are
  DOSGEN filling a fixed 240-byte free-list bitmap that covers 1920
  clusters on a drive that has 640. Three checks cover it. Low-level
  formatting (its `F` option) remains out of scope for a synthetic
  controller; the `-` option, which writes only the filesystem, is the one
  that matters and is the one tested.
- **The controller card itself is not emulated.** A real ABC830 is a
  complete second computer (its own Z80, Z80 DMA, FD1793 and firmware);
  what exists here models the *protocol* it speaks, not the card. Software
  driving the controller below the DOS layer, or copy-protected media,
  would need the real thing — see `ABC802_FLOPPY_SCOPING.md`'s option C,
  which the current design deliberately does not foreclose.
- **The "pling" speaker strobe is decoded but silent.** No audio.
- **The DART/SIO/CTC are modeled only as far as the boot path needs.**
  Baud-rate generation, transmit interrupts, and the SIO's own vectors
  are absent.
- **Frame frequency is ambiguous** — MAME's DIP label and the code
  comment that consumes it disagree; see `ABC802_REFERENCE.md`. Nothing
  currently depends on it.

## Testing

`make test-abc802` (also part of `make test`) runs
[`tests/run_tests.sh`](../tests/run_tests.sh): boot at both column widths,
the Swedish charset round trip, five SIO register checks driven from
BASIC through `INP()`/`OUT`, a `--screenshot` PNG validated down to its
IHDR dimensions, a chargen fixture diff covering the three row attributes
no boot screen exercises, the same mosaic row asserted in the *terminal*
render, and the character-ROM invariant that lets the terminal walk read
one scanline where the pixel walk reads them all. Two more drive
`bin/abc802-gtk` headlessly through its own `--screenshot`, and skip
loudly when that opt-in binary is absent. Four floppy checks — 160K and 640K
media booting real applications, drive independence, and a cross-drive
load with its negative control — need `ABC802_TEST_DISKS` pointed at a
directory holding `disk001.img`, `mf001.img` and `mf002.img`, and skip
loudly without it.

Three more run `DOSGEN`, the DOS's own disk generator, end to end: it
completes with the correct usable-sector count, the free-list bitmap it
writes marks every cluster past the end of the media, and a program saved
to the filesystem it built is read back **in a second process**. They need
`sys832-ufd.img` in the same directory, which the four above do not, so
they gate separately. `ABC802_TEST_DISKS` is an override: unset, the suite
looks in [`../resources/disks/`](../resources/disks/). The two-process form is not fussiness — done in one
process the check matches the program text still echoed on screen from
when it was typed, and passes with the card's writes injected away. That
happened, and is why the split exists.

Those three are `.img` dumps in physical sector order, so they run at the
default interleave. Images from other archives may be dumped in logical
order and need `--interleave 0` — see
[`../resources/disks/README.md`](../resources/disks/README.md).

Five further checks need **no external media at all**, because
`bin/abcdisk` builds it: two format a blank disk of each type — asserting
the drive's full usable capacity, 616 and 632 clusters, which is what
would have caught the ABC832 formatter shipping at half size — and two
`SAVE` a program to one and `LOAD`, `LIST` and `RUN` it back in a separate
process. The fifth has `abcdisk` read a real image it did not write, which
is what independently pins the directory's location — a writer and reader
sharing one wrong constant agree perfectly, as a deliberate sabotage
confirmed. That check is media-gated; the other four are not, so the disk
*write* path is now covered on a bare checkout.

Still uncovered: the `--interleave` override itself. Verifying it needs a
logical-order image the suite can rely on, which is a media problem rather
than a test-writing one.

## Planned next steps

None committed. Milestones 2-12 closed the items that previously stood
here, and the ABC80 target's PC-trap bypass has since been retired in
favour of this controller — the card now lives at `abcbus/disk.c`, shared
by both machines, and that work is written up as ABC80 Milestone 12 in
[`../../abc80/docs/ABC80_COMPLETED.md`](../../abc80/docs/ABC80_COMPLETED.md).
It also corrected a real bug here: status bit 3 was modeled as an error
flag on no evidence, and is in fact its complement ("this command has not
failed"). This machine's ROM never reads the bit, so nothing on this
target could have caught it. The remaining candidates, roughly in order
of how much they would add:

1. **Two controllers at once** (an ABC830 *and* an ABC832 on the bus),
   which the current one-controller design cannot express. Only worth
   doing if some real software turns out to want both.
2. **The `SF`/`HD` drive types**, which the ROM scans for and the geometry
   table is shaped to take. Blocked on verified geometry and test media —
   and note that interleave cannot be inferred from the working drives,
   which need opposite settings.
3. **A GTK disk dialog and colour picker**, which `bin/abc80-gtk` has an
   equivalent of and this window does not. Purely front-end work.
4. **Real tape audio.** The cassette (Milestone 12) models the SIO's byte
   stream, which is the protocol boundary and enough for this machine's
   own `SAVE`/`LOAD`. Loading a `.wav` recorded from real hardware would
   need the analogue layer underneath it — FSK modulation and frequency
   detection — which nothing here needs today.

## Sources consulted

- MAME mainline driver and video code for the ABC800 family:
  `src/mame/luxor/abc80x.cpp` and `src/mame/luxor/abc80x_v.cpp` — the
  memory/I-O maps and ROM checksums (Milestone 1), and
  `abc802_update_row()` plus its transcribed PAL16R4 equations, the ATE/
  ATD/INV bit assignments in `abc80x.h`, and the FLSH-clock divider in
  `vs_w()` (Milestone 3). Fetched from
  <https://raw.githubusercontent.com/mamedev/mame/master/src/mame/luxor/abc80x_v.cpp>.
  Note the driver is `abc80x`, covering the whole ABC800/802/806 family —
  there is no `abc802.cpp`.
- MAME's `rgb_t::amber()` — `src/lib/util/palette.h`, which defines the
  amber phosphor this machine's screen is configured with as
  `(247, 170, 0)`. Used verbatim by `--screenshot` rather than guessed at.
- Real ABC800-family floppy images: <https://www.abc80.net/archive/luxor/sw/disk_images/ABC800/>
  (`160k/` holds the ABC830-format images Milestone 5 was verified
  against — `disk001.img` boots ORD 800, `disk003.img` boots PROMMIS).
  **Not committed to this repo**, following the same decision ABC80's
  Milestone 6 made about its own `disk003.img`: they are third-party
  software dumps, and the emulator takes a path to one rather than
  shipping it.
- `sasq64/abc80sim` (`src/disk.c`, `src/abcio.c`) — the synthetic ABC-bus
  controller whose command protocol Milestone 5 reimplements, after
  confirming three of its details against this machine's own DOS ROM.
  Note its interleave is compiled *out*, which this project's own
  experiments contradict; see that milestone's write-up.
- MAME `src/devices/bus/abcbus/lux21046.cpp` — the real controller card's
  machine configuration (its own Z80, Z80 DMA and FD1793), which is what
  established that emulating the card itself is a much larger job than
  modeling the protocol it speaks.
- The committed ROM images themselves (`../resources/rom/`), used as the
  independent cross-check on every fact taken from MAME above: the
  attribute-code inventory, the two font halves and exactly which codes
  differ between them, and the blank/cursor scanlines at `0x0E`/`0x0F`
  were all re-derived from the ROM rather than trusted. See that
  directory's own `README.md` for provenance and checksums.
