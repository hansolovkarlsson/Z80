# Turbo Pascal (upstream)

`TURBO.COM`/`TURBO.MSG`/`TURBO.OVR` (the compiler/editor/IDE itself) and
`TINST.COM`/`TINST.DTA`/`TINST.MSG` (its terminal-configuration utility)
are Borland's real, unmodified Turbo Pascal 3.01A for CP/M-80 — a genuine
integrated environment (full-screen WordStar-key editor, single-pass
compiler, and compile-and-run, all in one program), not just a compiler.
Downloaded from
[retroarchive.org](http://www.retroarchive.org/cpm/lang/TP_301A.ZIP)
(`TP_301A.ZIP`) — real 1985 Borland binaries, confirmed by the literal
`Copyright (C) 1983,84,85 BORLAND Inc.` bytes at the start of both
`.COM` files. `READ.ME` is Borland's own errata sheet for the printed
manual. The ZIP also bundles a few sample `.pas` programs (a MasterMind-
style game split across `mc*.pas`/`mc-mod*.inc`, plus `lister.pas`/
`cmdlin.pas`) not otherwise used here.

**This particular copy ships pre-configured for a "Microbee VDU"**
(an Australian CP/M computer) per `READ.ME`'s own note ("your TURBO
PASCAL disk has been pre-installed for you for Microbee disk systems") —
not something a modern ANSI terminal understands. `../derive.sh`
reproduces the real fix: running `TINST.COM` (Turbo Pascal's own
terminal-configuration tool) through this project's emulator, selecting
its built-in "ANSI" profile (option 6 of 32 terminals it supports out of
the box), to rewrite `TURBO.COM`'s own terminal-control byte tables in
place — see `../derive.sh`'s own comment for the full story, including a
real emulator bug (`BDOS_ENTRY` in `emu/src/cpm.h`) that `TINST.COM`
itself surfaced.

**License note**: like `resources/sargon/`, `resources/ccp/`, and
`resources/adventure/`, this is commercial 1985 software with no open
license — kept here per the same private-repo policy as those.

See `docs/TURBOPASCAL_REFERENCE.md` for the editor's WordStar-style
keyboard commands and what the language adds beyond standard Wirth
Pascal (strings, typed constants, direct memory/port access, and a large
standard library) — sourced from Borland's own 1985 manual, not just
this README.
