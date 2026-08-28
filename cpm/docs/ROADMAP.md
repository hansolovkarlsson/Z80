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

## Known gaps / near-term technical debt

Not blocking Phase 1's ZEXALL/ZEXDOC goal (the exerciser doesn't exercise
any of these):

- [x] **I/O ports**: `IN r,(n)`/`IN r,(C)`/`OUT (n),A`/`OUT (C),r` are now
  implemented (`z80core/z80.c`'s `z80_op_prefix_ed`, plus `0xD3`/`0xDB` in
  `main_opcode_table`), backed by a real `cpu->io_ports[256]` array
  (`z80_io_in`/`z80_io_out` in `z80.c`/`z80.h`) — no actual devices are
  attached, but `IN` now reads back whatever the last `OUT` to that port
  wrote instead of being a silent no-op, which is enough for round-trip
  correctness. The undocumented `IN (C)` (flags-only, discards the result)
  and `OUT (C),0` forms are also handled.
- [x] **`RETI`/`RETN`/`LD A,I`/`LD A,R`/`LD I,A`/`LD R,A`**: implemented in
  `z80_op_prefix_ed`, including the undocumented `RETN` duplicate encodings
  (`0x55`/`0x5D`/`0x65`/`0x6D`/`0x75`/`0x7D`). `RETN` restores
  `iff1 := iff2`; `LD A,I`/`LD A,R` set `P/V` from `iff2` (S/Z/X/Y from the
  result, H/N cleared, C unaffected) — matches documented Z80 behavior, but
  note the real hardware has a race condition where `P/V` can read wrong if
  an interrupt lands during the instruction; not modeled here since there's
  no interrupt delivery yet (see below).
- [x] **`IM 0`/`1`/`2`**: implemented (`cpu->im` is now set), including the
  undocumented duplicate encodings. No interrupt-delivery mechanism
  consumes `cpu->im`/`iff1`/`iff2` yet — see below.
- [x] **Interrupt delivery**: implemented (`z80_step()`/`z80_service_int()`/
  `z80_service_nmi()` in `z80.c`), grounded against the Zilog Z80 CPU User
  Manual (UM008011-0816)'s "Interrupt Response" section rather than
  guessed. `z80_request_int(cpu, data)`/`z80_request_nmi(cpu)`/
  `z80_clear_int(cpu)` are the host-side API a real device would call
  (mirroring asserting an actual `INT`/`NMI` line) — sampled at
  instruction boundaries, gated correctly by `iff1` (`INT` only; `NMI` is
  genuinely non-maskable) and by `ei_delay` (a new `Z80` field: the
  documented one-instruction delay after `EI`, extended here to `NMI` too
  since the manual only states it for `INT` but the internal sample-inhibit
  circuit isn't gated by `IFF` at all — every serious independent
  reference/emulator agrees). `IM 1` (13 T-states) and `IM 2` (19
  T-states, vector-table lookup with the device byte's low bit correctly
  forced to 0) match the manual's own explicit numbers exactly; `NMI` (11
  T-states, fixes `PC` to `0x0066`, clears only `IFF1`) is the
  well-established number every reference converges on where the manual
  itself only describes the mechanism, not a stated total. `IM 0` supports
  only a single-byte `RST` device vector — the real-world norm the manual
  itself calls out ("often this response is a restart instruction") and
  the only case safely dispatchable without a full "instruction fetched
  from the device, not memory" bus model (several opcode handlers,
  `z80_op_rst_dispatch` included, re-derive their own opcode via
  `z80_read_byte(cpu, cpu->pc - 1)` rather than being passed it directly,
  which would silently read the wrong byte for a generic dispatch); a
  non-`RST` `IM 0` vector is a documented, deliberate gap (returns `-1`,
  the same "unimplemented" signal a genuinely unrecognized opcode gives),
  not a guess. `HALT` needed no new code at all — its existing "just
  decrement `PC` back to itself and keep re-fetching" implementation
  already breaks out correctly the moment an accepted interrupt
  overwrites `PC`, matching the manual's own description ("the CPU
  functions as if it had recycled a restart instruction" from the halt
  address). No host-side interrupt-raising device exists yet (no
  timer/keyboard chip is modeled) — `cpm/tests/test_interrupts.c` (see
  below) is presently this mechanism's only caller, by direct C-level
  unit test rather than a real device or CP/M program, since no
  CP/M-executable instruction can raise an interrupt against itself.
- [x] **Automated regression check**: `make test` (`cpm/tests/run_tests.sh`)
  runs ZEXALL/ZEXDOC and fails if the output contains `ERROR`, an
  `Unimplemented opcode` line, or doesn't reach `Tests complete`; it also
  assembles and runs every `asm/examples/*.asm` program and fails on any
  `FAIL` line (the `OK n`/`FAIL n` convention `selftest.asm`/
  `gaps_test.asm` use) or unimplemented-opcode hit. `asm/examples/
  gaps_test.asm` specifically covers the I/O-port and `RETI`/`RETN`/
  `LD A,I`-family additions above, since ZEXALL doesn't exercise any of
  them. `cpm/tests/test_interrupts.c` (`bin/z80-test-interrupts`, also
  wired into `run_tests.sh`) is the direct C-level equivalent for
  interrupt delivery specifically — the one regression check in this
  project that isn't a `.asm` program, for the reason given above: 27
  checks covering `IM 0`/`1`/`2` timing/vectoring, `NMI` vs. `INT`
  masking and priority, `IFF1`/`IFF2` semantics (including `RETN`'s
  restore), the `EI`-delay window, `HALT` interaction, and
  `z80_clear_int()`.
- **Flat memory model**: `z80_read_byte`/`z80_write_byte` index straight
  into a 64KB array with no bank switching. Fine for CP/M's 64KB TPA;
  revisit only if a later phase needs more than that.
