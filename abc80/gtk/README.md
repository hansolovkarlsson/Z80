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
../bin/abc80-gtk --help
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
that precise value). Background stays black either way. **Confirmed by
the user, hands-on**: "it looks great."

**A note on how this window gets tested going forward**: automating
`screencapture`/`osascript` against the user's real desktop (as earlier
verification in this file did) turned out to be disruptive - it steals
focus and switches Spaces while they're working on other things. From
here on, changes to this app are verified by clean compilation, a
non-visual smoke test (launch, confirm no `Gtk-CRITICAL`/`Gtk-WARNING`
output, confirm a clean exit), and the user's own hands-on look rather
than an automated screenshot - the entries below reflect that.

**A margin around the canvas**: user-reported - the drawing area sat
flush against the window edges on every side, making the "monitor" look
crammed into its own frame. Fixed with a plain widget margin
(`ABC80_GTK_CANVAS_MARGIN`, 24px) applied to the `GtkDrawingArea` itself
via `gtk_widget_set_margin_*()` - real empty space outside
`draw_screen()`'s own `cairo_scale()`'d coordinate space, not part of
the emulated picture at all, centered within the window
(`gtk_widget_set_halign`/`valign(..., GTK_ALIGN_CENTER)`). Follow-up,
also user-requested: that margin initially showed the default GTK theme
background - a different color from the canvas's own black, so it read
as a visible border rather than blending in. Fixed with a small
`GtkCssProvider` (`.abc80-canvas-area { background-color: black; }`)
applied to `layout_box`, the container `menu_bar`/`drawing_area` both sit
in - both children are opaque and paint over their own allocated area
regardless, so this only actually shows through in the margin itself.
Always black, not conditional on `--amber`, since `draw_screen()` never
changes the background, only the foreground palette.

**A File menu**: `Save Program…`/`Load Program…`/`Take Screenshot…`, a
real in-window `GtkPopoverMenuBar` built from a `GMenu` model (not
`gtk_application_set_menubar()`'s native-menu-bar integration, which
needs desktop-shell-specific settings to actually show anything on
X11/Wayland - the in-window bar is visible the same way on every
platform). Directly answers the real gap that prompted asking for this:
BASIC's own interactive `SAVE`/`LOAD` hang forever, since real cassette
hardware isn't emulated (see `abc80/docs/ABC80_ROADMAP.md`'s Milestone
4) - Save/Load Program instead call the same
`abc80_cassette_quicksave()`/`abc80_cassette_quickload()` the CLI's own
`--quicksave`/`--quickload` flags already use, via the modern async
`GtkFileDialog` (GTK 4.10+) rather than the older, now-deprecated
`GtkFileChooserDialog`. **Confirmed by the user, hands-on**: "It works.
Load/Save too." "Take Screenshot" renders through the identical
`draw_screen()` the live window uses, against an offscreen Cairo
surface, so the saved PNG always matches what's on screen (amber
palette, cursor blink phase, and all) rather than a second
implementation that could drift from it.

**A real `-h`/`--help`**: user-reported - `--amber` was missing from
`--help`'s output. Root cause: this app never had a dedicated `-h`/
`--help` handler at all, unlike `bin/abc80`'s own - an unrecognized
argument's terse one-line fallback message happened to double as the
only "usage" text anyone would ever see, and it *did* already list every
flag correctly by the time this was reported (generated from the same
source, not actually stale), but there was no way to see it without
triggering what looked like an error. Added a real `print_usage()` (one
line per flag plus an indented description, mirroring
`abc80/emu/src/main.c`'s own style exactly) and wired `-h`/`--help` to
call it and exit `0` - the unrecognized-argument path now calls the same
function too, instead of keeping its own separate, driftable copy of the
flag list.

**Ctrl-C now breaks a running program**: user-reported - Ctrl-C didn't
stop BASIC the way it does in `bin/abc80 --interactive`. Root cause: a
real terminal's raw mode pre-folds Ctrl-<letter> into a single control-
code byte before `--interactive`'s own `poll_stdin_byte()` ever sees it,
but GDK reports the plain letter keyval plus a separate Control-modifier
bit instead, and `on_key_pressed()` was ignoring that modifier state
entirely (`(void)state;`). Fixed by translating any Ctrl-<letter> chord
to its standard ASCII control-code byte (`Ctrl-A` through `Ctrl-Z` →
`0x01`-`0x1A`) before the existing plain-key switch runs - real ABC80
hardware then sees the identical `0x03` byte a real terminal's raw mode
would have produced for Ctrl-C, and Ctrl-X ("backspace the whole line,"
per `ABC80_BASIC_REFERENCE.md`'s Keyboard section) now works too, for
the same reason.

**Cmd-Q/Cmd-S/Cmd-O**: user-requested app-level shortcuts for
quit/save/load. Bound via GTK's own portable `<Primary>` accelerator
modifier (`gtk_application_set_accels_for_action()`) rather than a
hand-rolled `GDK_META_MASK` check in `on_key_pressed()` - `<Primary>`
resolves to Cmd on macOS and Ctrl elsewhere automatically, and
GtkApplication's own accelerator dispatch already runs before a key
event would reach `on_key_pressed()` at all, so there's no interaction
with the Ctrl-<letter>-to-ABC80-keyboard translation above (Cmd and
Ctrl are different modifier keys - no ambiguity). `<Primary>S`/
`<Primary>O` bind directly to the existing `win.save-program`/
`win.load-program` actions the File menu already uses; `<Primary>Q`
is a new app-level `app.quit` action (quitting isn't really a
per-window concept the way Save/Load are) whose handler destroys the
real window - the same `on_window_destroy()` path a close-button click
or `SIGTERM` already drives, so a pending `--quicksave` still flushes
from Cmd-Q exactly like it does from every other exit path, rather than
a second copy of that logic.
