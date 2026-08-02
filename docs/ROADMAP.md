# Roadmap

The project plan, kept current as work progresses. Phase order is a
dependency chain, not a rigid schedule — each phase needs the previous one
working.

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
source (a natural correctness target) and CP/M-style `.asm` sources
generally.

**Milestone reached**: `bin/z80asm emu/zexall/ZEXALL-main/zexall.z80` now
assembles with zero errors, and running the result through
`bin/z80_emulator` reports the same clean 67/67 OK / 0 errors / "Tests
complete" as the original pre-built `zexall.com` — i.e. our own assembler,
assembling the real unmodified `zexall.z80` source, produces a program our
own (independently ZEXALL-validated) emulator executes correctly. Getting
there surfaced and fixed several real dialect gaps, documented below.

- [x] Two-pass assembler (`asm/src/`, builds to `bin/z80asm`): lexer-free
  line-oriented parser, symbol table with forward-reference resolution, a
  recursive-descent expression evaluator (`+ - * / % & | ^ ~`, `$` for the
  current address, `low()`/`high()`), and an instruction encoder covering
  the full non-prefixed/`CB`/`ED`/`DD`/`FD` instruction set including the
  undocumented `IXH`/`IXL`/`IYH`/`IYL` forms and the real-`H`/`L` hardware
  quirk (mirroring the emulator's own decoder).
- [x] Directives: `ORG`, `EQU`, `DB`/`DEFB`, `DW`/`DEFW`, `DS`/`DEFS`
  (with optional fill value), `END`.
- [x] Conditional assembly (`IF`/`ELSE`/`ENDIF`), integrated into the real
  two-pass loop rather than a text-preprocessing step — it has to see the
  actual `$`/symbol-table state at that point in assembly, which a pure
  preprocessor pass doesn't have yet. Backing this, `expr.c` gained
  relational operators (`eq`/`ne`/`lt`/`le`/`gt`/`ge`, plus symbolic
  `= <> < <= > >=`), lowest precedence, non-chaining, `-1`/`0` for
  true/false (the traditional assembler convention).
- [x] `MACRO`/`ENDM`/`LOCAL`/`INCLUDE` (`asm/src/preprocess.c`) as a text
  substitution stage that runs once and flattens into a line list fed
  unchanged into the two passes above. `name MACRO param1,param2,...`
  definitions (matching the common M80/ZSM4 convention, name before the
  `MACRO` keyword — the same convention `zexall.z80`'s own `tstr`/`tmsg`
  macros use); `&param` substitution; `LOCAL name1,name2,...` for
  per-expansion-unique label renaming (verified with a macro invoked twice
  whose body defines a label — would collide without renaming, and
  doesn't); `INCLUDE "path"` resolved relative to the including file's own
  directory, not the invoking working directory. Also added `ERROR
  'message'`, used by `zexall.z80`'s own macros as a self-check.
- [x] Four example programs (`asm/examples/`) assembled and run through
  `bin/z80_emulator` as an end-to-end correctness check — not just
  assembled, actually executed and checked for the right behavior:
  `hello.asm` (labels, `DJNZ`, conditional jumps, CP/M BDOS calls),
  `selftest.asm` ((IX+d)/(IY+d) addressing, `PUSH`/`POP` IX/IY,
  `CB`-prefixed rotate/BIT, 16-bit `ADD HL,DE`), `macro_test.asm`
  (`MACRO`/`&param`/`LOCAL`), `include_test.asm` + `include_defs.inc`
  (`INCLUDE`, and that a macro defined in an included file is usable back
  in the includer) — all four pass.
- [x] ~~Not yet exhaustively tested~~ — substantially addressed by the
  `zexall.z80` reassembly-and-run milestone above: since `zexall.z80` is
  the source that generates the exerciser covering the full documented and
  undocumented instruction set, successfully assembling *and running* it
  is much broader validation than the four hand-written example programs
  alone. Still not literally exhaustive (macro/directive edge cases outside
  what `zexall.z80` itself exercises are untested), so keep the same
  healthy suspicion of freshly-exercised forms ZEXALL originally taught for
  the emulator.
- [x] **Attempted `zexall.z80` itself and fixed what broke** — six real,
  distinct dialect gaps found and fixed this way (each verified against the
  regression suite before moving to the next):
  1. `LOCAL`-declared names are referenced with `&` too, not bare (e.g.
     `local lab` then `&lab:`, `$-&lab`) — same mechanism as `&param`
     substitution, not a separate bare-identifier rename.
  2. `ASEG`/`CSEG`/`DSEG` segment-selection directives weren't recognized
     at all — now accepted as no-ops (this is a single flat-image
     assembler, segments don't apply).
  3. Macro call arguments can be `<...>`-grouped to bundle a comma-list
     into one argument (`tstr <0edh,042h>,...` — the instruction-under-test
     bytes as one `insn` parameter) — the call-argument splitter now treats
     `<>` like `()` for nesting depth, and strips one enclosing `<>` pair
     from the stored argument value.
  4. Macro parameters are sometimes referenced bare, without `&`
     (`zexall.z80`'s own `tstr` macro does `db insn` alongside `dw
     &memop,&iy,...` in the *same* body) — `&param` substitution now also
     matches a bare whole-identifier occurrence. Trade-off: this is more
     permissive than real ZSM4 and carries a collision risk for a
     hypothetical future macro whose parameter name coincides with an
     unrelated identifier elsewhere in its own body; accepted since the
     alternative was failing to assemble the actual target file.
  5. A colon-less label form (`bdos	push	af`, `crcval	ds	4` — label and
     instruction on one line, no `:`) needed `is_known_mnemonic()`
     (`encode.h`) added so the assembler can tell "unrecognized word
     followed by more text" apart from a genuine unknown instruction.
  6. `low`/`high` needed to work as unary prefix operators (`low msbt`, no
     parens), not just call syntax (`low(msbt)`) — reimplemented in
     `expr.c`'s `parse_unary` as a prefix operator, which subsumes the
     parenthesized form for free (parens are still just generic grouping).
- [ ] **Not fully byte-identical yet**: the reassembled `.com` is 8585
  bytes vs. the original's 8704 (both `org 100h`), and `cmp` reports the
  two are byte-for-byte identical up to EOF of the shorter file - not a
  content mismatch, a truncation. The `crctab` block at the end assembles
  to the correct 1024 bytes in isolation (extracted and assembled
  standalone as a sanity check), so the missing 119 bytes are somewhere in
  how it's produced *in the context of the full file*, not a `crctab`
  syntax gap - not yet root-caused. Doesn't block functional correctness
  (see the milestone above: the reassembled binary runs and passes all 67
  tests), so this is a polish item, not a correctness blocker.
- [ ] A small library of example programs beyond the ones above.
- [ ] Decide whether to target compatibility with an existing CP/M
  assembler's syntax/output format (e.g. ZSM4, as
  `emu/zexall/ZEXALL-main/README.md` mentions) — largely answered in practice
  now (the dialect gaps above are exactly the ZSM4-isms needed), but worth
  an explicit decision on how far to lean into full ZSM4 fidelity vs. the
  more permissive/pragmatic choices made above (e.g. #4).
- [ ] **Disassembler**, the assembler's natural sibling tool: given a
  `.com`/binary, print mnemonic/operand text for each instruction. Three
  concrete payoffs, not just symmetry: (1) it would
  have made the manual byte-by-byte disassembly done by hand during the
  original ZEXALL debugging session (see git history around the DD/FD
  prefix fixes) trivial instead of tedious; (2) it directly attacks the
  assembler's "not yet exhaustively tested" gap above — assemble a test
  program, disassemble the output, and diff against the source, or
  disassemble a real corpus like `zexall.com`/`zexdoc.com` and spot-check
  against `zexall.z80`'s own source; (3) general tooling for inspecting
  CP/M binaries in Phase 3. Shares opcode-table knowledge with `encode.c`
  (same mnemonics, inverse direction) but is a separate binary, not a mode
  flag on `z80asm` - reading is a different shape of problem than writing
  (no expression evaluation or symbol table needed, but does need to
  re-derive labels from jump/call targets to be useful instead of just a
  raw mnemonic dump).

## Phase 3: CP/M BDOS/BIOS

- [ ] **Research the CP/M BDOS/BIOS call specification first** — before
  writing more of `cpm.c`, pin down function
  numbers, register-passing conventions, and return/error conventions from
  a primary source (e.g. the CP/M 2.2 Interface Guide/Programmer's Guide),
  rather than guessing at BDOS semantics the way the current two functions
  (2, 9) were bootstrapped for ZEXALL's narrow needs.
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

Aspirational, not yet scoped:

- A GTK-based UI so this becomes a full computer emulator, not just a CLI
  test harness.
- A custom ROM/OS on top of it — open design questions include a stack VM
  and whether Logo-style prefix notation could combine with a stack machine
  model.

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
