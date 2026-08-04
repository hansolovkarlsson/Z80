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

## Status: work in progress, blocked on a macOS 26 OS bug

Builds cleanly, and terminal rendering itself is now confirmed working —
running `bin/z80-gtk emu/zexall/ZEXALL-main/zexall.com` shows the real
`VteTerminal` widget with `bin/z80`'s own output rendering correctly. Two
separate crash causes were found along the way, one fixed in this
project's own code, one that isn't fixable here at all:

1. **Fixed**: `vte_terminal_spawn_async()` closes every inherited file
   descriptor below the process's open-file limit in the child before
   `exec` (glib's `fdwalk()` fallback — macOS has no `/proc/self/fd` to
   just enumerate the ones actually open). This shell's default
   `ulimit -n` is over a million, and walking that many overflowed the
   spawn thread's stack — confirmed via a real crash report showing
   `EXC_BAD_ACCESS`/"Thread stack size exceeded" directly inside
   `vte::base::SpawnContext::exec` → `fdwalk` → `__chkstk_darwin`.
   `main()` now calls `lower_fd_limit()` (caps `RLIMIT_NOFILE` to 4096)
   before touching GTK/VTE at all, which resolved this one.

2. **Not fixable here**: a separate, intermittent (~2-3% of launches)
   crash inside `libsystem_malloc`'s new "xzone" memory allocator during
   `posix_spawn()` of the `bin/z80-gtk` binary itself — happening before
   any of this project's own code, or even GTK/VTE's, runs at all. First
   suspected as a Homebrew GTK4/glib bottle vs. OS-version ABI mismatch,
   but that theory doesn't hold: the crash occurs at process-launch time,
   across several different internal call paths
   (`gtk_application_new`/`libintl_dcigettext`/`vte`'s own `g_strdupv`
   all crash inside the same allocator symbol,
   `_xzm_xzone_malloc_freelist_outlined`), and matches a bug Apple's own
   engineers have confirmed on their Developer Forums: [Sporadic crash in
   xzm_main_malloc_zone_init_range_groups when spawning large binaries
   (macOS 26.3.1)](https://developer.apple.com/forums/thread/821081) —
   same "~2-3% of spawns," same allocator, same "before the app's own
   `main()` runs" symptom, reported against large Mach-O binaries in
   general, not GTK/VTE specifically. `bin/z80-gtk` links a lot of large
   dylibs (GTK4, VTE, Pango, Cairo, HarfBuzz, GLib...), which plausibly
   makes it more likely to trip this than a small binary like plain
   `bin/z80` ever would. The standard `MallocNanoZone=0` mitigation
   doesn't help (it targets a different, older malloc-zone crash class).

Practical upshot: `bin/z80-gtk` now works when it launches, but still
occasionally fails to launch at all, for a reason outside this project's
control. If a launch crashes, just try again. No further action makes
sense here until Apple ships a macOS update fixing the allocator bug
linked above.
