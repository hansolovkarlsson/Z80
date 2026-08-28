# Project journal

A running log of what was worked on, what it took, and what it taught.
Newest first.

This is not a changelog — `git log` already does that, and better. It is
the place for the things a commit message cannot hold: why an approach was
chosen over the alternatives, what turned out to be wrong on the first
attempt, and which assumptions the hardware refused to honor. The
per-target `*_COMPLETED.md` files hold the same detail organized *by
milestone*; this file holds it organized *by when*, which is the view that
answers "what was I in the middle of, and why did I do it that way."

Cross-cutting failures that are worth understanding on their own get a
full write-up in [`postmortems/`](postmortems/) rather than being buried
in an entry here.

---

## 2026-08-28 (later) — ABC802 Milestone 5: the ABC-bus floppy, scoped then built

Scoped the work first, on the suspicion it would be intricate. It was —
but not where expected, and the scoping is what made the build quick. The
prediction-versus-outcome comparison lives at the top of
`abc802/docs/ABC802_FLOPPY_SCOPING.md`; the short version is that the
document got the *shape* entirely right and the *estimate* wrong in the
cautious direction.

### What the scoping found

Three things, none visible without looking:

- **The controller is a complete second computer** — its own Z80 at
  4&nbsp;MHz, a Z80 DMA controller, an FD1793 FDC, five firmware variants,
  and a PAL that was never dumped.
- **Porting MAME is not a shortcut.** MAME's card-side ABC-bus handlers
  are a byte latch, a busy flag and an NMI pulse. It does not *know* the
  disk protocol; it runs the firmware that implements it. So the choice is
  binary: run real firmware, or implement the protocol yourself.
- **The ABC80 target's bypass does not transfer.** That one traps two PC
  addresses because that ROM has a single sector-level routine. This DOS
  ROM's bus driver is generic and its callers issue *sequences* of bus
  commands, so there is no equivalent place to cut.

But abc80sim documents the protocol, and three details of it matched this
ROM exactly — the two command constants, the select mask, the select
values. That is what turned "an implementation exists for a related
machine" into "this is the same protocol", and it is why the build needed
no reverse-engineering at all.

### What the build found

- **`0x00` and `0xFF` both mean "no device."** The scoping flagged the
  status byte as the likeliest source of trouble and guessed the problem
  would be bit polarity. The actual constraint was stricter and was missed
  entirely: the ROM's poll loop does `INC A / JR Z` *and* `DEC A / JR Z`,
  so either value aborts it. Which is exactly why the old "every ABC-bus
  read returns `0xFF`" behavior read as *no card fitted* — correct, and by
  accident. An idle controller is `0x81`.
- **The interleave contradiction, settled by experiment.** The scoping
  recorded that this project verified interleave factor 7 empirically
  while abc80sim ships with interleave compiled out, and that both could
  not be right. Rebuilding with it disabled and changing nothing else,
  real media stops booting entirely. Resolved in favour of this
  repository's own earlier finding, now confirmed on a second machine and
  a different DOS ROM.
- **`LIB` does not exist, and the error was right.** Three guesses at
  directory-listing syntax all returned `Error 220`, which looked like a
  controller bug. It was not: the DOS ROM's own command table holds
  exactly four entries — `BYE`, `KILL`, `NAME`, `AS`. Reading the ROM
  settled in minutes what guessing had not in three attempts. The general
  lesson is one this project keeps relearning: when the machine disagrees,
  read its ROM rather than try another guess.
- **A gap no scoping would have predicted.** `--type` was unusable for
  disk work, because the ROM reports the keyboard ready long before a
  booting program is listening; `LIB` arrived as `B`. `--type-at` exists
  now because of it, and without it no scripted disk test is reproducible.

### On verifying it

The temptation with disk support is to see software appear on screen and
call it done. `ORD 800 Version 2.4` booting off a real image is a
wonderful thing to see, and proves nothing on its own — so the check was
that the string `ORD 800` appears at sector 57 of the image and in **none
of the six committed ROMs**, and that with no `--disk` the machine still
boots to a bare BASIC prompt.

That habit came directly from [the boot-screen
postmortem](postmortems/2026-08-28-boot-screen-cannot-validate.md) written
earlier the same day, which is the first time one of these write-ups has
visibly changed how the next piece of work was checked.

The first round-trip test was also thrown away and redone: it typed a
program into a session where the disk's own autoboot program was still
resident, so `SAVE` stored a merge of both and `LIST` came back showing
someone else's Luxor game menu with one line of mine in it. The round trip
was genuinely correct; the output simply did not demonstrate it. Redone
with `NEW` first, it does.

---

## 2026-08-28 — ABC802 Milestones 2-4: interactive, pixels, GTK

Picked the project back up after a gap. Starting state: `make test` green
(16/16), every ABC80 and CP/M roadmap item closed, and ABC802 one
milestone deep — it booted the real BASIC II ROM to a prompt and could be
typed at only through a fixed `--type` string. Three candidate next steps
were listed and none committed.

Took them in the order the roadmap suggested, which turned out to be the
right order for a reason worth recording: each one made the next one
verifiable.

### Milestone 2 — live interactive keyboard and screen

`bin/abc802 --interactive`: raw-terminal input, execution paced to the
real 3 MHz clock, a screen redrawn at 30fps, and UTF-8 input for the
Swedish letters the display could already show.

Four things were established by **probing the real ROM rather than porting
the ABC80 target's assumptions**, and all four would have been wrong if
assumed:

