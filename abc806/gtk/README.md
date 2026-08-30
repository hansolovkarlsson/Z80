# bin/abc806-gtk

A GTK4 window for the Luxor ABC806, built with `make abc806-gtk`. Opt-in:
never part of `make` or `make test`, on the same terms as the other two
GTK apps here.

```
make abc806-gtk
bin/abc806-gtk
bin/abc806-gtk --disk abc802/resources/disks/sys832-ufd.img
```

## What it is

A real Cairo pixel framebuffer running the CPU core in-process on a
`g_timeout_add()` batch loop — the same shape as
[`abc80/gtk`](../../abc80/gtk/README.md) and
[`abc802/gtk`](../../abc802/gtk/README.md), and **not** the thin VTE
launcher [`cpm/gtk`](../../cpm/gtk/README.md) is.

It is the shortest of the three, and the reason is worth stating because it
is the point of how this repository is arranged: nothing about the machine
lives here. `abc806_step()` (`../emu/src/step.h`) is shared with the CLI's
own `--interactive` loop, so the per-instruction logic exists once, and the
pixel decode is already a pure function (`abc806_render_pixels()` in
`../emu/src/chargen.c`, verified independently by
`bin/abc806-chargen-dump`). This file turns palette indices into a Cairo
surface and does nothing else with the machine.

So the whole picture comes along for free and cannot drift from what
`bin/abc806 --screenshot` produces: the colour attribute plane, the RAD
PROM's underline/flash/blank scanline substitutions, double width, the
cursor, and the high-resolution graphics layer composited underneath the
text.

## Two differences from bin/abc802-gtk

Both follow from this being the colour machine.

- **The framebuffer is palette-indexed, not monochrome.** That app maps
  every non-zero pixel onto one amber foreground; here each pixel byte is a
  pen 0-7 and the surface is built through `abc806_palette()`, the
  machine's own eight colours.
- **The flash phase has to be supplied.** The ABC802 blinks its cursor in
  *software*, toggling MC6845 R10 from its own clock interrupt, so pacing
  execution correctly was the entire implementation. The ABC806's flash is
  a hardware attribute with no software clock behind it, so this app
  derives the phase from elapsed real time exactly as the CLI does — at an
  assumed 2 Hz, which `../docs/ABC806_ROADMAP.md` lists as an open gap.

## No audio, and no threads

This machine's only sound is a strobe the emulator decodes but does not
sound, so unlike `bin/abc80-gtk` there is no SDL2 dependency and no
real-time callback thread. `gtk4` is the only requirement.

## How changes here get verified

Automating a screen capture against the user's real desktop steals focus
and switches Spaces while they are working — the lesson
[`abc80/gtk/README.md`](../../abc80/gtk/README.md) records at length. So
this app is built to verify *itself* instead:

1. **A clean build**, no warnings.
2. **Headless renders.** `--screenshot FILE` opens no window and never
   creates a `GtkApplication`; it runs the machine unpaced and then renders
   one frame through the *identical* `draw_screen()` the live window uses,
   against an offscreen surface. A saved image is therefore the same code
   path as a displayed one rather than a second implementation that could
   drift.
3. **A launch-and-terminate smoke test**: launch, confirm no
   `Gtk-CRITICAL`/`Gtk-WARNING` output, confirm a clean exit. `SIGTERM` and
   `SIGINT` reach the GTK main loop through `g_unix_signal_add()` rather
   than a raw handler, so shutdown runs as an ordinary main-loop callback
   and tears the window down exactly the way closing it does — without
   which a `kill` would end the process outright and this check could not
   tell a clean exit from a crash.

Verified this way at the time of writing: clean build; a headless render of
the ROM's sign-on; a headless render of
`FGCTL 2:FGPOINT 10,10,1:FGLINE 200,120,1` showing white text above a red
high-resolution line at 2x; and a launch/terminate cycle exiting 0 with an
empty log.

One deliberate detail in the headless path: **the flash phase is pinned**
rather than taken from the clock, so a screenshot is reproducible. A render
that sometimes catches the dark half of the flash cycle is a render that
sometimes fails.

## Options

```
--rom-dir DIR      ROM directory
--dos-rom FILE     DOS PROM at 0x6000
--disk FILE        attach a floppy image; repeat for drives 0, 1, ...
--interleave N     override the drive's own sector interleave
--columns 40|80    characters per line (default 80)
--screenshot FILE  headless: run, render one frame, exit
--cycles N         with --screenshot, T-states to run first
--type TEXT        with --screenshot, type TEXT before rendering
```

`--type` waits for the machine to have drawn something before sending its
first key, the same readiness gate the CLI uses: the ROM reports the
keyboard ready long before it is listening, and typing at T-state 0 loses
the opening characters.

## Keyboard

Printable keys, including Å/Ä/Ö/Ü/É, go through the emulator's own charset
table, so the keyboard can type every letter the screen can show.
Ctrl-`<letter>` is folded to 0x01-0x1A by hand, since GDK reports the plain
letter plus a modifier bit rather than pre-folding it the way a tty driver
does — Ctrl-C therefore reaches BASIC as a plain 0x03, which is where a
break belongs.

Left arrow maps to backspace and the other three arrows are dropped. On the
ABC802 that follows from a byte-by-byte sweep of its line editor, which
turned out to have no cursor movement at all. **Here it is inference** from
the same family and the same year rather than a sweep of this ROM, and is
listed as such in the roadmap's gaps.
