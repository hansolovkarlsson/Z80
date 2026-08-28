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

## 2026-08-29 (later still) — regression suites for the machine targets

Three machine targets, one covered by `make test`. Everything the ABC
targets had ever been verified by lived in a hand-run matrix retyped each
session — which is how the previous entry's change got signed off, and it
would not have survived to the next one.

`abc80/tests/run_tests.sh` (17 checks) and `abc802/tests/run_tests.sh`
(14) are that matrix, kept, with shared reporting in
`scripts/testlib.sh`. `make test` now runs all three suites: 47 checks,
under two minutes including a clean build.

### The stance

No unit tests of internal functions. Almost every bug these targets have
had was a wrong belief about the hardware rather than a wrong line of C,
and a unit test written from the same wrong belief passes — the
`*_COMPLETED.md` files are substantially a catalogue of such beliefs.
Everything drives the real ROMs and asserts on what the machine produced.

Where a check can be written as BASIC, it is. `PRINT INP(6)` exercises the
real port decode, the ROM's own `INP`, and the CPU's I/O path; calling the
same C function exercises none of them. That came from the ABC802 SIO work
and transfers to the ABC80 unchanged.

### Validated by breaking things on purpose

A suite nobody has seen fail is not known to work, so five real
regressions went in one at a time and each had to be caught: the status
bit from the previous entry reverted, the bus decode widened to swallow
the sound port, an ABC802 DIP-switch default flipped, the ABC830
interleave disabled, the mosaic-font address bit changed.

Four were caught. **The port-decode check passed with its subject entirely
broken** — it was asserting on its own input. Full write-up in
[a postmortem](postmortems/2026-08-29-test-matched-the-echoed-input.md),
because the lesson generalizes to every check on these targets: the
machine is driven by typing at it and read by looking at video RAM, and
video RAM contains the echoed input, so input and output share one
buffer.

Rerunning the sweep found two more checks that were weak rather than
wrong — `disk-boot` asserted only the absence of an error, and the
UFD-DOS check asserted a "file not found" that a completely dead card
also produces. Both now assert on the card's own `ABCBUS_TRACE=1` output,
so they fail when the bus stops carrying anything.

Of seventeen checks written that afternoon, three were not testing what
their names claimed, and the suite reported green throughout. A check is
finished when it has been seen to fail for the right reason.

### An aside on measuring

The hand-run matrix used instruction caps up to 120,000,000 where
3,000,000 does. They had been copied forward from an earlier session and
never questioned. At roughly 1.7M instructions/sec that was most of a
minute per check spent on nothing, and measuring the minimum took two
commands. Worth remembering next time a number gets carried across
sessions because it worked once.

### A false alarm worth recording

Twenty minutes went into suspecting the SN76477 was broken, because
`OUT 6,255` from BASIC rendered silence and so did `OUT 6,0`. Both are
correct. `0xFF` disables the chip and inhibits the mixer; `0x00` selects
an envelope mode that produces nothing without a trigger. `0x40` — the
value the demo tool itself uses — gives a clean tone. The bit layout was
in `sound.c`'s own header comment the whole time, and the near-miss was
writing the phantom up as a known gap in the roadmap.

It turned into the seventeenth check. `sound-register-from-basic` drives
the register through the CPU rather than calling the model directly, which
covers `step.c`'s decoding of BASIC's compiled `ED`-prefixed `OUT` — a
path nothing else touched. Breaking that decode now fails it.

### Also found

The ABC80 emulator runs at about 1.7M instructions/sec, several times
slower than the ABC802 on the same shared core. Nothing depends on it, but
it is most of why one suite takes 20 seconds and the other 3; recorded as
a Performance note in the ABC80 roadmap rather than chased. And
`bin/abc80-video-timing-dump` already spoke PASS/FAIL and set an exit code
of its own — it had simply never been run automatically. It is now.

---

## 2026-08-29 (later) — ABC80 Milestone 12: retiring the PC-address trap

