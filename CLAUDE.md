# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A Z80 CPU emulator written in C, built to run CP/M-80 programs. The current
milestone (per `notes.txt` / `ideas.txt`) is passing the ZEXALL/ZEXDOC
instruction exerciser; later phases plan an assembler and a full CP/M BDOS.
This directory is a git repository (initialized after the first working
ZEXALL/ZEXDOC pass).

## Build & Run

Source lives in `src/`; the Makefile builds it into `bin/z80_emulator`.

```
make         # gcc -Wall -Wextra -O2, compiles src/*.c into bin/z80_emulator
make run     # build, then ./bin/z80_emulator | less (runs zexall.com by default)
make clean   # remove object files and the binary
```

There is no test framework — correctness is verified by running the ZEXALL
exerciser and reading its console output for per-opcode `ERROR` reports vs.
`OK` lines. ZEXALL/ZEXDOC (`zexall/ZEXALL-main/`) are third-party,
downloaded pre-built CP/M test binaries (by Frank D. Cringle, via YAZE-AG,
GPLv2) — not code belonging to this project, and not meant to be edited.
They're the correctness oracle: if the emulator is right, every opcode
reports "OK"; a wrong flag or result shows up as an "ERROR" line naming the
instruction. As of the last full run, both `zexall.com` and `zexdoc.com`
pass cleanly (67/67 OK, 0 errors, 0 unimplemented opcodes). To run a
specific CP/M `.com` file instead of the default `zexall.com`, pass it as
argv[1] (paths are resolved relative to the working directory you invoke
the binary from, typically the repo root):

```
./bin/z80_emulator zexdoc.com
```

(`zexdoc.com`/`zexall.com` live in `zexall/ZEXALL-main/`; a copy of
`zexall.com` is also kept at the repo root.) The zexdoc variant checks only
documented flag behavior; zexall also checks the undocumented flags (bits 3
and 5, `FLAG_X`/`FLAG_Y`).

## Architecture

**Table-driven dispatch.** `z80_init_tables()` (in `z80.c`) populates
`main_opcode_table[256]` (a `Z80OpcodeHandler` array) mapping each opcode byte
to a handler function. `z80_step()` intercepts CP/M BDOS calls, fetches one
opcode byte, bumps the R register, and calls `main_opcode_table[opcode](cpu, ram)`.
Handlers return the T-state cycle count for the instruction (or a negative
value on an unimplemented/fatal opcode, which halts `main.c`'s run loop).

Several opcode ranges are handled generically instead of one handler per
opcode, decoding register indices out of the opcode byte itself:
- `0x40`–`0x7F` → `z80_op_ld_r_r` (register-to-register loads; `0x76`/HALT is
  a special case within this range).
- `0x80`–`0xBF` → `z80_op_alu_group` (ADD/ADC/SUB/SBC/AND/XOR/OR/CP against
  any register/`(HL)`).
Both use `get_cb_reg`/`set_cb_reg` to map a 3-bit register index to
B/C/D/E/H/L/(HL)/A.

**Prefixed instructions** get their own dispatcher functions, wired up as
single entries in `main_opcode_table` (not separate 256-entry tables — despite
some earlier/commented-out code suggesting otherwise, `0xCB` and `0xED`
dispatch via an opcode `switch` inside `z80_op_prefix_cb`/`z80_op_prefix_ed`):
- `0xCB` → `z80_op_prefix_cb` (rotate/shift/BIT/SET/RES via `get_cb_reg`/
  `set_cb_reg`).
- `0xED` → `z80_op_prefix_ed` (extended ops: block transfer/search, NEG,
  16-bit ADC/SBC, `LD SP,(nn)`, `RLD`/`RRD`, etc.).
- `0xDD`/`0xFD` → `z80_op_prefix_index`, which explicitly decodes the
  IX/IY-specific opcodes (16-bit load/arith/inc/dec, push/pop, `EX (SP),IX`,
  `JP (IX)`, `LD SP,IX`, `(IX+d)` displacement forms, and the undocumented
  `IXH`/`IXL`/`IYH`/`IYL` 8-bit ops). For the `0x40`-`0x7F` and `0x80`-`0xBF`
  ranges it delegates to `z80_op_index_ld_r_r`/`z80_op_index_alu_group`,
  which substitute `IXH`/`IXL` for `H`/`L` *unless* the other operand is
  `(IX+d)` memory — real Z80 hardware quirk: `LD H,(IX+d)` loads real `H`,
  not `IXH`. Any opcode not covered by any of this (genuinely
  prefix-independent, e.g. arithmetic/logic against `B`/`C`/`D`/`E`/`A`) falls
  back to `main_opcode_table[opcode]`. A nested `0xDD/0xFD 0xCB d opcode`
  double prefix is handled by `z80_op_index_cb`.

**ALU logic lives in `alu.c`/`alu.h`**, separate from opcode dispatch: flag
bit masks (`FLAG_C`, `FLAG_N`, `FLAG_PV`, `FLAG_X`, `FLAG_H`, `FLAG_Y`,
`FLAG_Z`, `FLAG_S`) and the actual add/sub/logic/rotate/shift/block-op
implementations that compute result + flags. Opcode handlers in `z80.c` call
into these rather than duplicating flag math.

**Memory is a flat 64KB `uint8_t` array** (`RAM_SIZE` in `z80.h`), owned by
`main.c` and pointed to by `Z80.memory`. `z80_read_byte`/`z80_write_byte` are
a bus abstraction but currently just index straight into that array (no bank
switching/MMU).

**CP/M BDOS emulation (`cpm.c`)** is minimal: `check_cpm_bdos()` runs at the
top of every `z80_step()` and, when `PC == 0x0005`, handles BDOS function 2
(console char out, `E`) and function 9 (print `$`-terminated string at `DE`),
then manually pops the return address off the stack into `PC` to simulate the
`RET`. `main.c` preloads `RET` (`0xC9`) at addresses `0x0000` and `0x0005` so
unhandled calls to either still return safely.

