# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A Z80 CPU emulator written in C, built to run CP/M-80 programs. The current
milestone (per `notes.txt` / `ideas.txt`) is passing the ZEXALL/ZEXDOC
instruction exerciser; later phases plan an assembler and a full CP/M BDOS.
This directory is a git repository (initialized after the first working
ZEXALL/ZEXDOC pass).

## Build & Run

```
./build.sh   # gcc -Wall -Wextra -O2 main.c z80.c cpm.c alu.c -o z80_emulator
./run.sh     # ./z80_emulator | less  (runs zexall.com by default)
```

There is no Makefile and no test framework — correctness is verified by
running the ZEXALL exerciser and reading its console output for per-opcode
`ERROR` reports vs. `OK` lines. ZEXALL/ZEXDOC (`zexall/ZEXALL-main/`) are
third-party, downloaded pre-built CP/M test binaries (by Frank D. Cringle, via
YAZE-AG, GPLv2) — not code belonging to this project, and not meant to be
edited. They're the correctness oracle: if the emulator is right, every
opcode reports "OK"; a wrong flag or result shows up as an "ERROR" line
naming the instruction. To run a specific CP/M `.com` file instead of the
default `zexall.com`, pass it as argv[1]:

```
./z80_emulator zexdoc.com
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

**Prefixed instructions** get their own sub-tables/dispatchers, all wired up
in `z80_init_tables()` and invoked from `main_opcode_table`:
- `0xCB` → `cb_opcode_table` (rotate/shift/BIT/SET/RES).
- `0xED` → `ed_opcode_table` (extended ops: block transfer/search, NEG, 16-bit
  ADC/SBC, `LD SP,(nn)`, etc.).
- `0xDD`/`0xFD` → `z80_op_prefix_index`, which decodes IX/IY-specific opcodes
  (indexed loads/ALU with an `(IX+d)`/`(IY+d)` displacement byte) and, for any
  opcode it doesn't special-case, falls back to re-decoding as a plain
  (non-indexed) opcode via `main_opcode_table`. A nested `0xDD/0xFD 0xCB d
  opcode` double prefix is handled by `z80_op_index_cb`.

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

## Notable non-obvious file

`z80_1.c` is a stray/earlier working copy of `z80.c` (not referenced by
`build.sh`, contains dead commented-out code). Treat `z80.c` as the canonical
source; don't edit `z80_1.c` expecting it to affect the build.