The ABC802 roadmap's top candidate, taken up directly: replace the ABC80
target's floppy trap with the real ABC-bus card the ABC802 already had.

### The move

`abc802/emu/src/disk.c` went to `abcbus/disk.c` at the repo root, beside
`z80core/`. The reasoning is the same one that put `z80core/` there — the
ABC bus is a bus, not a machine — and it had to happen *before* the ABC80
could use the card, or `abc802/` would have quietly become a shared
library for `abc80/`, which is the exact arrangement moving `z80core/` out
of `cpm/` was meant to end. `abc80/emu/src/disk.c` became `abcbus.c` and
kept only what is genuinely machine-specific: loading the DOS ROM into the
`0x6000` expansion window, and this machine's own `0x17`-masked port
decode.

Before writing any of it, the first question was whether the ABC80's DOS
ROM even speaks the same protocol, since assuming so would have been the
whole risk. Scanning `ABCDOS80.bin` for `IN`/`OUT` opcodes answered it in
one command: ports 0, 1, 2 and 4, and nothing else. Same registers, same
C1/C3 pulses. Disassembling `0x612B` then showed the same four-byte
`B`/`C`/`D`/`E` header and the same `0x03`/`0x0C` command constants the
ABC802's ROM uses. Two unrelated ROMs, five years apart, agreeing on a
bitmask — which is what makes it a bitmask and not a coincidence.

### The bit that was invented

Then it did not work, twice, in two ways that looked unrelated. `ERR 21`
on every read; after fixing that, `ERR 48` at boot on every write. Both
were one bit: the card modeled status bit 3 as an error flag, and it is
the exact complement — "this command has not failed". Full write-up in
[a postmortem](postmortems/2026-08-28-status-bit-invented-from-one-rom.md),
because the interesting part is not the bit but *why nothing could have
caught it*: the ABC802's ROM never reads bit 3 at all, so no test on that
target — however thorough — could have distinguished right from wrong. A
second consumer was the only thing that could.

The tell was there to be seen, though. The file's header comment derives
four status bits from specific ROM addresses; bit 3 sat in the `#define`
block with no citation and no mention in that comment. In a file otherwise
scrupulous about grounding every claim, **the one value nobody could point
at an address for was the one value that was wrong.**

What made it quick was adding `ABCBUS_TRACE=1` to the card, and then
reading the trace for what it *lacked*: correct command headers for
exactly the right sectors, and no transfers at all. The absence of an
expected line was the entire diagnosis. Kept the facility.

### Evidence it was worth doing

Three results, in ascending order of how much they justify the change:

1. **Byte-identical output.** The same `SAVE`/`LOAD` keystrokes against
   the same image produce a disk file identical to the trap's, down to the
   five physical sectors touched. The real protocol and the carefully
   derived shortcut agree exactly — which is the reassuring result, not
   the interesting one.
2. **A case the trap could not do.** `RUN LIB` + Enter, the real directory
   listing utility, reported `Diskfel` under the trap. It now prints the
   volume label, all fourteen files and the free-sector count. Nobody was
   working on that; it came free with modeling the device instead of the
   routine. Worth noting that this was a *pre-existing, unnoticed*
   failure — the trap had been signed off as working.
3. **A different DOS ROM.** `UFD80V20.bin` was examined during Milestone 6
   and left unwired, for the good reason that a trap derived against
   ABC-DOS's routines cannot serve a different DOS. It drives the same
   card correctly — 40 real bus commands, reading bitmap, directory and
   file sectors — and then correctly reports `HITTAR EJ FILEN`, because an
   ABC-DOS-formatted disk has no UFD-DOS startup file on it. That is the
   whole argument for a device model in one line, so `--dos-rom` was added
   to make it reproducible instead of a throwaway experiment.

### Carried across

The same lesson as the ABC802 sessions, pointed the other way this time:
the ABC802 target was a good source of *structure* — the whole controller
transferred unchanged — and one of its hardware facts was wrong. Reuse the
shape, re-derive the facts. The difference is that here the wrong fact was
one this project had written itself, and the second machine is what
audited it.

