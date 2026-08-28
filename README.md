# Z80

A Z80 CPU emulator written in C, built to run CP/M-80 programs.
CP/M-specific code lives under `cpm/`; the CPU core (`z80core/`), the
shared ABC-bus peripherals (`abcbus/`), the assembler and disassembler
(`asm/`, `disasm/`), their generic docs (`docs/`), `bin/` (the build
output), and `scripts/` (general-purpose tooling) are genuinely
CP/M-independent and stay at the repo root.
(This repo previously also hosted a standalone Game Boy emulator as a
separate, code-sharing-free subproject under `gameboy/` — it's since
been split out into its own repository via `git subtree split`,
preserving its real commit history, once it became clear the two
projects would never actually share code.)

The core CPU emulator is complete: it implements the full documented and
undocumented Z80 instruction set (table-driven opcode dispatch, all
`0xCB`/`0xED`/`0xDD`/`0xFD`-prefixed forms, including the undocumented
`IXH`/`IXL`/`IYH`/`IYL` half-index registers) and passes the
[ZEXALL/ZEXDOC](cpm/emu/zexall/ZEXALL-main/) instruction exerciser cleanly:
67/67 tests OK, 0 errors, 0 unimplemented opcodes, on both the
documented-only (ZEXDOC) and documented+undocumented (ZEXALL) variants.

A Z80 assembler (`asm/`) is in progress: a two-pass assembler covering the
full non-prefixed/`CB`/`ED`/`DD`/`FD` instruction set (including the
undocumented half-index-register forms), `ORG`/`EQU`/`DB`/`DW`/`DS`
directives, expression evaluation, conditional assembly
(`IF`/`ELSE`/`ENDIF`), and `MACRO`/`ENDM`/`LOCAL`/`INCLUDE`. It's proven
against the real target: `bin/z80asm` assembles the actual, unmodified
`zexall.z80`/`zexdoc.z80` source with zero errors, and running the result
through `bin/z80` passes all 67 tests, same as the original
pre-built binaries.

A disassembler (`disasm/`) covers the same instruction set in reverse:
given a `.com`/binary, it prints a listing with auto-generated labels for
jump/call targets. It does real code/data separation: a worklist-driven
recursive traversal follows jump/call/`RST` targets from the entry point
rather than decoding straight through, so embedded strings and tables come
out as labeled `DB` bytes instead of being mis-decoded as instructions.
Verified against `asm/examples/hello.asm`/`selftest.asm` (assemble →
disassemble → matches the source exactly) and spot-checked against the real
`zexall.com`. The one standard limitation: code reachable *only* via an
indirect/computed jump (`JP (HL)`/`(IX)`/`(IY)`) has no static target to
follow, so it comes out as data unless something also reaches it directly.

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
stays at the repo root, reached as `../bin/z80` from there) since
that's also where `bin/z80`'s own CP/M disk-directory lookup
(`cpm_disk/`, see below) expects to be run from.

