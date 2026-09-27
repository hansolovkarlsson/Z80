# Two cursors, and a check that strips escapes

**Date**: 2026-09-26
**Component**: the `--interactive` console of `bin/abc80`, `bin/abc802` and
`bin/abc806` (each machine's `emu/src/main.c`)
**Found by**: the user, running each session by hand in a Linux terminal

## What happened

Asked to try the three ABC machines' live sessions on the Linux VM, the
user reported the cursor "flickering". It was shelved twice as minor, in
the ABC802 and then the ABC806 roadmap, with a guess at the cause. Asked
what it actually did, the user described it exactly: the cursor blinked
in the screen and just below it, by turns, as if jumping. It did the same
on the Mac.

There were two cursors. Each machine draws its own cursor into the
screen it renders, and the terminal's real cursor, never hidden, sat
wherever each redraw ended, one line below the screen, blinking on its
own schedule.

## Root cause

`abc80_console_init()` (ABC80 Milestone 8, `d14dae3`, 2026-08-13) put
the terminal in raw mode and nothing else. A program that paints its
own cursor has to hide the terminal's (`ESC [ ? 25 l`) and show it again
on exit; this one never did. `bin/abc802` and `bin/abc806` copied the
console code, and the omission with it: the ABC802's still opens with
"Mirrors abc80/emu/src/main.c's abc80_console_init()/_shutdown()".

## Why it survived

- **No check could see it.** Every automated look at a live session goes
  through `scripts/ptysession.py`, which strips ANSI escapes before
  anything is asserted, by design, so that checks read text. What a
  terminal *shows* is decided by exactly the escapes it strips. The
  suites asserted on the machine's screen and never on the terminal's
  state.
- **The cursor that was checked was the right one to check, and
  settled the question.** The machine's own cursor had real work behind
  it: the ABC802's software blink through R10, the ABC806's flash phase,
  the ABC80's `ABC80_BLINK_HZ`. With that verified, "the cursor works"
  read as done, and a second cursor was nobody's subject.
- **It was copied, not shared.** Two targets inherited the console setup
  by copying it, the pattern this repo already keeps for
  `abc806/emu/src/ports.c` on purpose. A copy carries its omissions as
  faithfully as its code, so one missing line became three.
- **"Minor" was recorded before it was described.** The first roadmap
  entry offered two mechanisms (the software blink meeting the redraw,
  or the terminal) without asking what the user saw. The description
  named the cause in one sentence.

## What changed

Each machine's console setup hides the terminal cursor when standard
output is a terminal, and its exit path shows it again, so Ctrl-\ leaves
the shell as it found it. The debugger prompt needs a cursor to type at,
so `z80dbg_set_cursor_hidden()` lets a machine say it hid it: the prompt
shows it while reading and hides it on resuming. The user confirmed it
in real terminals on both platforms (`e592ec0`).

For the class, only half has changed. The byte order was verified with
a raw pty capture that did not strip escapes, written in a scratch
directory for the purpose; no check in the suites asserts on terminal
state yet, so the next missing escape would survive the same way. That
check is on the CP/M roadmap. **Added 2026-09-27:** it now exists.
`scripts/ptysession.py --raw` keeps the escapes, and `terminal-cursor` in
each ABC suite asserts the cursor is hidden while running, shown at a
debugger prompt, hidden on resuming and shown at quit; removing each of
the three escapes in turn failed it. And when a symptom is reported from a
place that cannot be observed here, ask for a description before
shelving it or guessing at it: here it was the whole diagnosis.
