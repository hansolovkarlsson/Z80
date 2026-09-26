# A stand-in answers only where it matches

**Date**: 2026-09-25
**Component**: `cpm/gtk/src/main.c` (`bin/z80-gtk` showing nothing until
a keypress) and the debugger's break keys in the three ABC GTK windows
**Found by**: the user, running diagnostics built for the real window and
the real terminal

## What happened

Two problems reported from the morning's hand-check of the GTK windows
were each diagnosed on something standing in for the real thing, and
each diagnosis was wrong in a way the stand-in could not show.

**`bin/z80-gtk --debug` opened blank.** `bin/z80` had printed its start-up
lines, the stop and the prompt (a `sample` showed it waiting in `fgets`),
yet the window showed nothing until Enter was pressed. The first guess,
that VTE had not processed output written before the window was mapped,
became a change (spawn from the terminal's `map` signal) and was
committed in `eda6550`, flagged as unconfirmed. It changed nothing.

The second attempt tried to avoid taking the user's desktop by probing a
VTE widget that was never put in a window, reading its text back with
`vte_terminal_get_text_format()`. The probe reproduced the blank, and
about an hour of experiments on it built a detailed story: a termios
change made just after spawn raced VTE's own pty setup and discarded
earlier output. VTE's source was read and a packet-mode pty test showed
the kernel delivering every byte. Then the probe lost a line with no
termios change at all, while reporting the cursor on the row *below* it.
Its text snapshot had never been reliable, so none of the story's
evidence meant anything.

**F12 as a second break key** went in (`eda6550`) on the premise that a
Swedish Mac cannot type Ctrl-], which was true of the one place it had
been tried, a GTK window. The user then found F12 did nothing there
either: GTK's macOS backend delivered no event for any function key. And
in a real terminal, later the same day, Ctrl on the key right of Å
arrived as Ctrl-] with no help at all. The premise held only in the
windows, and the key chosen to work around it worked only outside them.

## Root cause

In both cases the conclusion was drawn from an environment that differed
from the real one in exactly the respect under test.

- An unrealized VTE widget is not a mapped one. Whatever VTE does
  differently before a widget is on screen was the very thing in
  question, so the probe could not say anything about it.
- A GTK window and a terminal get keys through different machinery: GDK
  reports a key by keysym and hands on only what its backend delivers,
  while a terminal turns a key chord into bytes itself. "Ctrl-] cannot be
  typed" was a statement about GDK on macOS, recorded as one about the
  keyboard.

The actual `bin/z80-gtk` fix was found only when the user ran a
diagnostic that opened three real windows side by side, each starting
`bin/z80` a different way and printing its terminal's text and cursor:
creating the pty with `vte_pty_new_sync()` and attaching it before
spawning showed everything, `vte_terminal_spawn_async()` nothing. Why
VTE loses the output the other way is still not known.

## Why it survived

- **The stand-in was chosen for convenience, not fidelity.** The reason
  to avoid a real window was sound (this repo's own rule, since
  automating the user's desktop steals focus). But "what can I observe
  without disturbing anyone" became "what is true", and the probe's
  limits were never tested before its readings were trusted. The first
  version of the probe had already dropped `hello.com`'s ordinary output,
  and was abandoned for it; the second round built a variant of the same
  probe and trusted it without running that check again.
- **A mechanism read in the source made the story feel settled.** VTE's
  packet-mode handling, the kernel's control bytes, the post-spawn
  `set_pty()`: each was real, so the theory built from them looked
  understood. That is `2026-09-24-a-paused-machine-looks-like-a-running-one.md`'s
  key-gate theory again, in a different subsystem.
- **A guess was committed with a caveat.** `eda6550`'s record said the
  `map` fix was "unconfirmed until the window is looked at", which is
  honest, but a caveat does not stop the change sitting in the tree as if
  it were a fix.

## What changed

`bin/z80-gtk` attaches the pty before spawning (`75955f0`), confirmed by
the user in a real window, and the `map`-signal guess was removed with
it. The windows gained break keys established by a key-logging window on
the user's keyboard (`87c272e`: Cmd-. and Ctrl on the key where a US
layout has `]`), and the records were corrected to say the Ctrl-]
problem was the windows', not the keyboard's (`8ff4bd8`).

For the class: when the question is about an environment that cannot be
driven from here (a mapped window on the user's desktop, the user's
keyboard, the user's terminal), the fastest route today was a small
diagnostic that **runs in that environment and prints its findings**,
handed to the user to run (`! ./diag`), with the variants to compare
side by side in one run. Three such round trips settled what an hour on
the stand-in could not. And a stand-in is used only after it has been
seen to reproduce something known: had the probe been checked against a
plain program's output first, its readings would never have been
trusted.
