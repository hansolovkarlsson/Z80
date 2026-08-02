# Z80

A Z80 CPU emulator written in C, built to run CP/M-80 programs.

The core CPU emulator is complete: it implements the full documented and
undocumented Z80 instruction set (table-driven opcode dispatch, all
`0xCB`/`0xED`/`0xDD`/`0xFD`-prefixed forms, including the undocumented
`IXH`/`IXL`/`IYH`/`IYL` half-index registers) and passes the
[ZEXALL/ZEXDOC](emu/zexall/ZEXALL-main/) instruction exerciser cleanly:
67/67 tests OK, 0 errors, 0 unimplemented opcodes, on both the
documented-only (ZEXDOC) and documented+undocumented (ZEXALL) variants.

A Z80 assembler (`asm/`) is in progress: a two-pass assembler covering the
full non-prefixed/`CB`/`ED`/`DD`/`FD` instruction set (including the
undocumented half-index-register forms), `ORG`/`EQU`/`DB`/`DW`/`DS`
directives, expression evaluation, conditional assembly
(`IF`/`ELSE`/`ENDIF`), and `MACRO`/`ENDM`/`LOCAL`/`INCLUDE`. It's proven
against the real target: `bin/z80asm` assembles the actual, unmodified
`zexall.z80`/`zexdoc.z80` source with zero errors, and running the result
through `bin/z80_emulator` passes all 67 tests, same as the original
pre-built binaries. See [`docs/ROADMAP.md`](docs/ROADMAP.md) for the full
picture: what's next, what's done, and known gaps (I/O ports, interrupts).

## Build & run

```
make             # builds both bin/z80_emulator and bin/z80asm
make emulator    # just the emulator
make assembler   # just the assembler
make run         # build the emulator, then run it against zexall.com
make clean       # remove build output
```

Correctness is verified by running the ZEXALL/ZEXDOC exerciser and reading
its console output: every opcode should report `OK`; a wrong flag or result
shows up as an `ERROR` line naming the instruction. `bin/z80_emulator`
defaults to running `emu/zexall/ZEXALL-main/zexall.com`; pass a different
`.com` file as an argument to run something else:

```
./bin/z80_emulator emu/zexall/ZEXALL-main/zexdoc.com
```

The assembler takes a source file and writes a raw binary (an `ORG 100h`
source produces a CP/M `.com`-ready image):

```
./bin/z80asm asm/examples/hello.asm -o hello.com
./bin/z80_emulator hello.com
```

## Project layout

- `emu/src/` — the emulator itself (`z80.c`/`z80.h` opcode dispatch,
  `alu.c`/`alu.h` flag/arithmetic logic, `cpm.c`/`cpm.h` minimal CP/M BDOS
  emulation, `main.c` the CP/M-style program loader/run loop).
- `emu/zexall/ZEXALL-main/` — the ZEXALL/ZEXDOC instruction exerciser
  (third-party, GPLv2, by Frank D. Cringle via YAZE-AG — not this
  project's code, not meant to be edited).
- `asm/src/` — the assembler (`symtab`, `expr`, `encode`, `assemble`,
  `preprocess`, `main`); `asm/examples/` has example `.asm` programs.
- `docs/` — the project roadmap ([`ROADMAP.md`](docs/ROADMAP.md)), a Z80
  CPU reference including undocumented opcodes
  ([`Z80_REFERENCE.md`](docs/Z80_REFERENCE.md)), and the assembler syntax
  reference ([`ASSEMBLER.md`](docs/ASSEMBLER.md)).
- `resources/` — reference links/documents on the Z80, CP/M, etc. (not
  code, just reading material).

See [`CLAUDE.md`](CLAUDE.md) for a deeper architecture writeup (opcode
dispatch design, prefix-handling internals, and non-obvious hardware
quirks the emulator has to replicate).

## License

This project's own code has no license file yet. The bundled ZEXALL/ZEXDOC
exerciser under `emu/zexall/ZEXALL-main/` is third-party and licensed under
the GPLv2 by its original author — see the `LICENSE` file in that
directory.
