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
  (`resources/tastybasic/`, a genuine third-party CP/M program — see Phase 3's
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
  With both fixed, `resources/tastybasic/derive.sh`'s C-preprocessed,
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
- [x] **Real-world validation: Tasty Basic** (`resources/tastybasic/`) — the
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
- [x] **Real-world validation: SARGON** (`resources/sargon/`) — Dan and
  Kathe Spracklen's 1978 Z80 chess program, a genuinely different profile
  than the two BASIC interpreters above: heavy register-level arithmetic
  and board-array logic rather than an interpreter loop, minimal file
  I/O, mostly console-driven. Real source from
  [billforsternz/cpm-sargon](https://github.com/billforsternz/cpm-sargon)
  (a CP/M assembly of
  [retro-sargon](https://github.com/billforsternz/retro-sargon)'s
  restoration of the original book listing); a second port with VT100/
  ANSI graphics ([z80playground/sargon-cpm](https://github.com/z80playground/sargon-cpm))
  gave a prebuilt `.com` for an initial smoke test before assembling the
  real source ourselves. Note: unlike Tasty Basic (GPLv3), this source's
  1978 copyright notice is "all rights reserved" with no open license
  from any host repo — included here as a widely-mirrored historical
  artifact, a deliberate call (see `resources/sargon/upstream/README.md`).
  Getting the real source to assemble surfaced three real gaps, none of
  which any prior real-program testing had exercised:
  1. A macro invocation combined with a same-line label
     (`DRIV04: PRTBLK MVENUM,3`) wasn't recognized as a macro call at all
     — `preprocess.c`'s macro-call detection only ever checked the first
     token on a line, so a label prefix made it fall through to
     `assemble.c` as a plain "unknown mnemonic". Fixed by splitting such
     a line into a standalone label line plus the macro's expansion, the
     same way a bare label on its own line already works.
  2. `EX AF,AF'` (completely standard, documented Z80) failed to
     assemble because the comment-stripping quote-tracker (`assemble.c`'s
     `strip_comment`/`preprocess.c`'s `pp_strip_comment`, both added
     earlier to keep a `'` or `"` containing `;` from being mistaken for
     a comment) treated the bare trailing `'` in `AF'` as *opening* an
     unterminated character literal, silently swallowing the rest of the
     line — including the real trailing `; comment`. Fixed by only
     treating a `'` as opening a quote when it's not immediately preceded
     by an identifier character (a real char literal like `'A'` is always
     preceded by whitespace/comma/paren, never a letter or digit).
  3. A forward-referenced `EQU` expression (`WACT EQU ATKLST`, where
     `ATKLST` is a label defined a few lines later) evaluates against a
     placeholder on pass 1 (the referenced symbol isn't known yet) and
     the real value on pass 2 once every symbol is - legitimately
     different values across passes, not a genuine conflicting
     redefinition the way two real labels colliding would be. `EQU`'s
     handler was running both through the same duplicate-value check.
     Fixed by leaving the symbol undefined for the rest of pass 1 in
     that case (same as any other as-yet-unknown symbol) instead of
     recording a placeholder pass 2 would then see as a conflict.
  With all three fixed, `resources/sargon/derive.sh`'s translation of the
  source's only 3 stray 8080-style mnemonics (`JMP`/`CMP` where the other
  70+ jump/compare instructions in this otherwise-real-Z80 file correctly
  use `JP`/`CP` — an inconsistency in the original file, not systematic
  8080 dialect, confirmed by grepping the whole source before touching
  it) assembles cleanly with `bin/z80asm` and runs correctly through
  `bin/z80`: banner, color/difficulty prompts, and board setup all
  verified interactively.
- [x] **Corrected a wrong assumption about SARGON's board display, and
  fixed two real bugs it led to** — the note above originally claimed the
  board needed VT100/ANSI terminal emulation this project didn't have,
  based on testing exclusively through piped/backgrounded output
  captures (which can only ever show raw escape bytes as literal text,
  never how a live terminal would actually render them). Asking the user
  to run it live in a real terminal instead disproved that entirely: the
  checkered board grid, colors, and rank/file labels — all genuine
  VT100 cursor-positioning (`ESC[row;colH`) and color (`SGR`) escape
  codes — rendered *perfectly*, since `console_emit()`'s raw byte
  passthrough already lets any modern terminal interpret those on its
  own. The real, much narrower remaining gap, found by fetching and
  smoke-testing a second Sargon port
  ([z80playground/sargon-cpm](https://github.com/z80playground/sargon-cpm)'s
  ANSI-enhanced `sargon78.com`, now also in `resources/sargon/` — its own
  README literally tells PuTTY users to set "Code Page 437"): the actual
  chess-piece glyphs are CP437 (IBM PC/DOS code page 437 — box-drawing/
  shading bytes) rather than plain ASCII, and a modern UTF-8 terminal has
  no idea what to do with a lone `0xDB` or `0xB1` byte. Fixed with a
  small CP437→Unicode translation table in `console_emit()` (`cpm.c`,
  see `CLAUDE.md`'s Console output section) — a few dozen lines, not a
  VT100 escape-sequence parser or a GTK terminal widget (see the removed
  Phase 4 entry this replaces).
  Verifying the fix against `resources/sargon/sargon_cpm.asm` (the
  billforsternz plain-console port this project actually builds from
  source, not the ANSI one) surfaced a second, unrelated, more serious
  bug along the way: its piece-graphics data tables use `$83`-style
  dollar-prefixed hex literals extensively (a common vintage-assembler
  convention this project's `expr.c` never supported) — `$` alone was
  already a valid primitive (the current address, e.g. `$-TBASE`), so
  `$83` silently parsed as just `$`, with the `83` entirely discarded
  and no error raised, quietly replacing every affected byte with
  whatever the current address happened to be at that point instead
  of the intended literal value. Confirmed directly: `DB $83,$83,$83,$83`
  (four identical literals) assembled to `00 01 02 03`, not `83 83 83
  83`. Fixed by disambiguating on whether a hex digit immediately
  follows the `$` (two adjacent primaries with no operator between them,
  as in `$` immediately followed by more digits, would never be
  meaningful otherwise) — see `docs/ASSEMBLER.md`'s Numbers and literals
  table. Confirmed no other real source in this project uses `$`
  followed immediately by a hex digit for anything else, so this was a
  SARGON-only silent-corruption bug, invisible until actually looking at
  what the assembled bytes were rather than just "did it assemble
  without an error."
  One remaining, unfixable-here quirk specific to the plain-console
  port: its `DSPBRD` board-drawing routine writes piece bytes into a
  memory-mapped video buffer at a fixed address (`0xC000`, explicitly
  commented `"System Dependent - First video address"` in the original
  source) for whatever specific original 8-bit machine it targeted —
  nothing in the file ever reads that buffer back to turn it into
  console text, so the board itself never visibly renders for this port
  regardless of any encoding fix, a limitation of the original port
  rather than something to chase further here.
- [x] **Real-world validation: Colossal Cave Adventure** (`resources/adventure/`)
  — Willie Crowther and Don Woods' *Colossal Cave Adventure*, a CP/M port
  (350 points, from the [Interactive Fiction Archive](https://www.ifarchive.org/if-archive/games/cpm/Advent_CPM.zip)).
  No source available for this port, just a prebuilt `Adventur.com` plus
  its `Phrogz.din` data file — the opposite validation profile from
  SARGON: real file I/O against a large (113KB) data file rather than
  register-heavy computation, and the first real program tested that
  reads a *second*, separate file at runtime rather than just its own
  `.com` image. Surfaced one real bug in `cpm.c`'s file I/O: `F_OPEN`
  (function 15) unconditionally reset the FCB's `EX`/`CR` fields to 0 on
  every open, discarding any position the caller had deliberately set.
  Real CP/M's `F_OPEN` searches the directory for the extent matching
  whatever `EX`/`S1`/`S2` the caller already put in the FCB — some real
  programs (this Adventure port's own data-file paging, jumping to a
  specific extent+record to fetch a given room's text) rely on exactly
  that rather than always reading sequentially from the start. Fixed by
  honoring a caller-supplied nonzero `EX` (computing `RC` relative to
  that extent's base record) instead of always resetting to 0 — while
  leaving the `EX==0` case (every other test/program so far) byte-for-
  byte unchanged, since plenty of real programs *do* assume Open zeroes
  `CR` for them in that common case. Before the fix, the game's actual
  opening room description was silently skipped in favor of whatever
  text happened to live at the wrong (always-record-0) file position;
  after, the authentic banner, opening room ("YOU ARE STANDING AT THE
  END OF A ROAD..."), and a movement command (`IN`, correctly revealing
  the well house and its items) all verified interactively.
- Implement the I/O port instructions and interrupt delivery this phase
  will actually need for a BIOS layer (see Known gaps) — I/O ports are
  already done, and there's now a real (if minimal) BIOS layer too (see
  above); interrupt delivery is the one piece still deferred.
- [x] **Get a real CP/M 2.2 CCP (shell) booting** (`resources/ccp/`,
  `bin/z80 --ccp cpm_disk/ccp.com`) — genuine, unmodified Digital
  Research CCP source (the `A>` prompt, built-in `DIR`/`TYPE`/`ERA`/
  `REN`/`SAVE`/`USER` commands, and loading/running other `.com` files by
  name), from [brouhaha/cpm22](https://github.com/brouhaha/cpm22/blob/main/ccp.asm).
  Scoped down from "boot a full CP/M system image" to "get just the CCP
  running against our existing BDOS/BIOS" — avoids needing real DPH/DPB
  disk-image machinery (see the File I/O design trade-off above) since
  the CCP only ever talks to the rest of the system through BDOS calls
  and a handful of BIOS conventions this project already implements.
  Two real pieces of work:
  1. **A general 8080→Z80 mnemonic translator** (`scripts/8080_to_z80.py`
     — see `scripts/README.md`) — CP/M predates the Z80, so DRI's own CCP
     source is written entirely in 8080 mnemonics, unlike SARGON (real
     Z80) or Tasty Basic. Unlike SARGON's 3 stray lines, this meant
     translating the whole ~1300-line file — worth building properly
     (register-pair renaming, `M`→`(HL)`, `PSW`→`AF`, condition-code
     jump/call/return forms, ALU ops needing an explicit `A,` operand vs.
     not) rather than hand-translating, and it's now reusable for any
     other 8080-mnemonic CP/M-era source, not CCP-specific — kept under
     `scripts/` rather than `resources/ccp/` for exactly that reason.
     `resources/ccp/preprocess.py` handles the parts that
     *are* CCP-specific: resolving the `IFDEF`/`IFNDEF` conditionals
     `z80asm` doesn't support (fixing the load address at `0E400h`, and
     — deliberately — taking the reformatted source's own `noserial`/
     `noserialize` escape hatch to omit a serialization check that
     compares bytes at the nominal BDOS location against an embedded
     serial number and self-patches the CCP into a `DI`/`HLT` trap on
     mismatch: this build has no resident BDOS bytes for it to compare
     against, since BDOS is emulated entirely on the host side rather
     than being real resident Z80 code, so left enabled it would always
     fail and brick the CCP the first time a program ran).
  2. **A real warm-boot re-entry mechanism** (`emu/src/cpm.c`,
     `emu/src/main.c`) — `main.c` gained a `--ccp <path>` boot mode
     (loads the given file at `CCP_BASE` instead of `0x100`, sets the
     initial PC there, and calls the new `cpm_set_ccp_mode()`). With it
     enabled, `check_cpm_bios()`'s `WBOOT` handling — previously just "set
     PC to 0, which halts the emulator" — instead re-enters the CCP at
     `CCP_BASE`, the direct analog of what a real BIOS's `WBOOT` does
     (reload CCP+BDOS off disk, jump back into the CCP) for a design with
     no real disk image to reload from. Getting this right needed two
     more fixes once real testing (`HELLO` → warm boot → `DIR`) surfaced
     them:
     - Both `main.c`'s own loop-level `PC==0` halt check *and* a second,
       separate one inside `z80_step()` itself needed to become
       CCP-mode-aware (`!ccp_boot` / `cpm_is_ccp_mode()`) — otherwise
       either the `JP <wboot>` instruction main.c preloads at address 0
       never actually got to execute (a program ending with a bare
       `jp 0`, like `hello.com` does, never even reached the real `WBOOT`
       vector for `check_cpm_bios()` to intercept), or once that was
       fixed, `z80_step()`'s own guard turned into a permanent 100%-CPU
       spin instead (returning 0 cycles forever without ever advancing
       PC off of 0).
     - The CCP's cold-boot entry point (`ccploc`/`ccpstart`) expects the
       current disk/user number packed into register `C` — on real
       hardware this comes from BIOS's `WBOOT` loading it out of the
       persisted low-memory byte at `0x0004` before jumping there, since
       a real warm boot reloads the CCP fresh off disk on *every* entry,
       cold or warm. This design instead keeps the same in-RAM CCP
       resident across warm boots (just re-entering it, no reload) —
       but `ccpstart` doesn't know that, so without also seeding `C`
       from `0x0004` the same way, it read whatever register `C` happened
       to contain from the just-exited program instead, corrupting the
       prompt (`A>` silently became `J>` after running `hello.com`, since
       the byte the CCP itself had already dutifully written to `0x0004`
       via its own `setdiska` routine before running the program was
       right there the whole time, just never being read).
  Verified interactively end-to-end: boots to a real `A>` prompt, `DIR`
  correctly lists `cpm_disk/`'s contents via the real BDOS search
  functions (8.3-incompatible names like `tastybasic.com` are silently
  skipped — correct CP/M behavior, not a bug), `TYPE` prints a file's
  contents, and running `HELLO`/`SARGON` by name loads and executes them
  with a correct return to the `A>` prompt afterward — multiple commands
  in a row, not just one.
  - **A real assembler bug, found via actual interactive use** (not
     scripted testing): once `cpm_disk/`'s filenames were fixed to all
     fit CP/M's 8.3 limit (see below), `DIR` should have wrapped its
     output every 4 entries — instead it printed exactly one correct
     4-entry line, then crammed every remaining entry onto a single
     giant line. The suspect looked like the CCP translation at first,
     but the real cause was one level deeper: `z80asm` had **no binary-
     literal (`1100000b`-style) support at all**. `expr.c`'s number
     parser scanned *hex* digits first (since `b`/`B` is itself a valid
     hex digit) and only checked for a trailing `h` afterward — so a
     binary literal's own `b` suffix got silently swallowed as if it
     were part of the number, found no `h` following it, and fell
     through to decimal, misparsing `1100000b` as literal decimal
     1,100,000 — later truncated to fit a byte wherever it was used
     (`0xE0`, wildly different from the intended `0x60`). This exact
     literal is what `ccp.asm`'s `DIR` routine uses to compute a
     directory-entry buffer offset from the returned search-slot number,
     so every 4th entry's "start a new line" branch silently took the
     wrong path. Fixed by scanning the *full* alphanumeric token first
     and deciding hex/binary/decimal from what it ends with, not from a
     greedy hex-digit scan — see `docs/ASSEMBLER.md`'s Numbers and
     literals table for the now-supported binary suffix. Confirms once
     more that even a translated, non-original-Z80 source (unlike
     SARGON) can surface a genuine gap nothing else had ever exercised;
     none of the other real-world programs use binary literals, so this
     was CCP-only.
- [x] **Real-world validation: Turbo Pascal 3.01A** (`resources/turbopascal/`)
  — Borland's real 1985 CP/M-80 integrated environment (full-screen
  WordStar-key editor, single-pass compiler, and compile-and-run, all in
  one program — genuinely bigger in scope than anything tried so far,
  and initially deferred for exactly that reason). Deliberately held off
  on until the SARGON VT100/CP437 investigation above was resolved, since
  Turbo Pascal's editor leans on the same kind of terminal/character
  handling for its screen and window borders. Real, unmodified binaries
  from [retroarchive.org](http://www.retroarchive.org/cpm/lang/TP_301A.ZIP)
  (confirmed genuine via the literal `Copyright (C) 1983,84,85 BORLAND
  Inc.` bytes at the start of both `.COM` files) — no source available,
  like `resources/adventure/`/`resources/Mbasic.com`.
  Two real things surfaced getting it running:
  1. **The bundled `TURBO.COM` ships pre-configured for a "Microbee
     VDU"** (an Australian CP/M computer), not anything a modern ANSI
     terminal understands — confirmed by its own startup banner
     (`Terminal: Microbee VDU`) and `READ.ME`'s own note about being
     "pre-installed... for Microbee disk systems." Real Turbo Pascal
     ships with `TINST.COM` specifically to rewrite `TURBO.COM`'s own
     terminal-control byte tables in place for a different terminal —
     option 6 of its own built-in list of 32 is a genuine "ANSI"
     profile. `resources/turbopascal/derive.sh` reproduces this as a
     real build step: running `TINST.COM` through this project's own
     emulator with that selection, the same role `clang`/`sed` play in
     the other `derive.sh` scripts, just patching a binary instead of
     translating source. Confirmed by `TURBO.COM`'s own banner changing
     from `Terminal: Microbee VDU` to `Terminal: ANSI`, and real ANSI
     escape codes appearing in its output afterward.
  2. **A real emulator bug**: `TINST.COM` refused to even start,
     printing `Not enough memory` / `Program aborted` immediately.
     Real CP/M's zero-page convention (`docs/CPM_REFERENCE.md`) has a
     genuine `JP` to the BDOS entry point at `0x0005`-`0x0007`, but this
     project previously only preloaded a bare `RET` at `0x0005` itself,
     leaving `0x0006`-`0x0007` at zero — harmless for the overwhelming
     majority of software (which just does `CALL 5` and never looks at
     what's stored there), but `TINST.COM` reads that address back
     (`LHLD 6`) as a proxy for "how much TPA is free," and zero read as
     "none." Fixed by preloading a real `JP <BDOS_ENTRY>` instead (a
     new `BDOS_ENTRY` constant in `cpm.h`, comfortably between `CCP_BASE`
     and `BIOS_BASE`, giving a plausible ~61KB of apparent free memory)
     — safe to do since `check_cpm_bdos()` always handles the return
     itself via direct stack manipulation regardless of what instruction
     bytes are actually stored at the call address, confirmed by reading
     its own code before making the change. `check_cpm_bdos()` also now
     intercepts `BDOS_ENTRY` directly, identically to `0x0005`, in case
     software calls that address having read it back rather than always
     using `CALL 5` (the same self-referencing-target reasoning as the
     BIOS vectors).
  Verified interactively: real `TURBO Pascal system Version 3.01A`
  banner, the main menu (`E)dit C)ompile R)un S)ave e(X)ecute D)ir
  Q)uit`, memory stats), all genuine. The full-screen editor itself
  (the main reason this was deferred until VT100/CP437 was sorted out)
  is the natural next thing to try interactively.

## Phase 4: Beyond CP/M (exploratory)

Aspirational, not yet scoped:

- A GTK-based UI so this becomes a full computer emulator, not just a CLI
  test harness — a real terminal widget of its own would mean not
  depending on the host terminal at all (useful for a standalone GUI
  app, or capturing/replaying screen state programmatically), though
  note this is no longer motivated by a VT100 gap the way an earlier
  version of this entry claimed: the current bare stdout passthrough
  already lets any real host terminal correctly interpret standard
  cursor-positioning/color escape codes on its own (see the corrected
  SARGON entry above) — what that investigation actually needed was a
  much smaller CP437-to-Unicode translation, already done.
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
