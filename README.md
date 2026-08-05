# Z80

A Z80 CPU emulator written in C, built to run CP/M-80 programs. This
project also hosts an unrelated Game Boy emulator (`gameboy/`, see its
own `README.md`) — everything CP/M-related below lives under `cpm/`,
kept separate; only `bin/` (the shared build output for both) and
`scripts/` (general-purpose tooling used by both) stay at the repo root.

The core CPU emulator is complete: it implements the full documented and
undocumented Z80 instruction set (table-driven opcode dispatch, all
`0xCB`/`0xED`/`0xDD`/`0xFD`-prefixed forms, including the undocumented
`IXH`/`IXL`/`IYH`/`IYL` half-index registers) and passes the
[ZEXALL/ZEXDOC](cpm/emu/zexall/ZEXALL-main/) instruction exerciser cleanly:
67/67 tests OK, 0 errors, 0 unimplemented opcodes, on both the
documented-only (ZEXDOC) and documented+undocumented (ZEXALL) variants.

A Z80 assembler (`cpm/asm/`) is in progress: a two-pass assembler covering the
full non-prefixed/`CB`/`ED`/`DD`/`FD` instruction set (including the
undocumented half-index-register forms), `ORG`/`EQU`/`DB`/`DW`/`DS`
directives, expression evaluation, conditional assembly
(`IF`/`ELSE`/`ENDIF`), and `MACRO`/`ENDM`/`LOCAL`/`INCLUDE`. It's proven
against the real target: `bin/z80asm` assembles the actual, unmodified
`zexall.z80`/`zexdoc.z80` source with zero errors, and running the result
through `bin/z80` passes all 67 tests, same as the original
pre-built binaries.

A disassembler (`cpm/disasm/`) covers the same instruction set in reverse:
given a `.com`/binary, it prints a listing with auto-generated labels for
jump/call targets. It's a straightforward linear decoder (no code/data
separation yet — pointed at a data region, it'll decode those bytes as
instructions too), but the decoding itself is solid: verified against
`cpm/asm/examples/hello.asm`/`selftest.asm` (assemble → disassemble → matches
the source exactly) and spot-checked against the real `zexall.com`.

See [`cpm/docs/ROADMAP.md`](cpm/docs/ROADMAP.md) for the full picture: what's
next, what's done, and known gaps (currently just interrupt delivery —
I/O ports, `IM`, `RETI`/`RETN`, and `LD A,I`-family are implemented).

## Build & run

```
make               # builds bin/z80, bin/z80asm, and bin/z80dasm
make emulator      # just the emulator
make assembler     # just the assembler
make disassembler  # just the disassembler
make run           # build the emulator, then run it against zexall.com
make test          # build, then run the regression check (cpm/tests/run_tests.sh)
make clean         # remove build output
```

`source scripts/config.sh` (from the repo root) puts `bin/` on `PATH`
(and sets `$BASEDIR` to the repo root), so the three tools below can be
run as `z80`/`z80asm`/`z80dasm` from anywhere instead of needing a
relative `bin/...` prefix. Optional — the examples below use the
explicit relative path instead, run from inside `cpm/` (`bin/` itself
stays at the repo root, shared with the `gameboy/` subproject, so it's
reached as `../bin/z80` from there) since that's also where `bin/z80`'s
own CP/M disk-directory lookup (`cpm_disk/`, see below) expects to be
run from.

Correctness is verified by running the ZEXALL/ZEXDOC exerciser and reading
its console output: every opcode should report `OK`; a wrong flag or result
shows up as an `ERROR` line naming the instruction. `make test` automates
this (plus running every `cpm/asm/examples/*.asm` program) into a pass/fail
exit code. `bin/z80` takes the `.com` file to run as an argument
(`bin/z80 -h`/`--help`, or no arguments at all, prints usage instead of
running anything); `bin/z80 --ccp [ccp.com]` boots a CP/M shell instead
of a single program (see `cpm/cpm_disk/README.md`):

```
cd cpm
../bin/z80 emu/zexall/ZEXALL-main/zexdoc.com
```

The assembler takes a source file and writes a raw binary (an `ORG 100h`
source produces a CP/M `.com`-ready image):

```
../bin/z80asm asm/examples/hello.asm -o hello.com
../bin/z80 hello.com
```

The disassembler takes a binary and prints a listing (default load
address `0x100`, matching CP/M convention; `-o` overrides it, `-l` caps
how many bytes to decode):

```
../bin/z80dasm hello.com -l 0x34
```

## Project layout

Everything CP/M-related lives under `cpm/` — kept separate from the
unrelated `gameboy/` subproject (a standalone Game Boy emulator sharing
this repo, see its own `README.md`). Only `bin/` (the shared build
output for both) and `scripts/` (general-purpose tooling used by both)
stay at the repo root.

- `cpm/emu/src/` — the emulator itself (`z80.c`/`z80.h` opcode dispatch,
  `alu.c`/`alu.h` flag/arithmetic logic, `cpm.c`/`cpm.h` minimal CP/M BDOS
  emulation, `main.c` the CP/M-style program loader/run loop).