- **The cursor blink belongs to the ROM, not the emulator.** Tracing the
  ROM's CRTC writes showed it toggling MC6845 R10 between `0x09`
  (non-blink) and `0x29` (non-display) continuously, driven by its own
  93.75 Hz clock interrupt — it blinks in *software*. So this target needs
  no blink-rate constant at all, unlike `bin/abc80`, which must supply
  `ABC80_BLINK_HZ` because that machine blinks in hardware. Honoring R10
  and pacing execution correctly is the entire implementation; the
  measured ~2.7 Hz result is the firmware's decision, not this code's.
  This is the happiest kind of finding: the feature was already there, and
  the work was to stop getting in its way.
- **The host Backspace key sends DEL, and this ROM does not want DEL.**
  `0x08` is a real destructive delete (`PRINT 12` + two `0x08` + `3`
  evaluates to `3`); `0x7F` is treated as an ordinary printable character
  and echoes a blank into the line. Untranslated, Backspace would have
  silently corrupted what the user typed instead of erasing it.
- **This machine's line editor is not the ABC80's.** That target maps host
  arrow keys onto `0x08`/`0x09`, grounded in a disassembly of *its* ROM.
  Probing this one found no non-destructive cursor-right at all: `0x09`
  and `0x1F` are ignored, `0x0C` clears the screen. Rather than invent a
  mapping, arrow keys are dropped and the gap is recorded. Left as a known
  gap on purpose — the honest fix is to disassemble this ROM's editor,
  which is a piece of work in its own right.
- **Live input needs the same pacing `--type` already used.** See
  [the DART postmortem](postmortems/2026-08-28-dart-single-byte-overwrite.md).

### Milestone 3 — pixel rendering from the character ROM

`--screenshot FILE` writes a real 480x240 PNG in the machine's own amber
phosphor. Grounded in MAME's `abc802_update_row()` — which carries the
video board's actual PAL16R4 equations — then cross-checked byte-for-byte
against the committed ROM, which is what turned a port into knowledge.

The scheme is genuinely strange and worth knowing before touching anything
video-related: **the character generator ROM's own output byte decides
whether a cell is a character or an attribute command.** Bit 7 set means
it is not pixel data but an instruction. So the *font* defines which
character codes act as attribute codes — here exactly 17 of them. And all
three attributes work by substituting the scanline address rather than
post-processing pixels, which is why the real cursor is a solid bar
*replacing* the glyph rather than an inversion of it.

Two decisions worth keeping:

- `abc802_render_pixels()` is a **pure function** over an `Abc802Screen`
  struct. That was not architectural taste for its own sake — it is what
  let the decode be verified with no CPU core at all, and it is why the
  GTK app a milestone later could not drift from `--screenshot`.
- `png.c` is hand-written, using DEFLATE *stored* blocks so no compressor
  is needed. Same reasoning as `abc80`'s own WAV writer: the default build
  of this project has no third-party libraries, and the uncompressed path
  of both formats is small and completely specified.

The important lesson here was about testing, not rendering, and it has its
own write-up: [the boot screen cannot validate the
feature](postmortems/2026-08-28-boot-screen-cannot-validate.md).

### Milestone 4 — a GTK window

`bin/abc802-gtk`, a Cairo pixel framebuffer running the core in-process.
Much shorter than ABC80's equivalent, because Milestone 3's decode was
already shared and verified — the app only turns its output into a Cairo
surface. `abc802_step()` was extracted into `emu/src/step.h` at the same
time so the CLI's `--interactive` loop and the window share the
per-instruction logic, the same move ABC80's Milestone 11 made.

The interesting problem was **verification, not implementation**.
`abc80/gtk/README.md` records that automating `screencapture` against the
user's real desktop is disruptive — it steals focus and switches Spaces
while they are working — so that avenue was closed on purpose. The answer
was to build the app so it can verify itself: `--screenshot` opens no
window and never creates a `GtkApplication`, but renders one frame through
the *identical* `draw_screen()` the live window uses, against an offscreen
Cairo surface. Real evidence about the real renderer, with no desktop
involved.

That flag justified itself within minutes of existing by catching a real
bug — and a pre-existing one at that: [`--type` fed raw UTF-8
bytes](postmortems/2026-08-28-type-raw-utf8-bytes.md).

### What carried across all three

The pattern that kept repeating: **the ABC80 target is a good source of
architecture and a bad source of facts.** Its shapes transferred perfectly
— the raw-terminal setup, the pacing loop, the shared-step extraction, the
GTK app's structure. Every one of its *hardware* assumptions that got
tested against the ABC802 ROM turned out to be wrong: the blink, the
arrow keys, where the cursor comes from. Reuse the structure, re-derive
the facts.

Also: three of the four bugs found this session were found by *looking at
output*, not by a test failing. The screenshots and ASCII-art dumps earned
their keep.

### Housekeeping

- Deleted two untracked scratch files left over from an earlier session
  (`abc80/examples/hello.bac`, `test.bas`).
- Split completed work out of all three roadmaps into per-target
  `*_COMPLETED.md` files; the roadmaps went from 4,605 lines to 439 and
  now answer "what works, what doesn't, what's next" without scrolling
  through an archive. Two stale headings surfaced in the process and were
  corrected: **ABC80's Milestone 6 never said "done"** and **CP/M's Phase
  2 still said "in progress"**, though every item under both is checked
  off and has been for some time.
- Established this journal and `docs/postmortems/`.
