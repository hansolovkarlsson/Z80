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

**A real gap survived "done" until real-world testing found it**: plain
`JP (HL)` (`0xE9`, unprefixed) was never wired into `main_opcode_table` at
all — only the `DD`/`FD`-prefixed `JP (IX)`/`JP (IY)` forms existed. A
completely standard, documented instruction, missing since this phase was
first marked done, because ZEXALL/ZEXDOC test flag-affecting behavior, not
every control-flow opcode. Found (and fixed) when Tasty Basic's own
keyword-dispatch mechanism used it — see Phase 3's "Real-world
validation" milestone below for the full story of what else that testing
turned up.

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
- [x] **Byte-count discrepancy root-caused: it's a bundled-file version
  mismatch, not a bug in `z80asm`.** The reassembled `.com` is 8585 bytes
  vs. the original `zexall.com`'s 8704 (both `org 100h`), `cmp` reporting
  the two byte-for-byte identical up to EOF of the shorter file. Traced
  precisely (temporary instrumentation logging `pc`/`max_addr` through
  the assembly): `z80asm` correctly processes the *entire* source file in
  both cases — the trace ends exactly at the real final `crctab` line
  (`db 02dh,002h,0efh,08dh`) immediately followed by `end`, with
  `max_addr` landing at exactly `0x2289` (8585 bytes from `0x100`). That's
  not a premature stop; it's the source file's genuine, complete content.
  Confirming this wasn't specific to one file: **both** `zexall.z80` (the
  Perl-generated variant) *and* `zexall.mac` (the real ZSM4 source) —
  independently written/generated, 1546 vs. 1552 lines — reassemble to
  the identical 8585 bytes, ending at the identical last `crctab` entry.
  Disassembling the original `zexall.com`'s trailing 119 bytes
  (`bin/z80dasm`, extracted to a standalone file first) shows coherent,
  meaningful Z80 code — a string-copy routine checking for `CR`/`'`
  characters — not padding or noise. So the original pre-built
  `zexall.com` genuinely contains ~119 bytes of real code that has **no
  corresponding source** in either `.z80` or `.mac` file bundled in
  `emu/zexall/ZEXALL-main/`: the pre-built binary and the bundled source
  text are from two different revisions of the real ZEXALL project,
  independent of anything this project's own tools do. Doesn't block
  functional correctness (the reassembled binary runs and passes all 67
  tests) and isn't fixable on the assembler side — there's nothing in the
  available source to assemble that would produce those bytes.
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
- [x] **Two more real dialect gaps, found assembling Tasty Basic**
  (`asm/tastybasic/`, a genuine third-party CP/M program — see Phase 3's
  "Real-world validation" milestone below):
  1. **No `>>`/`<<` shift operators** — worse, `addr >> 8` silently
     mis-parsed as two relational `>` comparisons instead of erroring
     (`1234h >> 8` evaluated to `0FFh`, not `12h`, with no diagnostic at
     all). This broke Tasty Basic's entire command-dispatch table, built
     with a `(addr >> 8) + 080h` / `addr & 0ffh` macro. Fixed by adding
     real shift operators (`expr.c`) at their own precedence level between
     bitwise and additive (matching C) — checking for a *doubled*
     `<`/`>` before `parse_relational` ever sees the input keeps a lone
     `<`/`>`/`>=` unambiguous, only two of the same character in a row is
     ever read as a shift.
  2. **A colon-less label sitting alone on its own line** (directive on a
     *later* line, e.g. a bare `welcome` line followed by `DB "..."` on
     the next) wasn't recognized — only the same-line colon-less form
     (`bdos push af`) was. `assemble.c`'s reinterpret-as-label condition
     required something to follow on the *same* line; relaxed to also
     cover the label-alone case, falling through to the same bare-label-
     line handling the colon form already used.
  With both fixed, `asm/tastybasic/derive.sh`'s C-preprocessed,
  directive-translated `tastybasic_cpm.asm` assembles and runs correctly.
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

- [x] **Research the CP/M BDOS/BIOS call specification first** — done; see
  `docs/CPM_REFERENCE.md` for the full BDOS function table (0–40), FCB
  layout, BIOS 17-vector jump table, DPH/DPB, and zero-page memory map,
  gathered from the CP/M 2.2 Interface Guide/Programmer's Guide and the
  Seasip CP/M archive. Confirms `cpm.c`'s current two functions (2, 9) are
  correct as far as they go, and gives the register/error conventions
  needed to extend it without guessing.