---

## 2026-08-29 — ABC802 Milestone 9: a real SIO, and testing from inside

Documented the line editor's keys in `ABC802_REFERENCE.md` (and refreshed
its ABC-bus section, which still claimed no card was modeled), then took
the SIO — the last stub left in the machine.

The thing that made this worth doing properly, rather than leaving a
constant that "works", is that **two of the machine's configuration DIP
switches arrive through the SIO**. S1 and S2 are not memory-mapped; they
are channel B's DCD and CTS inputs. A stub returning `0x04` was therefore
not a neutral placeholder — it was asserting that both switches were off.
That is the kind of stub worth being suspicious of: one that answers a
question it was never asked, in a way nobody notices because the answer is
plausible.

### Testing from inside the machine

ABC802 BASIC has `INP()` and `OUT`, which means the chip could be verified
by the emulated machine itself:

```
PRINT INP(67)   ->  36   RR0 = Tx empty + CTS from S2
OUT 67,1
PRINT INP(67)   ->   1   the register pointer works
OUT 67,2 : OUT 67,88 : OUT 67,2
PRINT INP(67)   ->  88   the interrupt vector round-trips
```

Every one of those returned `4` before. This is a better class of evidence
than a C-level assertion: it goes through the real port decoder, the real
mirror masks, and the ROM's own `INP` implementation, so it tests the path
the machine actually uses rather than the function I happened to write. It
is also the same trick `asm/examples/*.asm` play on the CP/M side — let
the emulated machine check its own emulator — reached here through BASIC
instead of assembly.

Worth remembering for the remaining devices: if the guest has a way to
poke at hardware, the guest is the best test harness available.

### One deliberate wrong-looking answer

RR2 is valid on channel B only. Channel A returns 0 rather than the
vector, even though returning the vector would have been easy and would
have looked fine. A caller reading the wrong channel should get an
obviously wrong answer instead of a subtly right one — the same instinct
as the negative control in Milestone 7, applied to an API rather than a
test.

### Scope held

Cassette lives on SIO channel B, and it stays unattached. The real
interface is bit-level — modulated through the SIO's synchronous clocks,
demodulated by frequency detection — which is a milestone in itself, and
the machine already has working disk storage, so it would be fidelity
rather than capability. Recorded as its own roadmap item rather than
half-done here.

---

## 2026-08-28 (last) — ABC802 Milestone 8: the line editor, and a gap that wasn't

Closed the arrow-key gap Milestone 2 left open — by discovering there was
nothing to close.

Milestone 2 had recorded it as "probing found no non-destructive
cursor-right; closing this properly means disassembling the ROM's own line
editor the way the ABC80 target's was." That framing quietly assumed the
mapping existed and simply had not been found. It doesn't.

I swept the editor with every byte `0x00`-`0x1F` plus a sample across
`0x80`-`0xFF`, typing `ABCDE<code>X` each time and reading back what
happened to the line. Its entire vocabulary is six codes: backspace
(`0x08`), discard-line (`0x18`), clear-screen (`0x0C`), and three line
terminators (`0x03`/`0x0A`/`0x0D`). **No cursor movement of any kind**, in
either byte range. This is a genuinely simpler editor than the ABC80's,
which does have a non-destructive cursor-right at `0x09`.

So Left maps to `0x08` — the only leftward motion that exists, and the key
that literally *is* backspace on the ABC80's own keyboard — and Right is
dropped, because the machine has nothing for it to do. The known-gap entry
becomes a documented hardware fact instead, phrased so nobody re-opens it
as a missing feature.

### On the method

The planned approach was a disassembly, and it would have been the harder
road for a worse answer. The editor sits in a region `bin/z80dasm` cannot
reach — it is only entered indirectly, so a reachability-based
disassembler renders it as `DB` bytes, which is exactly what happened when
I looked. I could have carved the region out and disassembled it linearly,
but then I would be reasoning about code paths I might have misread.

