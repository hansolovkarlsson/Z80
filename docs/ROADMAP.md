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

I/O ports and interrupt-control instructions (`IN`/`OUT`, `IM`, `RETI`/
`RETN`, `LD A,I`/`LD A,R`/`LD I,A`/`LD R,A`) were out of scope for the
ZEXALL/ZEXDOC goal (the exerciser doesn't touch them) but have since been
implemented as part of closing the Known-gaps list below. Actual interrupt
*delivery* (a host-side source raising a real maskable/non-maskable
interrupt) remains deferred to Phase 3, once there's a BIOS device that
needs one.

## Phase 2: Assembler — in progress

Goal: an assembler capable of building `zexall.z80`/`zexdoc.z80` from
source (a natural correctness target) and CP/M-style `.asm` sources
generally.

**Milestone reached**: `bin/z80asm emu/zexall/ZEXALL-main/zexall.z80` now
assembles with zero errors, and running the result through
`bin/z80` reports the same clean 67/67 OK / 0 errors / "Tests
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
  `bin/z80` as an end-to-end correctness check — not just
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
- [x] **Real ZSM4 compatibility target reached: `zexall.mac`/`zexdoc.mac`
  (not just `zexall.z80`/`zexdoc.z80`) now assemble and run cleanly.**
  `zexall.z80` is *not* a human-maintained ZSM4 source — its own header
  says `; zexlax.z80`, and it's generated by
  `emu/zexall/ZEXALL-main/zexlax.pl` (a Perl script that fills
  `@c`/`@d`/`@f`/`@m`/`@s` placeholders in an embedded template with
  random test values and the correct expected CRC). `zexall.mac`/
  `zexdoc.mac` are the real thing: `.z80 ;Assemble with Hector Peraza's
  ZSM4` in the header. Diffing the two showed only three differences, two
  of which turned out to already work (dot-prefixed directives
  `.title`/`.z80` fall through as a harmless no-op; the colon-less `EQU`
  label `flgsat equ spat-2` was already handled by the existing
  `is_known_mnemonic()` disambiguation) and one genuine gap, now closed:
  - **`REPT`/`ENDM`** (used by `zexall.mac`'s own `dss` macro — `MACRO
    ... ENDM` wrapping a `REPT ... ENDM`, in place of `zexall.z80`'s plain
    `DS`). Two parts, matching the plan from the previous update: (a)
    `preprocess.c` now tracks `REPT`/`ENDM` nesting *while capturing a
    macro body*, so the inner block's `ENDM` isn't mistaken for the end of
    the enclosing `MACRO` — the `REPT`/`ENDM` text itself is captured
    verbatim, uninterpreted, at this stage; (b) actual `REPT` expansion
    happens in `asm/src/main.c`'s two-pass driver (`run_pass()`), not the
    preprocessor, since (like `IF`) the repeat count can depend on `$`
    (`dss`'s callers use `&lab+4-$` and `&lab+30-$`). This needed a real
    driver-level restructure: `run_pass()` now walks the preprocessed line
    array by index rather than a plain `for`, so on hitting a `REPT` line
    it can locate the matching `ENDM` (nesting-aware, for `REPT`-inside-
    `REPT`), evaluate the count against the *current* pass's live `$`/
    symbol state, and literally reprocess that line range that many times
    before continuing — something `assemble_line()` can't do on its own
    since it only ever sees one line at a time.
  - Validated at three levels: a new `asm/examples/rept_test.asm`
    exercises top-level `REPT`, `REPT` nested inside a `MACRO` body
    (mirroring `dss` exactly), and a `$`-dependent count used to pad to a
    fixed target address — disassembled the output and confirmed the
    padding lands on the target address byte-exact. Then `zexall.mac`/
    `zexdoc.mac` assemble with zero errors, produce byte-identical output
    to the already-validated `zexall.z80`/`zexdoc.z80` path, and running
    that output through `bin/z80` gives the same clean 67/67 OK, 0 errors
    as every other validated path.
- [x] **Disassembler** (`disasm/src/`, builds to `bin/z80dasm`) — the
  assembler's sibling tool, in a separate binary rather than a mode flag
  on `z80asm` (reading is a different shape of problem: no expression
  evaluator or symbol table needed, but does need to re-derive labels
  from jump/call targets instead of just dumping raw mnemonics).
  - Two-pass, like the assembler but in reverse: pass 1 linearly decodes
    the whole image collecting jump/call/`(nn)` targets, pass 2 decodes
    again and emits a listing with `Lxxxx:`/`Dxxxx:` labels substituted
    in wherever an operand's raw address matches a pass-1 target
    (`L`=code, from `JP`/`CALL`/`JR`/`DJNZ`/`RST`; `D`=data, from `(nn)`
    memory operands). Output is valid `z80asm` input (raw bytes are
    trailing `;` comments, ignored on reassembly).
  - Covers the full instruction set from `docs/Z80_REFERENCE.md`,
    including everything in the "Implementation status" table there that
    the *emulator* can't execute yet (`IN`/`OUT`, `IM`, `RETI`/`RETN`,
    `LD A,I` etc.) — the disassembler decodes real Z80 machine code, not
    just what this project's own emulator happens to support today. Also
    the undocumented `DD`/`FD CB` register-copy form (shown as e.g. `RLC
    (IX+5),B` when the copy target isn't `(HL)`'s slot).
  - Validated against `asm/examples/hello.asm` and `selftest.asm`:
    assemble, disassemble, and the code region matches the source
    exactly byte-for-byte in meaning (including the `(IX+d)`/`IXH`/`IYH`
    forms `selftest.asm` exercises) — see `disasm/examples/`. Also spot-
    checked against the real `zexall.com`: the decoded `start:` routine
    matches `zexall.z80`'s own source exactly (`LD HL,(6)` / `LD SP,HL` /
    ...), and CB/ED/`DD CB`-prefixed forms (`BIT`, `SBC HL`, `RRD`,
    indexed `RLC`, etc.) all decode to plausible output when they appear.
  - **Known limitation, by design for this pass**: purely linear decoding
    with no code/data separation. Fed a `.com` file's *data* region (e.g.
    a message string), it decodes those bytes as if they were
    instructions too - garbage, but not a bug, just what linear
    disassembly does absent a heuristic (or user-supplied hints) for
    where code ends and data begins. A future increment could follow
    `JP`/`CALL`/`JR` targets recursively instead of decoding straight
    through, stopping at unconditional jumps/`RET` the way real
    disassemblers avoid this.

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
any of these):

- [x] **I/O ports**: `IN r,(n)`/`IN r,(C)`/`OUT (n),A`/`OUT (C),r` are now
  implemented (`emu/src/z80.c`'s `z80_op_prefix_ed`, plus `0xD3`/`0xDB` in
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
- **Interrupt delivery**: `cpu->iff1`/`iff2`/`im` are tracked correctly
  (`DI`/`EI`/`IM n`/`RETN` all touch them now) but nothing actually raises
  a maskable or non-maskable interrupt — there's no host-side interrupt
  source (timer, keyboard, etc.) to trigger one yet, and no dispatch logic
  for `IM 0`/`1`/`2`'s differing behavior. Revisit once Phase 3 has a BIOS
  device that actually needs to interrupt (e.g. a timer tick) — building
  the delivery mechanism now would be speculative and hard to validate
  without a real consumer.
- [x] **Automated regression check**: `make test` (`tests/run_tests.sh`)
  runs ZEXALL/ZEXDOC and fails if the output contains `ERROR`, an
  `Unimplemented opcode` line, or doesn't reach `Tests complete`; it also
  assembles and runs every `asm/examples/*.asm` program and fails on any
  `FAIL` line (the `OK n`/`FAIL n` convention `selftest.asm`/
  `gaps_test.asm` use) or unimplemented-opcode hit. `asm/examples/
  gaps_test.asm` specifically covers the I/O-port and `RETI`/`RETN`/
  `LD A,I`-family additions above, since ZEXALL doesn't exercise any of
  them.
- **Flat memory model**: `z80_read_byte`/`z80_write_byte` index straight
  into a 64KB array with no bank switching. Fine for CP/M's 64KB TPA;
  revisit only if a later phase needs more than that.
