# A roadmap's "why" is the least-tested prose in the repo

**Date:** 2026-08-31
**Targets:** ABC80 (both instances)
**One-line lesson:** Everything in a `*_COMPLETED.md` was true when
written because something ran; a planned-work justification is written
*before* anything runs, and nothing ever goes back to check it.

## What happened

Two planned-work entries in `ABC80_ROADMAP.md` were acted on in one day.
Both carried a stated reason. Both reasons were false, and both dissolved
on the first measurement — in each case a measurement taking under two
minutes.

**"ABC-DOS scans all eight drives at boot."** The second-drive entry said
so, and added the corroborating detail that it is visible in
`ABCBUS_TRACE=1` output as a walk of units 0-7 reading directory sectors
16-23. Running that trace shows a full boot issuing **four** bus commands,
every one addressed to unit 0. The described behaviour belongs to the
ABC800 family's DOS — a different ROM on a different target — and had been
written down as a fact about this machine.

**"The per-instruction video-timing work is the obvious first suspect."**
The performance entry recorded that `bin/abc80` runs several times slower
than `bin/abc802` on the identical shared core, and named that suspect
while saying plainly "not investigated". One `sample` run put ~96% of
samples in `read()` and `__select()` against 66 in `abc80_step()`: the run
loop was issuing two syscalls per emulated instruction to poll stdin.
Nothing to do with video timing.

## It kept happening, including to me

Two more of the same shape turned up the same day, after this was written.

**"The cassette interface is bit-level, modulated through the SIO's
synchronous clocks and demodulated by frequency detection."** A trace of a
real `SAVE` shows the ROM handing the SIO whole *bytes*. The modulation is
hardware past the SIO, so the byte stream is the protocol boundary and the
feature was a fraction of the predicted size.

**"`DOSGEN` scans past the end of the modeled drive; whether the real card
answers differently, or whether it needs a drive-geometry reply, is not
known."** Neither. Removing the controller's range check entirely, so
out-of-range sectors succeed, produces byte-identical output — the card's
answer has no influence at all. DOSGEN is filling a fixed-size free-list
bitmap, and it completes correctly.

And then the same failure in my own writing, which is the part worth
keeping. Having written this postmortem, I ended a commit message with a
confident mechanism for why the cassette load failed: *the ROM drives the
SIO's hardware CRC generator, and this emulator does not implement it.* It
reads like something looked up. It was not: `WR5` bit 0 and `WR3` bit 3 —
both visible in a trace already on screen — are 0, so that hardware is
never enabled. The real cause was
[delivery pacing](2026-08-31-too-fast-is-a-correctness-bug.md).

So the failure is not specific to roadmaps, or to inheriting someone
else's note. It is what happens whenever an explanation is written at the
moment of least knowledge and then not re-checked, and a commit message is
just as good a place to do that as a planning document. The only reason
this one was caught in a day rather than in six weeks is that the next
session happened to continue the same work.

## Root cause

Not the individual errors — those are ordinary, and one of them was even
labelled a guess. The cause is structural: **a roadmap's "why" is written
at the moment of least knowledge and is never revisited.**

Each of these sentences was composed while planning work that had not
started. Neither was wrong at the level of "someone was careless"; the
video-timing suspect was genuinely plausible (this target really does do
per-instruction video work the ABC802 does not), and the drive-scan claim
was probably a true observation about the wrong machine, carried across
while writing up two similar targets in the same week.

## Why it survived

Three things, and the third is the one that generalises.

**Nothing executes a roadmap.** This repo is unusually good at making
claims executable — the BASIC references have checks behind them, the
hardware facts get fixtures, `*_COMPLETED.md` entries describe things that
were run. A roadmap is the one document with no such pressure. It is prose
about the future, and prose about the future cannot fail a test.

**A guess decays into a finding.** "Not investigated; X is the obvious
first suspect" is honest when written. Read six weeks later, the hedge is
skimmed and the suspect is retained. By the time the work starts, the
roadmap looks like it is telling you what you will find. The
drive-scan claim had lost its hedge entirely: it read as a plain
statement of fact, complete with the trace output that would confirm it.

**A justification's job ends when the work is approved.** Once a task
looks worth doing, nobody re-reads why. Both entries had already served
their purpose — they got the work prioritised — and the sentence was then
carried into the implementation as background rather than as a claim.

## What changed

Both entries were corrected in place, with the false claim named rather
than quietly deleted: `ABC80_REFERENCE.md` now records that ABC-DOS does
*not* scan drives at boot (and what it does instead — four commands, all
unit 0), and the roadmap's Performance section records what the slowness
actually was. Naming it matters more than removing it, because the next
person to read a plausible-sounding roadmap sentence should have an
example of one that was wrong.

The reusable part, which no file can enforce:

**Measure the premise before building on it, when measuring is cheap.**
Both of these cost under two minutes. In both cases the measurement was
*easier* than the work it was justifying, and in both cases it changed
what the work rested on — the second-drive milestone found better evidence
(a real `DR0`-`DR6` device table in the ROM at `0x6EB5`), and the
performance fix found an entirely different bug.

**Treat a roadmap's reason as a claim with no test behind it.** That is
the accurate status. It is not a lie and not usually careless; it is
simply the one category of assertion here that nothing ever re-checks.
This is a sibling of
[naming a source is not consulting it](2026-08-30-naming-a-source-is-not-consulting-it.md):
that one is about a note saying where an answer lives, this one about a
note saying what the answer will be. Both are tasks wearing the costume of
a status.

**Prefer a profile to a suspect.** The performance half is a special case
worth stating on its own. A performance guess is uniquely cheap to check
and uniquely tempting to skip, because the guess arrives with a mechanism
attached and feels explanatory. The split between user and system time
alone — 2.8s against 8.5s — refuted this one before any profiler ran.
