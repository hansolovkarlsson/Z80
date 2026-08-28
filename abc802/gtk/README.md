# bin/abc802-gtk — a GTK4 window for the ABC802

Built by the opt-in `make abc802-gtk`; never part of `make` or `make test`,
so the default build stays free of the GTK4 dependency. Needs `gtk4`
(via `pkg-config`) and nothing else — see "No SDL2" below.

```
make abc802-gtk
bin/abc802-gtk --columns 80
bin/abc802-gtk --columns 80 --disk system.img --disk games.img
```

## What it is

A real Cairo pixel framebuffer running the CPU core in-process on a
`g_timeout_add()` batch loop — the same shape as `bin/abc80-gtk`, and
deliberately **not** the shape of `bin/z80-gtk`, which is a thin VTE
launcher that spawns the real `bin/z80` on a pty. A character-cell
machine's screen is a bitmap, not a terminal, so there is nothing for a
terminal widget to do.

It shares `abc802_step()` (`abc802/emu/src/step.h`) with `bin/abc802`'s own
`--interactive` loop, so the per-instruction machine logic — the M1
notification, the CTC tick, interrupt delivery — exists once rather than
twice. That extraction is this milestone's own, mirroring what ABC80's
Milestone 11 did for the same reason.

## Why this file is so much shorter than abc80/gtk/src/main.c

Because the pixel decode was already done and already verified.
`abc802_render_pixels()` (`abc802/emu/src/chargen.c`) is a pure function
over an `Abc802Screen` struct, so this app only turns its output into a
Cairo surface. The mosaic font, all three row attributes, per-character
inverse video and the hardware cursor come along for free, and **cannot
drift** from what `bin/abc802 --screenshot` produces, because there is
only one implementation.

ABC80's GTK app carries its own decode instead, and had to: that target's
CLI renders Unicode block glyphs rather than pixels, so there was nothing
to share.

The one rendering choice made here rather than inherited: the decoded
pixels are uploaded as a single Cairo image surface and blitted scaled,
where `bin/abc80-gtk` emits a `cairo_rectangle()` per lit pixel. At
480x240 that is up to 115,200 rectangles per frame; one surface upload is
simpler and faster. `CAIRO_FILTER_NEAREST` keeps the pixels square when
scaled up — the default bilinear filter turns a 6x10 character into mush.

## No SDL2

`bin/abc80-gtk` links SDL2 for live SN76477 audio, and that audio callback
is its only thread. The ABC802's only sound is a speaker strobe this
emulator decodes but does not sound (see `../docs/ABC802_ROADMAP.md`), so
this app needs no SDL2 and has **no threads at all**.

## How changes here get verified

Automating `screencapture`/`osascript` against the user's real desktop is
disruptive — it steals focus and switches Spaces while they are working on
something else. `abc80/gtk/README.md` records that lesson, and this app
inherits the conclusion rather than rediscovering it. Verification is:

1. **Clean compilation** with `-Wall -Wextra`.
2. **A headless render**, which is this app's own answer to the problem:

   ```
   bin/abc802-gtk --columns 80 --type "PRINT 6*7
   " --screenshot shot.png
   ```

   `--screenshot` opens no window and never creates a `GtkApplication`. It
   runs the machine unpaced, then renders one frame through the *identical*
   `draw_screen()` the live window uses, against an offscreen Cairo
   surface. So the saved image is real evidence about the real renderer,
   obtainable without a desktop — which is what makes changes to this
   window checkable at all. The File menu's "Take Screenshot…" goes
   through the same function.
3. **A non-visual smoke test**: launch, confirm no `Gtk-CRITICAL` /
   `Gtk-WARNING` output, confirm a clean exit. `SIGTERM`/`SIGINT` are
   routed through `g_unix_signal_add()` so shutdown runs on the main loop
   and tears the window down exactly as closing it does — without that, a
   `kill` would terminate the process outright and the test could not tell
   a clean exit from a crash.
4. **The user's own hands-on look**, for anything about how it feels.

Verified this way at the time of writing: clean build, headless renders at
both column widths (including a multi-line BASIC program and Swedish
letters), and a launch/terminate cycle exiting 0 with an empty log.

## Keyboard

GDK keyvals equal ASCII/Latin-1 codepoints for printable characters, so
most keys need no table. What needs handling:

- **Return** → `0x0D`.
- **Backspace/Delete** → `0x08`, this ROM's real destructive delete. A
  terminal's Backspace sends DEL (`0x7F`), which this ROM treats as an
  ordinary printable character — so passing it through would corrupt the
  line rather than erase it.
- **Ctrl-\<letter\>** → `0x01`-`0x1A`. GDK reports the plain letter plus a
  modifier bit rather than pre-folding it the way a tty driver does.
  Ctrl-C matters in particular: it reaches BASIC as `0x03`, where a break
  belongs.
- **Å/Ä/Ö/Ü/É** go through the emulator's own charset table, the same one
  the renderer decodes with.

**Left arrow deletes; the other three do nothing.** A full sweep of the
ROM's line editor (Milestone 8) established that its entire vocabulary is
backspace, discard-line, clear-screen and three line terminators — there
is no cursor movement on this machine at all. Left therefore maps to
`0x08`, the only leftward motion that exists, and the rest are dropped.

Keystrokes are queued in a small ring buffer and handed to the DART no
faster than one per ~0.1s of emulated time. That is not throttling for its
own sake: the DART holds exactly one receive byte, and a GTK key event
cannot be left waiting in a terminal buffer the way a CLI keystroke can,
so without a queue a fast typist's keys would overwrite each other.

## Known gaps

- **No File dialog for disks.** `--disk FILE` attaches floppy images at
  launch (160K ABC830 or 640K ABC832/834, repeat for more drives), and
  BASIC's own `SAVE`/`LOAD` then work against them — but there is no
  in-window way to swap a disk, the way `bin/abc80-gtk`'s Save/Load
  Program dialogs work for that machine's cassette. A "Change Disk…" menu
  item would be the natural addition.
- **No color picker.** `bin/abc80-gtk` grew one; here the amber phosphor
  is fixed at the machine's real `(247, 170, 0)`.
- **Window size is fixed at startup** to 2x the emulated 480x240. The
  canvas scales with the window, but there is no zoom control.
