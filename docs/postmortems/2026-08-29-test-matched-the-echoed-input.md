# The test that matched its own input

**Date**: 2026-08-29
**Component**: `abc80/tests/run_tests.sh` — the ABC80 regression suite
**Found by**: deliberately breaking the code the test was written to protect

## What happened

A new check called `io-port-decode` guarded the boundary the ABC-bus card
sits on: port 6 is the SN76477 and must fall through to the CPU core's
flat `io_ports[]` array, while port 0 is the bus and must reach the card.
Widening the port decode by one would silently kill sound; narrowing it
would silently kill disks. The check drove it from BASIC:

```
OUT 6,42
PRINT INP(6)     -> expect 42
PRINT INP(0)     -> expect 255, no card fitted
```

and asserted that `" 42"` and `" 255"` appeared in the emulator's output.
It passed.

Then the port decode was deliberately widened to swallow port 6 — exactly
the regression the check existed to catch. **It still passed.**

## Root cause

`" 42"` was in the output, in the line the ROM had echoed back to the
screen when the keystrokes `OUT 6,42` were typed. The value BASIC actually
printed was `255`.

The check was asserting on its own input.

This is specific to how these targets are tested, and therefore not a
one-off. Every check drives the machine by typing at it and reads the
result off video RAM — and video RAM contains the typed line, because the
ROM echoes it. Input and output share one buffer. A substring search over
that buffer cannot distinguish "the machine computed this" from "I asked
for this", and any assertion whose expected value also appears in the
stimulus is silently satisfied by the stimulus.

The three-argument form made it worse by looking rigorous:

```sh
tl_want "$out" " 42" "port 6 (sound) round-tripping through io_ports[]"
```

The description says what the check means. Nothing verifies that the
pattern tests it.

## Why it survived

It survived for about forty minutes, which is the entire point of this
write-up: **it was never going to be caught by running the suite.** The
check passed on correct code and passed on broken code. Every signal a
green suite can emit, it emitted.

What caught it was deciding, before believing the suite, to break the
code on purpose and require each fault to surface. Five regressions were
injected — a status bit reverted, the port decode widened, a DIP-switch
default flipped, a sector interleave disabled, a font address bit changed
— and four were caught. The fifth was this.

Re-running the sweep afterwards then found two further checks that were
weak rather than wrong: `disk-boot` asserted only that no error appeared,
and the UFD-DOS check asserted a "file not found" message that a
completely dead card also produces. Neither was false, and neither would
have failed if the bus stopped carrying data.

So of seventeen checks written that afternoon, three were not testing what
their names claimed, and the suite reported 16/16 green throughout.

## What changed

- Numeric assertions no longer search the screen. `basic_numbers()`
  extracts the lines BASIC actually printed — lines that are nothing but
  spaces and digits — and checks compare them **positionally**, so the
  second printed value is the second printed value and cannot be an echo.
- `tl_want_eq` reports `expected '42', got '255'`, which names the real
  value; a substring miss could only ever say "not found".
- The two weak checks now assert on the card's own `ABCBUS_TRACE=1`
  output: that real command headers were issued, and that UFD-DOS issued
  at least twenty of them. Those fail when the bus stops carrying
  anything, which "no error appeared" never did.
- The reasoning is recorded at `basic_numbers()` itself, where the next
  person adding a check will read it, rather than only here.

What makes the class harder to reintroduce is the habit, not the code: a
new check is not finished when it passes. It is finished when it has been
seen to fail for the right reason. That is cheap here — inject the fault,
run the one suite, revert — and it is the only evidence that distinguishes
a test from a decoration.

## Footnote

The same session produced a second, smaller instance of believing an
unverified number. Investigating whether the SN76477 was broken, the
register value `OUT 6,255` was chosen as "obviously audible" and rendered
silence. So did `OUT 6,0`. Both are correct: `0xFF` disables the chip and
inhibits the mixer, and `0x00` selects an envelope mode that produces
nothing without a trigger. The value the demo tool itself uses, `0x40`,
produces a clean tone. Roughly twenty minutes went into suspecting the
emulator before reading the bit layout that was already documented in
`sound.c`'s own header comment — and the near-miss was writing that up as
a known gap. It is now a test (`sound-register-from-basic`) that covers a
path nothing else did: `step.c`'s decoding of BASIC's compiled
`ED`-prefixed `OUT`.
