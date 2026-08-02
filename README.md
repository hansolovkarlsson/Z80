# Z80

A Z80 CPU emulator written in C, built to run CP/M-80 programs.

The core CPU emulator is complete: it implements the full documented and
undocumented Z80 instruction set (table-driven opcode dispatch, all
`0xCB`/`0xED`/`0xDD`/`0xFD`-prefixed forms, including the undocumented
`IXH`/`IXL`/`IYH`/`IYL` half-index registers) and passes the
[ZEXALL/ZEXDOC](zexall/ZEXALL-main/) instruction exerciser cleanly: 67/67
tests OK, 0 errors, 0 unimplemented opcodes, on both the documented-only
(ZEXDOC) and documented+undocumented (ZEXALL) variants.

See [`docs/ROADMAP.md`](docs/ROADMAP.md) for what's next (an assembler,
then a full CP/M BDOS/BIOS) and known gaps (I/O ports, interrupts).

## Build & run

```
make         # builds src/*.c into bin/z80_emulator
make run     # build, then run it against zexall.com
make clean   # remove build output
```

Correctness is verified by running the ZEXALL/ZEXDOC exerciser and reading
its console output: every opcode should report `OK`; a wrong flag or result
shows up as an `ERROR` line naming the instruction. `bin/z80_emulator`
defaults to running `zexall/ZEXALL-main/zexall.com`; pass a different `.com`
file as an argument to run something else:

```
./bin/z80_emulator zexall/ZEXALL-main/zexdoc.com
```

## Project layout

- `src/` — the emulator itself (`z80.c`/`z80.h` opcode dispatch, `alu.c`/
  `alu.h` flag/arithmetic logic, `cpm.c`/`cpm.h` minimal CP/M BDOS
  emulation, `main.c` the CP/M-style program loader/run loop).
- `zexall/ZEXALL-main/` — the ZEXALL/ZEXDOC instruction exerciser
  (third-party, GPLv2, by Frank D. Cringle via YAZE-AG — not this
  project's code, not meant to be edited).
- `docs/` — planning notes and the project roadmap.

See [`CLAUDE.md`](CLAUDE.md) for a deeper architecture writeup (opcode
dispatch design, prefix-handling internals, and non-obvious hardware
quirks the emulator has to replicate).

## License

This project's own code has no license file yet. The bundled ZEXALL/ZEXDOC
exerciser under `zexall/ZEXALL-main/` is third-party and licensed under the
GPLv2 by its original author — see the `LICENSE` file in that directory.
