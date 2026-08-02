# Roadmap

This consolidates the planning notes in `notes.txt`, `ideas.txt`, and
`todo.txt` into one up-to-date plan. Phase order is a dependency chain, not
a rigid schedule — each phase needs the previous one working.

## Phase 1: Z80 core emulator — done

- Table-driven opcode dispatch (`main_opcode_table` plus dedicated
  `0xCB`/`0xED`/`0xDD`/`0xFD` prefix handlers) covering the full documented
  and undocumented instruction set, including `IXH`/`IXL`/`IYH`/`IYL` and
  the real-`H`/`L`-vs-half-index-register hardware quirk.
- `zexall.com` and `zexdoc.com` both run to completion: 67/67 tests OK, 0
  errors, 0 unimplemented opcodes.

Deliberately out of scope for this phase, deferred to Phase 3 where they'll
actually be exercised (see Known gaps below): I/O port instructions and
interrupt handling.

## Phase 2: Assembler — in progress

Goal: an assembler capable of building `zexall.z80`/`zexdoc.z80` from
source (a natural correctness target — the assembled output should match
the existing `.com` files byte-for-byte) and CP/M-style `.asm` sources
generally.

- [x] Two-pass assembler (`asm/src/`, builds to `bin/z80asm`): lexer-free
  line-oriented parser, symbol table with forward-reference resolution, a
  recursive-descent expression evaluator (`+ - * / % & | ^ ~`, `$` for the
  current address, `low()`/`high()`), and an instruction encoder covering
  the full non-prefixed/`CB`/`ED`/`DD`/`FD` instruction set including the
  undocumented `IXH`/`IXL`/`IYH`/`IYL` forms and the real-`H`/`L` hardware
  quirk (mirroring the emulator's own decoder).
- [x] Directives: `ORG`, `EQU`, `DB`/`DEFB`, `DW`/`DEFW`, `DS`/`DEFS`
  (with optional fill value), `END`.
- [x] Two example programs (`asm/examples/`) assembled and run through
  `bin/z80_emulator` as an end-to-end correctness check: `hello.asm`
  (labels, `DJNZ`, conditional jumps, CP/M BDOS calls) and `selftest.asm`
  ((IX+d)/(IY+d) addressing, `PUSH`/`POP` IX/IY, `CB`-prefixed rotate/BIT,
  16-bit `ADD HL,DE`) — both pass.
- [ ] **Not yet exhaustively tested.** The encoder was written in one pass
  from known-correct Z80 encodings (the same knowledge base the emulator's
  decoder was built from) and validated against the two example programs
  above, not against every addressing-mode combination. Treat freshly
  encoded instruction forms with the same suspicion ZEXALL originally
  surfaced in the emulator, until there's broader coverage.
- [ ] Macros and `include` directives — required before this can assemble
  `zexall.z80`/`zexdoc.z80` themselves (which lean on `tstr`/`tmsg` macros
  and `local` labels).
- [ ] A small library of example programs beyond the two above.
- [ ] Decide whether to target compatibility with an existing CP/M
  assembler's syntax/output format (e.g. ZSM4, as
  `zexall/ZEXALL-main/README.md` mentions) once macros make that question
  concrete, rather than inventing a new dialect.

## Phase 3: CP/M BDOS/BIOS

- Expand `cpm.c` past the two BDOS functions it has today (2: console char
  out, 9: print `$`-string) to the functions real CP/M-80 programs expect:
  console input (1, 6, 10), and file I/O (open/close/read/write/make/
  delete, search).
- Emulate disk I/O against the host filesystem (a CP/M disk image or a
  mapped subdirectory).
- Implement the I/O port instructions and interrupt delivery this phase
  will actually need for a BIOS layer (see Known gaps).
- Get a real CP/M 2.2 (or similar) system image loaded and booting under
  the emulator.

## Phase 4: Beyond CP/M (exploratory)

Captured from `ideas.txt` as aspirational, not yet scoped:

- A GTK-based UI so this becomes a full computer emulator, not just a CLI
  test harness.
- A custom ROM/OS on top of it — open design questions noted in
  `ideas.txt` include a stack VM and whether Logo-style prefix notation
  could combine with a stack machine model.

## Known gaps / near-term technical debt

Not blocking Phase 1's ZEXALL/ZEXDOC goal (the exerciser doesn't exercise
any of these), but worth fixing before or during Phase 3:

- **I/O ports**: `IN r,(n)`/`IN r,(C)` aren't implemented at all; `OUT
  (n),A` is wired up but discards the port write as a no-op.
- **Interrupts**: `cpu->iff1`/`iff2`/`im` exist on the struct but nothing
  besides `DI`/`EI` touches them — `IM 0`/`1`/`2` isn't implemented, and
  there's no interrupt-delivery mechanism at all.
- **No automated regression check**: correctness is verified by eyeballing
  ZEXALL/ZEXDOC console output for `ERROR` lines. Worth adding a thin
  wrapper (a `make test` target, or a script) that runs both exercisers and
  fails if the output contains `ERROR`, an `Unimplemented opcode` line, or
  doesn't reach `Tests complete` — cheap regression protection for Phase 2/3
  work that touches `z80.c`/`alu.c` again.
- **Flat memory model**: `z80_read_byte`/`z80_write_byte` index straight
  into a 64KB array with no bank switching. Fine for CP/M's 64KB TPA;
  revisit only if a later phase needs more than that.