- [x] **Console input** (BDOS functions 1 `C_READ`, 6 `C_RAWIO`, 10
  `C_READSTR`, 11 `C_STAT`) — implemented in `cpm.c`, backed by a
  `termios`-raw host terminal (only when stdin is a real TTY; a piped
  stdin just does a blocking `read()`, EOF mapped to `^Z`). Regression
  coverage: `asm/examples/console_test.asm`, driven with piped stdin by
  `tests/run_tests.sh` since it needs specific input bytes rather than
  running standalone like the other example programs. Two host-terminal
  translations only surfaced once a real program was actually typed at
  *interactively* (piped-input testing can't catch either): `ICRNL`
  silently rewrote the real `CR` a physical Enter key sends into `LF`
  before `read()` ever saw it, making Enter look dead to software written
  against a genuine raw serial line; and a modern keyboard's Backspace/
  Delete key sends `DEL` (0x7F), but CP/M-era software expects the classic
  `BS` (0x08) erase byte. Both fixed in `cpm_console_init()`/
  `console_read_char()` — see `docs/CPM_REFERENCE.md`'s Implementation
  status section.
- [x] **BDOS function 0** (`P_TERMCPM`, "quit to CP/M") — implemented,
  needing a two-part fix (`cpm.c` *and* `z80_step()`) since a naive
  `cpu->pc = 0` alone let the injected `RET` stub at address 0 pop the
  real stack and undo the termination — see `docs/CPM_REFERENCE.md`'s
  Implementation status section for the full mechanism.
- [x] **File I/O** (BDOS functions 15–23, 26, 33–35, 40, plus the drive/
  user stubs 13, 14, 25, 32) — implemented in `cpm.c`. Design decision:
  every drive/user number collapses onto a **single mapped host
  directory** (`cpm_disk/`), the easiest of the options considered — FCB
  names map straight onto host filenames, no disk image or DPH/DPB
  needed. Trade-off: can't express real drive-switching/per-user areas or
  boot an unmodified CP/M disk image (that still needs the DPH/DPB
  machinery `docs/CPM_REFERENCE.md` documents but this design
  deliberately skips) — revisit only if something concrete needs it.
  Regression coverage: `asm/examples/file_test.asm` (create, rename, read
  back, wildcard search, delete, confirm gone).
- [x] **Real-world validation: Tasty Basic** (`asm/tastybasic/`) — the
  best return on effort of anything tried this phase. Rather than only
  hand-written regression tests, got a real, unmodified third-party CP/M
  program (a genuine [Tasty
  Basic](https://github.com/dimitrit/tastybasic) port of Palo Alto Tiny
  BASIC, GPLv3) actually running and interacted with it — banner,
  `PRINT`/arithmetic, `GOSUB`/`RETURN` nesting (3 levels deep, confirmed
  correct unwind order), `FOR`/`NEXT`, `USR` (poking a 2-byte machine-code
  routine and calling it from BASIC — arg/result round-tripped through
  `DE` correctly), and `SAVE`/`LOAD` (real file I/O). Getting there needed
  the `JP (HL)` fix (Phase 1) and the two assembler dialect fixes (Phase
  2) above — none of which ZEXALL, the hand-written example programs, or
  the `zexall.z80`/`.mac` reassembly work had ever exercised. Also ran the
  bundled `resources/tastybasic-main/examples/` programs (`TICTAC.BAS`,
  `REVERSE.BAS`, `DUMP.BAS`, `BATNUM.BAS`) to completion, including a
  byte-perfect `PEEK`-based hex dump of Tasty Basic's own running code
  matching independently-known disassembly exactly. See
  `docs/TASTYBASIC_REFERENCE.md` for the language reference this testing
  produced, including a "known upstream quirks" section documenting real
  bugs found in Tasty Basic *itself* (not this project) along the way —
  an 8-character `SAVE`/`LOAD` filename off-by-one, and a `LOAD`
  truncation bug for any program containing a line number whose low byte
  is `0x1A` (line 1050 among others), which is exactly what broke the
  bundled `tictac.tba` example.
- [x] **Real-world validation: MBASIC, and a real BIOS layer to make it
  work** (`resources/Mbasic.com`) — Microsoft's own BASIC-80 (Rev 5.21,
  CP/M version), a genuinely different, more demanding real program than
  Tasty Basic. It made exactly one BDOS call (function 12, `S_BDOSVER`)
  and then silently quit before ever printing its banner — not
  unimplemented-opcode territory, just a program checking a version
  number this emulator had never bothered to return, added as a small
  fix. That got it *past* the version check, but it then hit a deeper
  issue: its low-level console-output routine calls directly into the
  BIOS rather than BDOS (a standard, portable CP/M optimization for
  performance-sensitive code), locating the BIOS the standard way (read
  the `JP` target embedded at address `0x0000`) - and this emulator had
  never had a real BIOS, just a bare `RET` at address 0, so that lookup
  always returned zero and MBASIC ended up calling address 0 for its very
  first character output. Traced precisely with targeted memory read/
  write instrumentation (not guesswork): MBASIC goes a level further
  still, reading each vector's *own jump target* once at startup and
  self-patching that address directly into its own code to bypass the
  jump table for speed thereafter - a second standard technique, and the
  reason every vector needed to be a genuine `JP <self>`, not just a bare
  `RET` (a self-referencing jump means "call the vector" and "read its
  target, then call that" land on the identical address either way).
  Implemented a full minimal 17-vector BIOS (`cpm_bios_init()`/
  `check_cpm_bios()` in `cpm.c` — see `docs/CPM_REFERENCE.md`'s BIOS
  section for the vector-by-vector behavior), and MBASIC now boots
  completely: real banner (`BASIC-80 Rev. 5.21`, copyright, free-memory
  count), program entry, `RUN`, `FOR`/`NEXT` with arithmetic, and `SYSTEM`
  (MBASIC's own exit command) all work correctly.
- Implement the I/O port instructions and interrupt delivery this phase
  will actually need for a BIOS layer (see Known gaps) — I/O ports are
  already done, and there's now a real (if minimal) BIOS layer too (see
  above); interrupt delivery is the one piece still deferred.
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
