# The boot screen cannot validate the feature

**Date**: 2026-08-28
**Area**: `abc802/emu/src/chargen.c`, ABC802 Milestone 3
**Severity**: no shipped bug — this is a near-miss about verification

## What happened

Milestone 3 implemented the ABC802's character-generator decode: the font,
the hardware cursor, per-character inverse video, and the three row
attributes (Row Graphic, Row Flash, Row Clear). The obvious way to check
it was `bin/abc802 --screenshot`, which renders the real ROM's own boot
screen after a real boot. That screenshot came out perfect on the first
try — banner, typed line, answer, prompt, cursor, all correct pixels.

It was also nearly worthless as evidence.

**The ROM's boot screen uses no Row Graphic, no Row Flash, and no Row
Clear.** The attribute state machine — the large majority of what the
milestone actually implemented — could have been deleted entirely and that
screenshot would have been byte-for-byte identical.

## Root cause

Not a defect in the code. A defect in the *inference* the test invited:
"the output is right, therefore the implementation is right." That holds
only where the input exercises the implementation, and this input did not.

What made the trap convincing was that the input was maximally real: not a
mock, not a fixture, but an actual 1983 ROM booting actual hardware
behavior and drawing an actual screen. Realism and coverage are
independent properties, and realism is the one that *feels* like rigor.

## Why it survived

It didn't — it was caught during the milestone, by asking "what would this
screenshot look like if the attribute code were broken?" and realizing the
answer was "exactly the same."

Two things made that question likely to get asked, and both are worth
keeping:

- The decode had been deliberately written as a **pure function** over an
  `Abc802Screen` struct, so feeding it a synthetic screen was cheap. If it
  had read emulator globals, the path of least resistance would have been
  to keep booting the ROM and looking at the result.
- The repo already had the precedent: `bin/abc80-chargen-dump` exists for
  the sibling target for the same reason.

## What changed

`bin/abc802-chargen-dump` (`make abc802-chargen-dump`) drives a synthetic
character RAM that uses **every** attribute — graphics on, flash on and
off across both clock phases, row clear, inverse video, the cursor, and an
attribute switched off partway along a row — and prints the result as
ASCII art. It shares the same decode and needs no CPU core. It also prints
the ROM's own attribute-code inventory, so the documented encoding can be
re-derived rather than trusted.

**The tool then caught the same class of mistake in itself**, which is the
part most worth recording. Its first version demonstrated "Row Graphic
switched off mid-row" using the string `"GFX"`/`"TXT"` — and uppercase
letters are **byte-identical in both halves of the character ROM**. Only
codes `0x21`-`0x3F` and `0x60`-`0x7F` differ. So that row rendered
identically whether the attribute worked, was ignored, or was inverted:
a test of the attribute that could not fail. It now uses digits, which
genuinely differ between the halves.

Two near-misses of the same shape in one milestone, at two levels of the
stack, is a pattern rather than a coincidence.

## The lesson

Before trusting a passing check, ask the counterfactual: **if the code
under test were broken, would this output change?** If not, the test is
measuring something else — however real, however authentic, however
satisfying the output looks.

The corollary for this project specifically: real ROMs and real 1980s
software are excellent *oracles* for the paths they take and say nothing
at all about the paths they don't. That is the identical blind spot
[ZEXALL had for I/O
instructions](2026-08-28-block-io-opcodes-missing.md) — same lesson, found
independently at the other end of the codebase.
