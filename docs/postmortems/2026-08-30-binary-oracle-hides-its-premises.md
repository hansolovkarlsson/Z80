# A binary oracle hides its premises

**Date**: 2026-08-30
**Component**: the ABC806 PAL16L8 investigation (`scripts/palanalyse.py`),
and the high-resolution colour work before it
**Found by**: printing the data instead of scoring it, after three wrong
verdicts

## What happened

Three times in one investigation, a pass/fail check said "no" to something
that was correct, and each time the natural reading — *the thing under test
is wrong* — was the false one.

**Twice about the PAL's column layout.** Evaluating `ABC-P4-1.bin` needs to
know which of the fuse array's 32 columns carries which signal. Two
candidate layouts were tried, each scored against a check that looked
unimpeachable: *with EME off and KEYDTR high, does the array select ROM
below `0x8000` and RAM above it?* Both printed `False`. Both were
discarded. The second of them was the correct layout, taken from the
device datasheet.

**Once about the memory model itself.** That same check was then applied to
the real layout and still failed — because the premise was false. The
ABC806 asserts `RAMD` across the *whole* low 32K (one of its product terms
is the bare literal `A15'`), with `ROMD` deciding only whether ROM
overrides on a read. "ROM low, RAM high" is not how this machine works, and
the emulator's own `memory.c` — written months earlier from behaviour — had
already got that right.

A fourth instance, in a different medium: three high-resolution lines drawn
in pens 1, 2 and 3 were reported as "all white, and colour is broken."
Reading the PNG's actual pixels gives `#FF0000`, `#00FF00`, `#FFFF00`. The
oracle there was an eye looking at a 480×250 image rendered small in a
terminal.

## Root cause

Every one of those checks encoded an assumption that was never stated and
therefore never examined:

- that the machine has a plain ROM-low/RAM-high split;
- that `I3`, `XML` and `RKDL` — three PAL inputs supplied by board logic —
  sat at the levels guessed for them;
- that colours distinguishable in principle are distinguishable *by eye at
  that scale*.

None of those was written down as a premise. They were baked into the
scoring function, where they became invisible.

## Why it survived

**Because a boolean carries no diagnostic information.** When a check
returns `False`, it does not say *which* of its conjuncts failed. The
result is indistinguishable between "your candidate is wrong" and "my
expectation is wrong", and the first reading is the one a person reaches
for, because the candidate is the thing they are consciously testing.

The failure compounds: having discarded a correct layout, the search
continued, which reinforced the belief that the layout was the problem.
Two wrong verdicts felt like evidence of a hard problem rather than of a
broken instrument.

It broke the moment the same data was **printed instead of scored**.
Decoded with the discarded layout, the product terms read as
`A15'.A14'.EME'` — the bottom 16K with EME off — and
`A14.B13.B12.B11`, which is exactly the `0x7800`-`0x7FFF` window. Nobody
needs an oracle to recognise that as a memory decode. A wrong column
mapping does not produce meaningful hardware equations by accident.

## What changed

`scripts/palanalyse.py` prints equations. It does not score them, and its
own header says why: the tool's job is to make the array legible, and
judgement stays with the reader who can see the whole picture rather than
one bit of it.

The general rule this project now works to:

> **When the subject is unknown, prefer an instrument that renders over one
> that judges.** A renderer's output is wrong in a way you can see; a
> judge's output is wrong in a way that looks exactly like being right
> about something else.

Corollaries that earned their place here:

- **A `False` from a compound check is a question, not an answer.** Before
  discarding the candidate, ask which conjunct failed.
- **Where the medium is visual, sample it.** Twelve lines of Python reading
  a PNG's pixels would have said `#FF0000` immediately; looking would never
  have.
- **State the premises next to the check.** The ROM-low/RAM-high assumption
  would not have survived being written down as a sentence — the machine's
  own memory model had been documented as an overlay for weeks.

## What this is not

It is not an argument against assertions. The regression suites here are
almost entirely pass/fail, and rightly so: they test *known* behaviour, so
their premises are settled and a `False` really does mean a regression.

The distinction is between checking something you understand and exploring
something you do not. This class of mistake belongs entirely to the second
case, and that is where an oracle should be replaced by a picture.
