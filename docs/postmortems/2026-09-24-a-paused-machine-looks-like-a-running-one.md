# A paused machine looks like a running one

**Date**: 2026-09-24
**Component**: `abc802/docs/ABC802_ROADMAP.md` (a known gap that was not
one), and the reading of `bin/abc802 --interactive` that put it there
**Found by**: a profile, taken while investigating the gap it had caused

## What happened

Testing the debugger under `--interactive` through a pty, the session
typed `10 GOTO 10`, `RUN`, then one Ctrl-C, to check that Ctrl-C still
reached BASIC. The screen afterwards showed `RUN` and nothing else, the
same as before the Ctrl-C, and the same with the debugger off. That was
read as "Ctrl-C does not break a running program", and it went into the
ABC802 roadmap as a known gap, with a candidate cause: the emulator's
keyboard feed only hands a byte to the DART when the ROM is listening,
"which a running program may never do". It was committed that way in
`8e1f460`, with a journal entry saying it was found and not chased.

The next task was chasing it. The machine had been right all along. One
Ctrl-C **pauses** a running program and prints nothing; a second breaks it
with `Stop in line N.`; Ctrl-S at the pause runs one line. The details are
in `ABC802_BASIC_REFERENCE.md`, "Stopping a running program".

## Root cause

Not a defect in the emulator. A defect in the inference: "the screen did
not change, so the program is still running." For this program the screen
cannot tell the two states apart. `10 GOTO 10` prints nothing while it
runs and nothing while it waits, so a paused machine and a running one
render the same frame. A loop that *was* printing, once paused, stops
changing too, and a still screen then looks like a slow one.

The key-gate theory made it worse, because it was plausible. It named a
real mechanism in this emulator (`abc802_keyboard_ready()`), so the gap
read as understood when nothing had been measured.

## Why it survived

Three things, each of which this project has met before in another form.

- **The only instrument was the screen.** The run was judged by what it
  drew, and a render is only evidence of the states it can distinguish.
  That is `2026-08-28-boot-screen-cannot-validate.md` again: an input that
  never exercises the difference proves nothing about it.
- **One sample was taken as a trace.** During the investigation itself, a
  run that happened to end with PC inside the ROM's key-wait loop was
  briefly read as "it broke after all". A longer run with a command typed
  afterwards showed BASIC not answering, which is what a pause does. A PC
  at one instant says where the machine was, not what state it is in.
- **Writing it down felt like the careful choice.** It went onto the
  roadmap as an open item, marked "not chased", so it looked like
  information preserved rather than a claim made. But the item stated a
  behaviour as fact ("does not break"), and a reader would have started
  from it.

## What changed

The fix was to the records: the roadmap item was removed, the BASIC
reference gained the real behaviour with the ROM addresses behind it, and
the journal (entry 15) says what the earlier entry got wrong.

What makes the class harder to repeat is `basic-ctrl-c` in the ABC802
suite. It asserts on *state*, not on a frame: the pause is shown by the
output being identical after 100M and after 200M T-states, the break by
BASIC answering a command afterwards, and stepping by exactly one more
digit per Ctrl-S. It fails with either key dropped from the keyboard path.

And the instrument that settled it is worth reaching for first: `--profile`
(or the debugger's `r` at a breakpoint in the loop) says **where the CPU is
spending its time**. After the Ctrl-C, nearly every instruction ran in the
key-wait loop at `03EE`-`03FA`. That one line would have prevented the
roadmap entry. When a machine seems to be "still doing X", ask the machine
where it is before writing down what it is doing.
