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
../bin/abc80-gtk resources/rom --quickload prog.cas --quicksave prog.cas
../bin/abc80-gtk resources/rom --amber
```

(Run from inside `abc80/` so the default `resources/rom` path resolves —
same convention as `bin/abc80`.) `--disk`/`--ram32k`/`--quickload`/
`--quicksave` all behave identically to `bin/abc80`'s own flags (all four
share the CLI's own `disk.c`/`cassette.c` implementations unchanged).
`--quickload` injects at the same `PC == 0x02AA` trigger point the CLI
uses; `--quicksave` has no bounded "end of run" to hook here the way the
CLI does, so it flushes when the window closes instead (including via a
real `SIGINT`/`SIGTERM` - `kill`, Ctrl-C in the launching terminal, or the
host system stopping the process - which now drives a clean
`gtk_window_destroy()` rather than dropping a pending save silently).

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

**A real bug found on exit, fixed and confirmed**: closing the window
produced `Gtk-CRITICAL **: gtk_widget_queue_draw: assertion 'GTK_IS_WIDGET
(widget)' failed` - the pacing timer wasn't stopped when the window
closed, so it could fire once more against an already-destroyed drawing
area. Fixed with the standard pattern: the window's own `"destroy"`
signal removes the timer's saved `GSource` and clears `AppState.
drawing_area` to `NULL` (checked first in `on_timer_tick()` too, as
defense in depth against an already-queued tick racing the removal).
Confirmed by the user, hands-on: the warning is gone on a real window
close.

GRAPHICS-mode pixel geometry (the 2×3 sub-cell split within a 6×10 real
character cell) is grounded directly against MAME's real
`src/mame/luxor/abc80_v.cpp` `draw_character()` (`if (l < 3) r0 = 0; else
if (l < 7) r1 = 0; else r2 = 0;`) — scanlines 0-2/3-6/7-9 (3/4/3, since 10
doesn't split evenly by 3), not assumed or evenly divided.

**GRAPHICS mode confirmed working, via a real screenshot**: piping a
`SETDOT`-based BASIC test program (box border plus a diagonal line) into
`bin/abc80-gtk` and capturing a real `screencapture -x` shows a genuine
pixel box built from real 2×3 sub-cell block-mosaic squares — not a
Unicode approximation, not garbled text. Getting there surfaced a real
finding, not an emulator bug: real ABC80's `SETDOT` only pokes a target
cell's raw dot-pattern byte — it does *not* also write a `CHR$(151)`
("START GRAPHICS") marker into the row first, and a row's GRAPHICS/TEXT
mode is a persistent per-row latch that resets to TEXT at the start of
every row. A bare `SETDOT` with no preceding `CHR$(151)` on that row
renders through the ordinary chargen path instead (confirmed identical
in both `bin/abc80-gtk`'s Cairo renderer and the pre-existing
`bin/abc80 --interactive` terminal renderer, ruling out a GTK-specific
bug) — real hardware behavior, matching the `CHR$(151)` reference entry's
own wording ("starts graphics mode for **one line**"). Prefixing each
target row with `PRINT CUR(row,0);CHR$(151);` fixed it. See
`abc80/docs/ABC80_ROADMAP.md`'s Milestone 11 for the full writeup,
including a second, smaller finding (`SETDOT`'s documented `R: 0-72`
range actually errors at `R=72` — usable range is `0`-`71`).

To make this self-verifiable without a human at the keyboard (this
sandboxed environment has no Accessibility permission for synthetic
keystrokes), `bin/abc80-gtk` gained optional `isatty()`-gated stdin
scripted input — mirrors `bin/abc80`'s own `poll_stdin_byte()` exactly,
and only activates when stdin is piped/redirected, so real interactive
GDK keyboard input (already confirmed working, see above) is completely
unaffected.

**`--quickload`/`--quicksave` confirmed working, round trip verified
against the CLI**: quicksaved a short program from `bin/abc80`,
quickloaded it into `bin/abc80-gtk` (confirmed via `LIST`/`RUN` in a real
screenshot), then sent the running GTK process a real `SIGTERM` and
confirmed a clean exit plus a real `.cas` file. That file wasn't
byte-identical to the original CLI save at first, traced to a real but
benign cause — BASIC re-links each line's stored address pointer on
load, so a load-then-save round trip legitimately differs from a fresh
save — confirmed by reproducing the identical load-then-save round trip
through the CLI itself and diffing: byte-for-byte identical output,
proving the GTK wiring matches the CLI's own already-verified behavior.

**Per-pixel `cairo_fill()` performance checked, empirically**: filled
every graphics dot on the entire screen (the densest possible workload
this renderer can produce) and watched real CPU usage - peaked around
22% of one core at the existing ~30fps throttle. No glyph cache needed.

**Cursor now blinks, ported from `--interactive`**: user-reported gap -
the cursor block was solid/always-on, unlike the real machine and unlike
`bin/abc80 --interactive` (which has had a real `ABC80_BLINK_HZ` blink
since Milestone 8). Fixed by computing the identical `fmod()`-based phase
`render.c`/`main.c` already use and gating the cursor fill on it in
`draw_screen()`. Verified with three real screenshots ~0.2s apart at the
idle prompt: solid, then absent, then solid again.

**`--amber`, an opt-in amber-phosphor palette**: user-requested look, not
a claim about real ABC80 hardware - the base ABC80 shipped with a
white/green-ish monochrome display (this project's own default,
unchanged, still models that). The Luxor ABC800, ABC80's direct
successor, is well known for its amber CRT option, which the user liked;
`--amber` swaps `draw_screen()`'s foreground color from white to
`#FFB000` (a commonly used amber-phosphor swatch matching most terminal
emulators' own "amber" themes - no ABC800 hardware manual was available
to source an exact CRT phosphor chromaticity, so this isn't asserted as
that precise value). Background stays black either way. Built cleanly;
visual verification via `screencapture` wasn't possible this session (the
host display was inaccessible - `screencapture` failed with "could not
create image from display" even for a plain full-screen capture, unrelated
to this app), so this one hasn't had the usual real-screenshot check yet -
worth a quick hands-on look next time the window's actually visible.
