# ABC80 GTK window (Milestone 11)

`abc80/gtk/src/main.c` is a real GTK4 window for the Luxor ABC80 machine
target — unlike `cpm/gtk/` (a thin launcher that spawns the real `bin/z80`
under a pty and hands it to a `VteTerminal` widget), this runs the CPU core
directly in-process and draws real pixels via Cairo. `bin/abc80`'s own
`--interactive` mode renders through `abc80/emu/src/render.c`, which
deliberately approximates GRAPHICS mode (ABC80's real 2×3 block-mosaic
graphics) with Unicode "Symbols for Legacy Computing" sextant characters,
since a terminal cell can't address individual pixels — this window closes
that gap with genuine pixel-accurate rendering instead, using the same
chargen-ROM/video-timing-PROM decode logic `bin/abc80-chargen-dump`/
`bin/abc80-video-timing-dump`/`bin/abc80-render-demo` already verify.

Shares `abc80_step()` (`abc80/emu/src/step.h`) with `bin/abc80`'s own
`--interactive` loop, so the carefully-derived per-instruction logic
(keyboard debounce, sound-register write detection, the floppy/DOS bypass,
periodic PIO interrupt scheduling) isn't duplicated a second time. Runs
single-threaded: a `g_timeout_add()` callback fires every 5ms, runs a
real-time-paced batch of instructions (the same wall-clock-vs-emulated-time
pacing `--interactive` already uses), and queues a redraw at ~30fps.

Kept as its own binary (`bin/abc80-gtk`, built via `make abc80-gtk`,
**opt-in** — not part of `make`/`make all`/`make test`) so the default
build stays free of the GTK4 dependency. Only needs `gtk4` (Homebrew) —
no VTE, since this target never spawns a child process.

## Usage

```
make abc80-gtk
cd abc80
../bin/abc80-gtk resources/rom
../bin/abc80-gtk resources/rom --disk /path/to/disk.img
../bin/abc80-gtk resources/rom --ram32k
```

(Run from inside `abc80/` so the default `resources/rom` path resolves —
same convention as `bin/abc80`.) `--disk`/`--ram32k` behave identically to
`bin/abc80`'s own flags (both share `disk.c`'s real implementation).
`--quickload`/`--quicksave` aren't supported yet — not essential for "run
in a window," and can be added later if useful.

## Status: working - real pixel rendering and keyboard input confirmed

Builds cleanly and launches a real window titled "ABC80". Verified by an
actual screenshot (not just "it compiled"): the real ROM's own sign-on
banner ("ABC80") renders as genuine pixels — a blocky, low-resolution
letterform matching the real 6×10 chargen ROM scaled up, not a Unicode
approximation — with a real solid cursor block beneath it, exactly
matching what the real ROM draws to video RAM at boot. Keyboard input
reaching BASIC was confirmed hands-on by the user in a real session
(this sandboxed environment has no Accessibility permission to script
synthetic keystrokes itself, so that verification couldn't be automated
here).

**A real bug found on exit, fixed**: closing the window produced
`Gtk-CRITICAL **: gtk_widget_queue_draw: assertion 'GTK_IS_WIDGET
(widget)' failed` - the pacing timer wasn't stopped when the window
closed, so it could fire once more against an already-destroyed drawing
area. Fixed with the standard pattern: the window's own `"destroy"`
signal removes the timer's saved `GSource` and clears `AppState.
drawing_area` to `NULL` (checked first in `on_timer_tick()` too, as
defense in depth against an already-queued tick racing the removal).

GRAPHICS-mode pixel geometry (the 2×3 sub-cell split within a 6×10 real
character cell) is grounded directly against MAME's real
`src/mame/luxor/abc80_v.cpp` `draw_character()` (`if (l < 3) r0 = 0; else
if (l < 7) r1 = 0; else r2 = 0;`) — scanlines 0-2/3-6/7-9 (3/4/3, since 10
doesn't split evenly by 3), not assumed or evenly divided. **Not yet
independently verified against a real GRAPHICS-mode program's own true
block pixels** - only the TEXT-mode boot banner and keyboard input have
been confirmed hands-on so far.