A 47-run behavioral sweep answered the question completely, and answered
it about *behavior* rather than about my reading of assembly. It also
turned up something the disassembly plan wasn't even aiming at: Ctrl-X
discards the whole line, has always worked, and had never been documented.

Worth remembering as a general preference. Disassembly tells you what the
code appears to do; a sweep tells you what the machine does. When the
question is "what does this accept?", and the input space is 256 values
wide, just try all of them.

---

## 2026-08-28 (end of day) — ABC802 Milestone 7: a second drive

Short one. `--disk` is now repeatable, so images take drives 0, 1, … in
order, with a `N:` prefix to pin one. The controller had tracked eight
units since Milestone 5 and the ROM has always addressed them as
`MO1:`/`MF1:`; the only missing piece was a way to attach one. No new flag
— repeating the existing one is what two-drive software expects and leaves
every single-`--disk` invocation working untouched.

The part worth recording is the **negative control**. The obvious test is
to save to drive 1 and load it back, and that passes — but it passes just
as happily if the unit number is being ignored entirely and everything
lands on drive 0. A round trip cannot tell those apart. What distinguishes
them is the read that must *fail*: `LOAD "MF0:D1TEST"` returning
`Error 21`, plus the two images' checksums showing only drive 1 changed.

That is the same lesson as [the boot-screen
postmortem](postmortems/2026-08-28-boot-screen-cannot-validate.md) wearing
different clothes. There the question was "would this output change if the
code were broken?" Here it is "would this test still pass if the feature
did nothing?" — and for a plain round trip on a two-drive system, the
answer is yes. Both are the same discipline: a passing check is only worth
what its counterfactual is.

Three sessions running now, that habit has caught something. It seems to
be the highest-value thing this project's write-ups have produced.

---

## 2026-08-28 (later still) — ABC802 Milestone 6: the 640K drive

Small follow-on to Milestone 5, and it produced one fact worth more than
the feature.

`--disk` now takes 640KB ABC832/834 images as well as 160KB ABC830 ones —
the ABC802's own native drive rather than the ABC80-era one it inherited.
Which controller is fitted comes from the image's size rather than a flag:
the formats differ by a factor of four, a real dump is always exactly one
of the two, and making the user restate what the file already says is a
good way to collect bug reports about the wrong geometry.

### The two drives interleave differently, in opposite directions

| Drive | Identity | Interleave 7 |
|---|---|---|
| ABC830 (160K) | does not boot | **boots** |
| ABC832/834 (640K) | **boots** | does not boot |

Both settled by booting real media both ways. Had either been assumed from
the other, that drive would have been silently broken, and the symptom is
"the disk does nothing" with no error to trace.

I nearly shipped this one badly. I wrote the code comment asserting the
identity result *before running the test* — reasoning from abc80sim's
table, which gives `MF` no interleave parameters. The reasoning happened
to be right, which is exactly what makes it a bad habit: a comment that
says "this was tested" when it has not been is worse than no comment,
because the next person has no way to tell the two apart. Caught it on
reread, tested both directions, and only then let the claim stand.

The trap underneath is the same one ABC80's Milestone 6 recorded: both
images have their directory at sector 16, and **sector 16 reads correctly
under either mapping**, because track-boundary sectors map to themselves.
So being able to see filenames in a hex dump proves nothing about
interleave. Only booting does. Worth knowing before anyone adds the `SF`
or `HD` types.

### A postmortem paying off twice

The `ADMINISTRATION 800` screen this milestone boots is the first time
*real* software has exercised Milestone 3's row-attribute decode — its
title bar is inverse-video and its rules are Row Graphic mosaics. That
path is precisely the one [the boot-screen
postmortem](postmortems/2026-08-28-boot-screen-cannot-validate.md) pointed
out the ROM's own boot screen could never test, and which had until now
only ever been driven by a synthetic screen built for the purpose. It
renders correctly, on 1983 output that knew nothing about this emulator.

Nothing was done to make that happen; it fell out of a milestone about
disk geometry. But it is the check the synthetic test was standing in for,
and it is worth recording that the stand-in turned out to be honest.

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