Correctness is verified by running the ZEXALL/ZEXDOC exerciser and reading
its console output: every opcode should report `OK`; a wrong flag or result
shows up as an `ERROR` line naming the instruction. `make test` automates
this (plus running every `asm/examples/*.asm` program) into a pass/fail
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
../bin/z80asm ../asm/examples/hello.asm -o hello.com
../bin/z80 hello.com
```

The disassembler takes a binary and prints a listing (default load
address `0x100`, matching CP/M convention; `-o` overrides it, `-l` caps
how many bytes to decode):

```
../bin/z80dasm hello.com -l 0x34
```

## Project layout

CP/M-specific code lives under `cpm/`; `z80core/`, `abcbus/`, `asm/`,
`disasm/`, `docs/`, `bin/` (the build output), and `scripts/`
(general-purpose tooling) stay at the repo root since none of them are
CP/M-specific — see [`CLAUDE.md`](CLAUDE.md) for the full reasoning.

- `z80core/` — the shared Z80 CPU core (`z80.c`/`z80.h` opcode dispatch,
  `alu.c`/`alu.h` flag/arithmetic logic) — machine-agnostic, used by both
  `bin/z80` (CP/M), `bin/abc80`, and `bin/abc802`.
- `abcbus/` — the synthetic ABC-bus floppy controller (`disk.c`/`disk.h`),
  shared by `bin/abc80` and `bin/abc802`. At the repo root for the same
  reason `z80core/` is: the ABC bus is a bus, not a machine, and both
  targets drive the same card with the same command header and status
  bits. Each machine keeps its own port decode and DOS ROM loading.
- `cpm/emu/src/` — the emulator itself (`z80.c`/`z80.h` opcode dispatch,
  `alu.c`/`alu.h` flag/arithmetic logic, `cpm.c`/`cpm.h` minimal CP/M BDOS
  emulation, `main.c` the CP/M-style program loader/run loop).
- `cpm/emu/zexall/ZEXALL-main/` — the ZEXALL/ZEXDOC instruction exerciser
  (third-party, GPLv2, by Frank D. Cringle via YAZE-AG — not this
  project's code, not meant to be edited).
- `asm/src/` — the assembler (`symtab`, `expr`, `encode`, `assemble`,
  `preprocess`, `main`); `asm/examples/` has example `.asm` programs, all
  wired into `make test` (`cpm/tests/run_tests.sh`) as automated regression
  checks. `asm/test/` is different: manual/interactive programs (e.g. a
  console-I/O test that needs a real keyboard) meant to be run and
  eyeballed by hand, not part of the automated suite.
- `cpm/resources/tastybasic/`, `cpm/resources/sargon/`, `cpm/resources/adventure/`,
  `cpm/resources/ccp/`, `cpm/resources/turbopascal/` — real, third-party CP/M
  programs (Tasty Basic, SARGON chess, Colossal Cave Adventure, Digital
  Research's own CCP shell, and Borland's Turbo Pascal 3.01A) tried
  against the emulator by hand for real-world validation (not part of
  `make test`, same spirit as `asm/test/`) — see `cpm/docs/ROADMAP.md`'s
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
- `cpm/cpm_disk/` — every program above (and `asm/examples/`), pre-built
  and ready to run: `../bin/z80 cpm_disk/<name>.com` from inside `cpm/`, or
  `../bin/z80 --ccp cpm_disk/ccp.com` to boot a real CP/M shell (`A>` prompt,
  `DIR`/`TYPE`/etc., run any program above by name) instead of a single
  program. See `cpm/cpm_disk/README.md`.
- `disasm/src/` — the disassembler (`decode`, `main`); `disasm/examples/`
  has example output.
- `docs/` — generic, non-CP/M-specific reference docs: a Z80 CPU
  reference including undocumented opcodes
  ([`Z80_REFERENCE.md`](docs/Z80_REFERENCE.md)) and the assembler syntax
  reference ([`ASSEMBLER.md`](docs/ASSEMBLER.md)). Also the project's
  cross-cutting history: [`JOURNAL.md`](docs/JOURNAL.md), a running log of
  what was worked on and what it taught, and
  [`postmortems/`](docs/postmortems/), write-ups of the failures whose
  lesson outlived the fix.
- `cpm/docs/` — the CP/M-side project roadmap
  ([`ROADMAP.md`](cpm/docs/ROADMAP.md)) covering what is left, with the
  finished phases in [`COMPLETED.md`](cpm/docs/COMPLETED.md); a CP/M 2.2
  BDOS/BIOS reference
  ([`CPM_REFERENCE.md`](cpm/docs/CPM_REFERENCE.md)) for the Phase 3 work,
  and references for the real CP/M software this emulator's been
  validated against: the CCP shell
  ([`CCP_REFERENCE.md`](cpm/docs/CCP_REFERENCE.md)), BDS C
  ([`BDSC_REFERENCE.md`](cpm/docs/BDSC_REFERENCE.md)), Tasty Basic
  ([`TASTYBASIC_REFERENCE.md`](cpm/docs/TASTYBASIC_REFERENCE.md)), MBASIC
  ([`MBASIC_REFERENCE.md`](cpm/docs/MBASIC_REFERENCE.md)), and Turbo Pascal
  ([`TURBOPASCAL_REFERENCE.md`](cpm/docs/TURBOPASCAL_REFERENCE.md)).
- `abc80/` — the Luxor ABC80 machine target (`make abc80`): boots the real
  1978 BASIC ROM with video, keyboard, cassette, sound, a periodic PIO
  interrupt, and floppy support, plus an opt-in GTK4 front-end
  (`make abc80-gtk`). See
  [`ABC80_ROADMAP.md`](abc80/docs/ABC80_ROADMAP.md) for what is left and
  [`ABC80_COMPLETED.md`](abc80/docs/ABC80_COMPLETED.md) for the finished
  milestones.
- `abc802/` — the Luxor ABC802 machine target (`make abc802`): boots the
  real 1983 BASIC II ROM — MC6845 CRTC, Z80 CTC/DART/SIO on an IM 2 daisy
  chain, the M1-decoded character-RAM overlay, and a serial keyboard —
  with a live `--interactive` session, real pixel rendering from the
  character ROM (`--screenshot`), ABC-bus floppy support booting real
  1980s disk images in both 160K and 640K formats (`--disk`), and an
  opt-in GTK4 front-end
  (`make abc802-gtk`). See
  [`ABC802_ROADMAP.md`](abc802/docs/ABC802_ROADMAP.md) for what is left,
  [`ABC802_COMPLETED.md`](abc802/docs/ABC802_COMPLETED.md) for the
  finished milestones,
  [`ABC802_FLOPPY_SCOPING.md`](abc802/docs/ABC802_FLOPPY_SCOPING.md) for
  the costed options on disk support, and
  [`ABC802_REFERENCE.md`](abc802/docs/ABC802_REFERENCE.md) for the
  hardware.
- `cpm/gtk/` — an opt-in (`make gtk`) thin GTK4 launcher for `bin/z80`,
  attached to a pty with a `VteTerminal` doing the real terminal
  interpretation. See `cpm/gtk/README.md`.
- `scripts/` — standalone reusable tooling that isn't part of the
  emulator/assembler/disassembler build itself: `config.sh` (`PATH`
  setup, see above) and `8080_to_z80.py` (the general 8080→Z80 mnemonic
  translator, see above). See `scripts/README.md`.
- `cpm/resources/` — reference links/documents on the Z80, CP/M, etc. (not
  code, just reading material), alongside the third-party program
  directories itemized above.

See [`CLAUDE.md`](CLAUDE.md) for a deeper architecture writeup (opcode
dispatch design, prefix-handling internals, and non-obvious hardware
quirks the emulator has to replicate).

## License

This project's own code — the emulator, assembler, disassembler, GTK
front-ends, and their tests/docs — is MIT licensed; see [`LICENSE`](LICENSE).

That covers only what's written here. The third-party material bundled for
testing and reference keeps its own terms, which are not MIT and in several
cases are more restrictive:

- `cpm/emu/zexall/ZEXALL-main/` — the ZEXALL/ZEXDOC exerciser, GPLv2 by its
  original author (Frank D. Cringle). See the `LICENSE` file in that
  directory.
- `cpm/resources/hanoi/upstream/`, `cpm/resources/queens/upstream/` — GPLv2;
  `cpm/resources/tastybasic/upstream/`, `cpm/resources/tastybasic-main/` —
  GPLv3. Each carries its own `LICENSE` file.
- `cpm/resources/bdsc/` — the BDS C compiler, released to the public domain
  by Leor Zolman. (The RED editor from the same distribution is separately
  copyrighted and its source is deliberately *not* included here.)
- The remaining commercial CP/M binaries bundled under `cpm/cpm_disk/` and
  `cpm/resources/` (dBASE II, Turbo Pascal, MBASIC, SARGON) and the ABC80
  ROM images under `abc80/resources/rom/` and `abc802/resources/rom/` are copyrighted by their
  respective owners, included here only as the real software this emulator
  is validated against. No license to redistribute them is claimed or
  granted.