- `cpm/emu/zexall/ZEXALL-main/` — the ZEXALL/ZEXDOC instruction exerciser
  (third-party, GPLv2, by Frank D. Cringle via YAZE-AG — not this
  project's code, not meant to be edited).
- `cpm/asm/src/` — the assembler (`symtab`, `expr`, `encode`, `assemble`,
  `preprocess`, `main`); `cpm/asm/examples/` has example `.asm` programs, all
  wired into `make test` (`cpm/tests/run_tests.sh`) as automated regression
  checks. `cpm/asm/test/` is different: manual/interactive programs (e.g. a
  console-I/O test that needs a real keyboard) meant to be run and
  eyeballed by hand, not part of the automated suite.
- `cpm/resources/tastybasic/`, `cpm/resources/sargon/`, `cpm/resources/adventure/`,
  `cpm/resources/ccp/`, `cpm/resources/turbopascal/` — real, third-party CP/M
  programs (Tasty Basic, SARGON chess, Colossal Cave Adventure, Digital
  Research's own CCP shell, and Borland's Turbo Pascal 3.01A) tried
  against the emulator by hand for real-world validation (not part of
  `make test`, same spirit as `cpm/asm/test/`) — see `cpm/docs/ROADMAP.md`'s
  "Real-world validation"/CCP entries for what that's found and fixed.
  Where source is available (`tastybasic/`, `sargon/`, `ccp/`),
  `upstream/` holds it unmodified and `derive.sh` documents/reproduces
  the translation needed to get it building with `z80asm` (a straight
  syntax patch for `tastybasic/`/`sargon/`; a full 8080→Z80 mnemonic
  translation for `ccp/`, since CP/M predates the Z80 — see
  `scripts/8080_to_z80.py`, general-purpose enough to reuse for other
  8080-mnemonic CP/M-era source); `adventure/` and `cpm/resources/Mbasic.com`
  are prebuilt binaries with no available source. `turbopascal/` is also
  a prebuilt binary, but still has its own `derive.sh` — not translating
  source, but reproducibly *patching* `TURBO.COM`'s own terminal-control
  tables (via its own `TINST.COM` utility, run through this project's
  emulator) from the "Microbee VDU" profile it ships with to a real ANSI
  one.
- `cpm/cpm_disk/` — every program above (and `cpm/asm/examples/`), pre-built
  and ready to run: `../bin/z80 cpm_disk/<name>.com` from inside `cpm/`, or
  `../bin/z80 --ccp cpm_disk/ccp.com` to boot a real CP/M shell (`A>` prompt,
  `DIR`/`TYPE`/etc., run any program above by name) instead of a single
  program. See `cpm/cpm_disk/README.md`.
- `cpm/disasm/src/` — the disassembler (`decode`, `main`); `cpm/disasm/examples/`
  has example output.
- `cpm/docs/` — the CP/M-side project roadmap
  ([`ROADMAP.md`](cpm/docs/ROADMAP.md)), a Z80 CPU reference including
  undocumented opcodes ([`Z80_REFERENCE.md`](cpm/docs/Z80_REFERENCE.md)),
  the assembler syntax reference ([`ASSEMBLER.md`](cpm/docs/ASSEMBLER.md)),
  a CP/M 2.2 BDOS/BIOS reference ([`CPM_REFERENCE.md`](cpm/docs/CPM_REFERENCE.md))
  for the Phase 3 work, and references for the real CP/M software this
  emulator's been validated against: the CCP shell
  ([`CCP_REFERENCE.md`](cpm/docs/CCP_REFERENCE.md)), BDS C
  ([`BDSC_REFERENCE.md`](cpm/docs/BDSC_REFERENCE.md)), Tasty Basic
  ([`TASTYBASIC_REFERENCE.md`](cpm/docs/TASTYBASIC_REFERENCE.md)), MBASIC
  ([`MBASIC_REFERENCE.md`](cpm/docs/MBASIC_REFERENCE.md)), and Turbo Pascal
  ([`TURBOPASCAL_REFERENCE.md`](cpm/docs/TURBOPASCAL_REFERENCE.md)). The
  top-level `docs/` (repo root) holds only `GAMEBOY_ROADMAP.md`, the
  unrelated Game Boy subproject's own roadmap.
- `cpm/gtk/` — an opt-in (`make gtk`) thin GTK4 launcher for `bin/z80`,
  attached to a pty with a `VteTerminal` doing the real terminal
  interpretation. See `cpm/gtk/README.md`.
- `scripts/` — standalone reusable tooling that isn't part of the
  emulator/assembler/disassembler build itself, shared by both `cpm/`
  and `gameboy/`: `config.sh` (`PATH` setup, see above) and
  `8080_to_z80.py` (the general 8080→Z80 mnemonic translator, see
  above). See `scripts/README.md`.
- `cpm/resources/` — reference links/documents on the Z80, CP/M, etc. (not
  code, just reading material), alongside the third-party program
  directories itemized above.

See [`CLAUDE.md`](CLAUDE.md) for a deeper architecture writeup (opcode
dispatch design, prefix-handling internals, and non-obvious hardware
quirks the emulator has to replicate).

## License

This project's own code has no license file yet. The bundled ZEXALL/ZEXDOC
exerciser under `cpm/emu/zexall/ZEXALL-main/` is third-party and licensed under
the GPLv2 by its original author — see the `LICENSE` file in that
directory.
