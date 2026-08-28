# The DART's single receive byte, honored on one path but not the other

**Date**: 2026-08-28
**Area**: `abc802/emu/src/main.c` (`--interactive`), ABC802 Milestone 2
**Severity**: caught during development; the feature did not work at all

## What happened

The first working version of `bin/abc802 --interactive` was tested the
obvious way — by piping a command into it:

```
$ printf 'PRINT 6*7\r' | bin/abc802 --interactive --columns 80 --cycles 30000000
```

BASIC received **nothing at all**. Not a mangled line, not a partial one:
the screen showed the sign-on banner and an idle cursor, exactly as though
no input had ever been sent.

## Root cause

The ABC802's keyboard is a serial device on DART channel B, and **the DART
holds exactly one received byte**. `abc802_keyboard_busy()` already existed
to express this, and the new code did check it — it sent the next byte
only once the ROM had consumed the previous one.

That is necessary but not sufficient. "The ROM has read the byte out of
the receive register" is not the same as "the ROM has finished processing
the keystroke." Reading the register clears `rx_ready` almost immediately;
the ROM's own input routine then does considerably more work before it is
ready for another. Draining a pipe at that rate meant each keystroke
overwrote its predecessor's processing, and essentially the whole line was
lost.

The pre-existing `--type` path had this right, and had for a milestone:

```c
// Gap between keystrokes, in T-states (~0.1s of emulated time at 3 MHz).
// Generous on purpose: the ROM discards input while it is still
// initializing after the sign-on banner...
const long long key_gap = 300000;
```

The new interactive path did not adopt that gap, on the implicit
assumption that a human types slowly enough not to need one.

## Why it survived

It didn't survive past its first test — but the *reasoning error* is the
interesting part, and it would have survived indefinitely under
hands-on testing only.

The assumption "a human cannot type faster than 10 characters per second"
is true. It is also irrelevant, because a human at a keyboard is not the
only input source:

- a pipe delivers a whole line instantly,
- a paste delivers a whole line instantly,
- and this project's own scripted testing is *entirely* piped input.

The feature was "live interactive keyboard," so the mental model was a
person at a terminal. The very first test was not a person at a terminal,
and could not have been.

## What changed

`--interactive` now enforces the same ~0.1s (300,000 T-state) gap
`--type` always used. Unsent input is not dropped — it waits in the host's
own terminal or pipe buffer until the gap expires, so a paste arrives
intact, just drained at the speed the real machine could accept it. This
is not throttling bolted on for convenience: it is what emulating a
one-byte receive register actually requires.

The GTK app (Milestone 4) inherits the same constraint and needed its own
answer, because a GDK key event **cannot** be left waiting in a terminal
buffer the way a CLI keystroke can. It queues keystrokes in a small ring
buffer and drains them at the same rate.

### The follow-on the fix created

Gating input behind the gap immediately broke multi-byte input. UTF-8
sequences (`Å` is two bytes) and ESC sequences are assembled across
successive polls, and they time out after 0.05s of *real* time — shorter
than the key gap is at real ABC802 speed. Gating the continuation byte
behind the gap expired every one of them, and no accented letter could
ever arrive.

Resolved by restarting the gap **only when a byte is actually delivered**,
never on a partial read. A `-1` return means "part of a sequence consumed,
need the rest promptly," and gets it.

## The lesson

**A modeled hardware limit constrains every input path, not just the one
it was discovered on.** `key_gap` looked like a `--type` implementation
detail — a pragmatic constant for feeding a scripted string. It was
actually a property of the DART, and therefore binding on any code that
hands the machine a keystroke. Both later input paths needed it; the GTK
one needed a different mechanism to achieve it.

Second, smaller: **the first test of an "interactive" feature is almost
never interactive.** Designing for the human case alone means the feature
is broken for the only way it will actually be tested — and, more
importantly, for paste, which real users do constantly.
