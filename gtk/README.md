# GTK terminal launcher (work in progress)

`gtk/src/main.c` is a thin GTK4 launcher for the real `bin/z80` CLI
emulator — not a terminal emulator of its own, and not a modification of
`bin/z80` itself. It spawns the existing, completely unmodified `bin/z80`
binary as a child process attached to a pty, and hands that pty to a
`VteTerminal` widget (from the [VTE](https://wiki.gnome.org/Apps/Terminal/VTE)
library — the same widget GNOME Terminal uses), which does the actual
VT100/ANSI interpretation. `z80.c`/`cpm.c`/`emu/src/main.c` are untouched
by this: `console_emit()`/`console_read_char()` already just talk to
stdin/stdout, and a pty's slave side *is* stdin/stdout from the child
process's point of view — so reusing VTE as the display needs no core
emulator changes at all.

Kept as its own binary (`bin/z80-gtk`, built via `make gtk`, **opt-in** —
not part of `make`/`make all`/`make test`) rather than a flag on
`bin/z80`, so the default build stays free of the GTK4+VTE dependency
this needs. Build dependencies (via Homebrew): `gtk4`, `vte3` (provides
the `vte-2.91-gtk4` pkg-config module used here — VTE's GTK4 binding).

## Usage

```
make gtk
bin/z80-gtk cpm_disk/hanoi.com
bin/z80-gtk --ccp cpm_disk/ccp.com
```

Every argument is passed straight through to `bin/z80` (located as a
sibling of `bin/z80-gtk` via `_NSGetExecutablePath()`, not trusted from
`argv[0]`), exactly as if it had been typed directly at a shell.

## Status: work in progress, currently blocked

Builds cleanly and launches, but crashes **intermittently** on this
development machine (macOS 26.5.2) before any of this file's own code
runs — every crash so far happens inside `gtk_application_new()`, deep in
GLib's own `GType` registration and memory allocator
(`g_malloc0`/`mfm_alloc`/`_xzm_xzone_malloc_freelist_outlined`), with
`EXC_BREAKPOINT`/`SIGTRAP` on one run and `EXC_BAD_ACCESS` ("possible
pointer authentication failure") on another. That's a strong signal of
an ABI/build mismatch between Homebrew's precompiled GTK4/glib bottles
and this specific (very recent) macOS version, not a bug in this file —
confirmed via the actual macOS crash reports
(`~/Library/Logs/DiagnosticReports/z80-gtk-*.ips`), not guessed. The
standard `MallocNanoZone=0` mitigation for this class of crash didn't
fix it (just changed which way it crashes).

Because of this, **terminal rendering itself is still unconfirmed** —
the one run that didn't crash showed an empty window (no `bin/z80`
output visible), which hasn't yet been root-caused since the crash has
made this hard to iterate on reliably. Two independent things need
verifying once the environment issue is sorted: that the crash is really
gone, and that real CP/M program output actually appears in the
`VteTerminal` widget as expected.

Next steps, not yet attempted: rebuilding `glib`/`gtk4`/`vte3` from
source via Homebrew (`brew reinstall --build-from-source glib gtk4
vte3`) to rule out a bottled-binary mismatch, or retrying once Homebrew
ships updated bottles for this macOS version.
