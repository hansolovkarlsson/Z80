# Roadmap

The project plan, kept current as work progresses. Phase order is a
dependency chain, not a rigid schedule — each phase needs the previous one
working.

## Completed work

Phases 1-3 are done. Their full write-ups — including the real-software
investigations that drove most of the BDOS/BIOS work — live in
[`COMPLETED.md`](COMPLETED.md).

| # | Phase | Outcome |
|---|---|---|
| 1 | Z80 core emulator | ZEXALL and ZEXDOC both pass cleanly, 67/67 OK, plus I/O ports and interrupt delivery |
| 2 | Assembler (and disassembler) | `bin/z80asm` assembles the real unmodified `zexall.mac`/`zexdoc.mac`; `bin/z80dasm` does recursive-traversal decoding |
| 3 | CP/M BDOS/BIOS | real CP/M software runs: Turbo Pascal, WordStar, dBASE II, MBASIC, BDS C, and a real CCP shell |

## Phase 4: Beyond CP/M (exploratory)

Aspirational, not yet scoped:

- **`make test` builds none of the GTK apps**, this one included, so a
  build break in any of them goes unnoticed until someone compiles by
  hand — which is how `bin/abc80-gtk` was found broken on 2026-08-31.
  The three ABC windows at least have headless checks that run when their
  binary exists; this one has none at all. Tracked as a planned next step
  in [`../../abc802/docs/ABC802_ROADMAP.md`](../../abc802/docs/ABC802_ROADMAP.md).

- **A GTK-based UI, in progress but currently blocked** (`gtk/`) — a real
  standalone app, not depending on the host terminal, useful for handing
  someone a double-clickable program rather than a CLI incantation. No
  longer motivated by a VT100 gap the way an earlier version of this
  entry claimed (the bare stdout passthrough already lets any real host
  terminal correctly interpret cursor-positioning/color escape codes on
  its own - see the corrected SARGON entry in `COMPLETED.md`). Architecture decided
  and implemented: `cpm/gtk/src/main.c` is a *thin launcher*, not a terminal
  emulator of its own — it spawns the real, completely unmodified
  `bin/z80` attached to a pty and hands that pty to a `VteTerminal`
  widget (the same widget GNOME Terminal uses), so `z80.c`/`cpm.c`/
  `cpm/emu/src/main.c` needed zero changes. Kept as a separate opt-in binary
  (`bin/z80-gtk`, built via `make gtk`, never part of `make`/`make
  test`) specifically so the default build stays free of the GTK4+VTE
  dependency. **Working, but still intermittently blocked**: terminal
  rendering itself is now confirmed correct (real `bin/z80` output shows
  up in the `VteTerminal` widget). One real crash cause found and fixed
  in `main.c` (`lower_fd_limit()`, working around a `fdwalk()` stack
  overflow at this shell's very high default file-descriptor limit). A
  second, separate crash - inside `libsystem_malloc`'s new "xzone"
  allocator during `posix_spawn()` of the large `bin/z80-gtk` binary,
  before any of this project's own code runs - turned out not to be a
  Homebrew bottle mismatch as first suspected, but a confirmed macOS 26
  OS bug matching Apple's own Developer Forums report (~2-3% of process
  launches, same allocator, same symptom - see `cpm/gtk/README.md` for the
  citation). Not fixable from application code; just retry the launch
  when it happens, and revisit if/when Apple ships a fix.
- **A Game Boy emulator - built, and since split into its own repo.**
  Developed for a while as a subproject here under `gameboy/` (a
  standalone SM83 core - superseded the earlier "extract a shared
  `core/`" idea sketched here, since the Z80 and SM83 diverge too much
  at the dispatch/ALU level for real sharing to have been worth it), it
  grew a real front end, real-game validation, and save states before
  being split out into its own GitHub repository via `git subtree
  split` (preserving its real commit history) once it was clear the two
  projects would never actually share code. See that project's own
  `docs/GAMEBOY_ROADMAP.md` (in its new repo) for the full phase plan
  and status if relevant here.
- A custom ROM/OS on top of it — open design questions include a stack VM
  and whether Logo-style prefix notation could combine with a stack machine
  model.

## Known gaps / not modeled

Only what is *absent* belongs here. The five items that used to sit in
this section as checked-off boxes — I/O ports, `RETI`/`RETN`/`LD A,I` and
friends, `IM 0`/`1`/`2`, interrupt delivery, and the automated regression
check — are all done, and their write-ups have moved to
[`COMPLETED.md`](COMPLETED.md) where the other finished work lives.

- **Flat memory model**: `z80_read_byte`/`z80_write_byte` index straight
  into a 64KB array with no bank switching. Fine for CP/M's 64KB TPA;
  revisit only if a later phase needs more than that. (The machine targets
  needing more than a flat array is what the core's `bus_read_hook`/
  `bus_write_hook` exist for — see `CLAUDE.md` — so this is a statement
  about the CP/M target, not the core.)
- **No host-side interrupt source**: the delivery mechanism is complete
  and tested, but nothing in the CP/M target raises an interrupt. No
  timer or keyboard chip is modeled, and no CP/M-executable instruction
  can raise one against itself, which is why
  `cpm/tests/test_interrupts.c` drives it at the C level instead. The
  ABC80 and ABC802 targets do raise real interrupts.
- **A non-`RST` `IM 0` vector** returns `-1` (the "unimplemented" signal),
  deliberately: dispatching a general instruction fetched from the device
  rather than from memory would need a bus model this does not have. Real
  `IM 0` hardware overwhelmingly uses `RST`, which is supported.

## Testing

`make test` now runs three suites — `make test-cpm` is this one,
[`cpm/tests/run_tests.sh`](../tests/run_tests.sh), and the machine
targets have their own (`make test-abc80`, `make test-abc802`; see
`CLAUDE.md`). This suite is unchanged: ZEXALL and ZEXDOC, every
`asm/examples/*.asm` program, the `z80dasm` fixture diff, and
`bin/z80-test-interrupts`'s 27 C-level interrupt checks.

It keeps its own helper functions rather than sharing
`scripts/testlib.sh` with the machine targets. That is deliberate: its
checks are shaped around `.com` programs that self-report `OK n`/`FAIL n`,
which is a different problem from asserting on a rendered screen, and
retrofitting a working suite would buy nothing.
