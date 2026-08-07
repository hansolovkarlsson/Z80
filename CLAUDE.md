# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A Z80 CPU emulator written in C, built to run CP/M-80 programs. Phase 1 (a
complete Z80 core passing ZEXALL/ZEXDOC cleanly) is done; Phase 2 (a Z80
assembler, including macros/`include`, plus a disassembler) is far along
too — see `cpm/docs/ROADMAP.md` for exact status, what's next (a full CP/M
BDOS/BIOS), and known gaps (I/O ports, interrupts). This directory is a
git repository (initialized after the first working ZEXALL/ZEXDOC pass).
The emulator, assembler, disassembler, GTK launcher, sample disk, and
third-party reference software/docs all live under `cpm/`; `bin/` (the
build output) and `scripts/` (general-purpose tooling) stay at the true
repo root. This repo previously also held a standalone Game Boy
emulator as a separate, code-sharing-free subproject under `gameboy/`;
it was split out into its own repository (via `git subtree split`,
preserving its real commit history) once real end-to-end functionality
made clear the two would never actually share code - see that project's
own `docs/GAMEBOY_ROADMAP.md` (in its new repo) for the full history if
relevant.

Three reference docs live in `cpm/docs/` alongside the roadmap:
`Z80_REFERENCE.md` (the Z80 instruction set, including undocumented
opcodes/flag behavior, plus which of those this emulator can actually
execute today), `ASSEMBLER.md` (the `z80asm` syntax — directives,
expressions, macros), and `CPM_REFERENCE.md` (the CP/M 2.2 BDOS/BIOS
call spec — function numbers, FCB layout, BIOS jump table — that Phase
3's `cpm.c` work targets). This file (`CLAUDE.md`) instead covers *code*
architecture — how the dispatch/encoding is actually implemented, not
the ISA, syntax, or OS spec itself.

## Build & Run

The emulator lives in `cpm/emu/src/`, the assembler in `cpm/asm/src/`, the
disassembler in `cpm/disasm/src/`; the Makefile builds all three into `bin/`
at the repo root.

```
make               # builds bin/z80, bin/z80asm, and bin/z80dasm
make emulator      # just the emulator
make assembler     # just the assembler
make disassembler  # just the disassembler
make run           # build the emulator, then run zexall.com through it | less
make test          # build, then run cpm/tests/run_tests.sh (see below)
make clean         # remove object files and all three binaries
```

Correctness is primarily verified by running the ZEXALL exerciser and
reading its console output for per-opcode `ERROR` reports vs. `OK` lines.
ZEXALL/ZEXDOC (`cpm/emu/zexall/ZEXALL-main/`) are third-party, downloaded
pre-built CP/M test binaries (by Frank D. Cringle, via YAZE-AG, GPLv2) —
not code belonging to this project, and not meant to be edited. They're the
correctness oracle: if the emulator is right, every opcode reports "OK"; a
wrong flag or result shows up as an "ERROR" line naming the instruction. As
of the last full run, both `zexall.com` and `zexdoc.com` pass cleanly
(67/67 OK, 0 errors, 0 unimplemented opcodes). `make test`
(`cpm/tests/run_tests.sh`) turns that "eyeball the output" check into an exit
code: it runs both exercisers and fails on any `ERROR`/`Unimplemented
opcode` line or a missing `Tests complete`, then assembles and runs every
`cpm/asm/examples/*.asm` program and fails on any `FAIL` line (the `OK n`/
`FAIL n` convention `selftest.asm`/`gaps_test.asm` use). ZEXALL/ZEXDOC
don't exercise I/O ports, `IM`, `RETI`/`RETN`, or `LD A,I`/`LD A,R`/`LD
I,A`/`LD R,A` — `cpm/asm/examples/gaps_test.asm` is the only regression
coverage for those.

`bin/z80` takes the `.com` file to run as argv[1] (paths are resolved
relative to the working directory you invoke the binary from, typically
`cpm/` — `bin/` itself stays at the true repo root, so run these as
`../bin/z80` from inside `cpm/`, or `bin/z80` from the repo root with a
`cpm/`-prefixed argument) — there's no default program; running it with
no arguments (or `-h`/`--help`) just prints usage instead:

```
../bin/z80 emu/zexall/ZEXALL-main/zexdoc.com
```

Any further argv entries become the program's own CP/M command-line
arguments — `../bin/z80 cpm_disk/CC.COM HELLO.C` compiles `HELLO.C` the
way a real CCP-launched `CC HELLO.C` would. `write_command_tail()`
(`main.c`) seeds the raw tail at `0x0080` (length byte) /`0x0081`
onward — space-prefixed, uppercased, *not* null-terminated, confirmed
against this project's own real CCP source
(`cpm/resources/ccp/upstream/ccp.asm`'s `bmove0`/`bmove1`/`bmove2`) rather
than guessed. `write_default_fcb()` additionally auto-parses the first
two arguments into the default FCBs at `0x005C`/`0x006C`, matching what
a real CCP also does before running a program — command-line CP/M
utilities of this era commonly read one or the other (or both). Neither
existed before BDS C's `CC.COM`/`CLINK.COM` needed it: every program
tested here up to that point was menu-driven (Turbo Pascal, WordStar,
dBASE, Tasty Basic, MBASIC) and never took an argument this way.
`write_default_fcb()` also expands a bare `*` into `?` for every
remaining position in whichever field (name or type) it appears in,
rather than storing it literally — confirmed against
`cpm/resources/ccp/upstream/ccp.asm`'s own `setname`/`setnam0` and `setty`/
`setty0` routines (the "must be ?'s" comment there), not guessed. Found
via BDS C's own `LDIR.COM` (a real utility for listing `.LBR` library
archives, part of the full `bdsc-all.zip` distribution — see
`cpm/resources/bdsc/upstream/README.md`): given a `*.*` pattern, it silently
reported "No (matching) members found" for every library, since it reads
the pattern out of the FCB looking for `?` wildcards — any program that
reads a raw FCB instead of the text tail is entitled to assume a real
CCP already expanded `*` before it got there, and a literal `*` byte
doesn't match anything. `main.c` writes the default FCBs *before* the
raw tail, not after — FCB2 (`0x006C`-`0x008F`) physically overlaps the
tail buffer (`0x0080` onward), a well-documented real CP/M memory-map
quirk, and whichever gets written last wins that overlap. Real CCP
(`ccp.asm`'s `move0`-then-`bmove0..3` sequence right before its `tran`
call) builds the FCBs first and writes the tail last, so a real command
line is always intact no matter how many arguments it had — FCB2's own
meaningful fields (`DR`/name/type, `0x006C`-`0x0077`) don't reach into
the overlap at all, so real programs relying on FCB2 lose nothing. An
earlier version of this function had the order backwards (tail first,
FCBs second), which silently truncated the tail to nothing the moment a
*second* real argument existed — invisible until `l2 t -d` (BDS C's
alternate linker, two arguments) needed it; `l2 t` (one argument) worked
fine, since FCB2 is only written at all when a second argument exists.

The zexdoc variant checks only documented flag behavior; zexall also
checks the undocumented flags (bits 3 and 5, `FLAG_X`/`FLAG_Y`). Passing
`--ccp [ccp.com]` instead of a plain `.com` path boots a CP/M CCP shell
(default `cpm_disk/ccp.com`) rather than running a single program — see
this file's own BIOS section below and `cpm/docs/CPM_REFERENCE.md`'s CCP
section for how that works.

## Architecture

(For the Z80 instruction set itself — mnemonics, addressing modes,
undocumented opcodes/flags — see `cpm/docs/Z80_REFERENCE.md`. This section is
about how the dispatch code is structured, not the ISA.)

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
  16-bit ADC/SBC, `LD SP,(nn)`, `RLD`/`RRD`, `IN r,(C)`/`OUT (C),r`, `IM
  0`/`1`/`2`, `RETI`/`RETN`, `LD A,I`/`LD A,R`/`LD I,A`/`LD R,A`, etc.).
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
switching/MMU). **I/O ports** are a separate 256-entry `cpu->io_ports` array
with their own `z80_io_in`/`z80_io_out` bus functions — no real devices are
attached, so a port read just returns whatever was last written there.

**CP/M BDOS emulation (`cpm.c`)**: `check_cpm_bdos()` runs at the top of
every `z80_step()` and, when `PC == 0x0005` (or `PC == BDOS_ENTRY`, see
below), handles the BDOS functions `cpm/docs/CPM_REFERENCE.md` documents —
`P_TERMCPM` (0), console output (2, 9), console input (1, 6, 10, 11),
`S_BDOSVER` (12), file I/O (15–23, 26, 33–35, 40), and drive/user
bookkeeping stubs (13, 14, 25, 32) — then manually pops the return
address off the stack into `PC` to simulate the `RET`, unconditionally,
regardless of which function matched (or none did) — so the actual
instruction bytes at `0x0005` never matter for BDOS calls to execute
correctly. `main.c` preloads a real `JP <BDOS_ENTRY>` there (`BDOS_ENTRY`
in `cpm.h`, a plausible-looking but otherwise-inert high address, not
real resident BDOS code) rather than just a bare `RET` — some real
software reads the address out of `0x0006`-`0x0007` as a proxy for "how
much TPA is free" (Turbo Pascal's `TINST.COM` is a concrete example: with
only a bare `RET` there, it read back essentially zero free memory and
refused to start at all). `check_cpm_bdos()` intercepts `BDOS_ENTRY`
itself identically to `0x0005`, the same self-referencing-target
reasoning as the BIOS vectors below, in case software calls that address
directly having read it back rather than always using `CALL 5`.
Immediately before that simulated `RET`, `check_cpm_bdos()` mirrors
whatever it put in `A` into `L` too (`H` set to `0`) — real CP/M BDOS
does this for every function except the couple (27, 31) that return a
genuine 16-bit pointer in `HL` instead of a status code (documented
behavior in the CP/M 2.2 Programmer's Reference, not an emulator
invention), and it matters here because BDS C's own `bdos()` library
wrapper returns its result via `HL` — the standard 8080 C
int-return-value register pair — not `A`. Four functions (1, 6, 11, 12)
already set `L` by hand before this existed, presumably because each
one's own real-software bug already surfaced the need; the generic
mirror at the end covers every other function instead of requiring each
future one to remember it individually. Found getting BDS C's own CDB
debugger operational (`cpm/resources/bdsc/upstream/README.md`'s "not
included" list) — CDB2's target-loading loop's compiled
`if (bdos(20,fcb)) break;` broke on the very first record despite
`F_READ` genuinely succeeding (`A=0`), because `HL` still held whatever
was left over from earlier in CDB2's own code, truncating every debugged
program to its first 128 bytes; assembly-level callers checking `A`
directly (every program validated before this one) never hit it.
`cpm/asm/examples/file_test.asm` check 8 is the permanent regression test.

**BIOS emulation (`cpm.c`)**: `check_cpm_bios()` runs alongside
`check_cpm_bdos()` and handles direct BIOS calls — some real software
(MBASIC's own console-output routine, for one) calls straight into the
BIOS instead of BDOS, bypassing the function-dispatch overhead, and
that's exactly why a full BIOS layer matters here rather than just
`check_cpm_bdos()`. `cpm_bios_init()` (called once from `main.c`) installs
a real `JP <wboot>` at address `0x0000` (not a bare `RET` — some software,
MBASIC included, reads this jump's target to locate the BIOS) and a
17-vector jump table at a fixed `BIOS_BASE`. Every vector is a genuine
`JP <self>`, not a bare `RET`, because MBASIC goes one step further: it
reads a vector's *own jump target* once at startup and self-patches that
address directly into its own code, permanently bypassing the jump table
for speed — a self-referencing `JP` means that trick and a direct call
both land on the identical address, so `check_cpm_bios()` intercepts
either path identically. It gives real behavior to `WBOOT`/`CONST`/
`CONIN`/`CONOUT` (reusing the same console plumbing as the BDOS
functions) and fixed, sensible responses for the rest; see
`cpm/docs/CPM_REFERENCE.md`'s BIOS section for the full vector-by-vector
rundown.

Console *input* needs the host terminal in raw mode
(no line buffering, no local echo) so character-at-a-time BDOS calls see
input the way real CP/M hardware would rather than waiting for a host
Enter keypress; `cpm_console_init()` (called once from `main.c`, `termios`-
based) only touches terminal mode when stdin is actually a TTY (`isatty`),
leaving a piped/redirected stdin untouched, and restores the original
mode via `atexit()`. Function 10 (`C_READSTR`, buffered line input) does
its own minimal line editing (echo, backspace/DEL) since raw mode disables
the terminal's own. Beyond `ICANON`/`ECHO` and the `ICRNL`/`INLCR`/`IGNCR`
CR-vs-LF fix, `IXON` (classic Unix software flow control) is disabled too
— left on, the host tty driver intercepts `Ctrl-S`/`Ctrl-Q` as XOFF/XON
and never delivers the byte at all, silently breaking any real program
that uses them for something else (Turbo Pascal's editor binds `Ctrl-S`
to cursor-left, which is what surfaced this).

`console_char_ready()` (BDOS `C_STAT`/BIOS `CONST`) can't just ask
`select()` "is stdin readable" - for a piped/redirected stdin, `select()`
reports readable both when a real byte is waiting *and* when stdin has
hit EOF (a `read()` genuinely wouldn't block either way), but a real
terminal's console status is never ambiguous like that (idle just means
no key pressed yet). Software that polls status before reading - BDS C's
own console-output routine checks for a Ctrl-C abort after printing
*every* character - saw "ready" forever once a non-interactive stdin ran
dry, called what it thought was a real read, and got EOF's `^Z` (26)
sentinel echoed into the output stream after each character it printed.
Fixed by having `console_char_ready()` actually attempt the read itself
to disambiguate: a genuine byte gets buffered in `pending_char` for the
next `console_read_char()` call (so it isn't lost), while a real EOF
sets a sticky `seen_eof` flag so status checks stop reporting "ready"
from then on, matching how a real console never spontaneously un-idles
on its own.

Console *output* (BDOS functions 1's echo, 2, 6, 9, and BIOS `CONOUT`)
routes every program-supplied byte through `console_emit()` rather than
calling `putchar()` directly. Plain ASCII (`< 0x80`) passes through
unchanged; high-bit bytes (`0x80`-`0xFF`) are translated from CP437 (IBM
PC/DOS "code page 437" — box-drawing, block-shading, and a handful of
accented/Greek/math glyphs) to the equivalent Unicode codepoint and
emitted as UTF-8, via `cp437_high[]` + `putchar_utf8()`. Real CP/M-era
software targeting a graphical terminal commonly emits CP437 bytes for
exactly this kind of output (SARGON's ANSI-enhanced port,
`cpm/resources/sargon/sargon78.com`, is a concrete example — its own README
tells PuTTY users to explicitly set "Code Page 437"). VT100/ANSI cursor-
positioning and color (`SGR`) escape codes need no translation at all —
they're plain ASCII bytes the host terminal already interprets correctly
on its own. Console *input* echo (typed keystrokes) skips `console_emit()`
entirely, since that's always plain ASCII from the keyboard.

`console_emit()` also runs a small state machine (`console_term_state`)
that translates two other, older protocols, each found the same way: a
real full-screen CP/M program behaving correctly in line mode but
printing garbage the moment it drew a form or editing screen.

Real Ashton-Tate dBASE II (`cpm/cpm_disk/DBASE.COM`) was hardcoded, at
whatever terminal type it was originally installed for, to a
Lear-Siegler ADM-3A-class terminal — shared by several CP/M machines'
own built-in terminals (Kaypro among them) — rather than VT100/ANSI:
direct cursor addressing is `ESC = <row+32> <col+32>` (not VT100's
`ESC [ row ; col H`), and `^Z` (0x1A) clears the screen (not VT100's
`ESC [ 2 J`). Confirmed by capturing dBASE II's own raw output
byte-for-byte while driving it through a pty with paced keystrokes (the
same pty/paced-keystroke technique the `find_or_reopen_file()`
investigation above used) — on a plain xterm, neither sequence means
anything, so it printed as literal garbage ("RECORD # 00001" preceded by
a stray "1", stray "!"/"@" where a cursor-address landed, etc.) instead
of moving the cursor. `ESC B <n>` / `ESC C <n>` bracket some video
attribute (almost certainly reverse-video/underline for field
highlighting) whose exact ADM-3A-variant mapping isn't confirmed from
primary-source documentation, so rather than guess at an SGR code,
`console_emit()` only strips those 3-byte sequences — that alone removes
the stray digits from the screen even without reproducing the highlight
itself.

Edward Ream's RED screen editor — part of the same BDS C distribution
as `CC.COM`/`CLINK.COM` (see the BDS C section below), but with its own,
separate copyright (`Copyright (C) 1986 by Enteleki, Inc.`, printed at
its own startup — not covered by Leor Zolman's public-domain release of
the compiler itself, so RED's own source isn't committed to this repo,
unlike `CC.COM`/`CLINK.COM`) — targets a VT52/Heath-Zenith-H19-class
terminal instead: a real, well-documented standard, not guessed. Cursor
addressing is `ESC Y <row+32> <col+32>` (VT52's own convention, the same
offset scheme as ADM-3A's `ESC =` just under a different letter), `ESC K`
erases to end of line (plain VT52), and `ESC l` erases the entire current
line without moving the cursor (an H19 extension beyond plain VT52). RED
doesn't use a dedicated clear-screen code at all — it clears by
positioning to each row in turn and issuing `ESC l`, confirmed the same
pty-capture way. `ESC M` (Reverse Index) also appears in that capture but
needs no translation at all — real ANSI/VT100 terminals already support
it natively as the identical bare `ESC M`, no `[` (CSI) required.

None of `ESC =`/`ESC Y`/`ESC B`/`ESC C`/`ESC K`/`ESC l` collide with real
VT100/ANSI, which always follows `ESC` with `[` (CSI) for cursor/color
control (or is otherwise a real, already-supported bare code like
`ESC M`), so this translation is a pure superset of the old
plain-passthrough behavior: an unrecognized byte after `ESC` (including
`[`) is replayed through untouched, so any other program's real ANSI
escape codes are unaffected. `cpm/asm/examples/term_test.asm` is the
permanent regression test — unlike the file-I/O checks, there's no
CP/M-visible way for a program to read back its own translated console
output, so `cpm/tests/run_tests.sh`'s own dedicated check greps the raw byte
stream this program produces for the expected ANSI translation instead
of relying on an in-program OK-n/FAIL-n self-check.

**File I/O** maps every drive/user number onto one host directory
(`CPM_DISK_DIR`/`cpm_disk/`, created by `cpm_fileio_init()` relative to
wherever `bin/z80` is invoked from) rather than emulating real disk
geometry — the simplest of the options weighed for how FCB-addressed
files should map onto the host filesystem, at the cost of not being able
to express real drive-switching or boot an actual CP/M disk image (see
`cpm/docs/CPM_REFERENCE.md`'s Implementation status section for the trade-off
in full). `build_host_path()` converts an FCB's name/type fields straight
into a host path; `fcb_pattern()`/`fcb_pattern_match()` implement `'?'`
wildcard matching for `F_SFIRST`/`F_SNEXT`/`F_DELETE`. Since there's no
disk-block bookkeeping, open files are tracked in a small table
(`open_files[]`) keyed by the FCB's own memory address — real CP/M
programs have no notion of a file handle distinct from the FCB they
opened, so this mirrors how callers already think about "which file."
Sequential I/O keeps the FCB's real `EX`/`CR` fields in step (one 16KB
extent = 128 records); random I/O (`R0`-`R2`) is a plain linear record
number multiplied by 128 and seeked to directly. `F_OPEN` honors a
caller-supplied nonzero `EX` (computing `RC` relative to that extent's
base record) instead of always resetting to 0 — needed for programs that
reposition mid-file by setting `EX`/`CR` before re-opening rather than
always reading sequentially from the start (a real CP/M `F_OPEN` searches
the directory for the extent matching whatever `EX`/`S1`/`S2` the caller
already put in the FCB). The common `EX==0` case (a fresh, from-the-start
open) is unaffected — `CR` is still reset to 0 there, since plenty of
real programs assume `F_OPEN` does that for them. `F_OPEN`'s
`alloc_open_file()` reuses (closing the stale handle first) any existing
`open_files[]` entry already at the target FCB address rather than
allocating a second entry alongside it — real CP/M has no file-handle
concept distinct from the FCB itself, so a program reusing one FCB
buffer for a new file without an intervening `F_CLOSE` (completely
normal — Turbo Pascal's compiler does exactly this loading `TURBO.MSG`
and then a work file through the same FCB) must transparently start
reading the new file, not silently keep re-reading whichever file
happened to open that address first (found via a real Turbo Pascal
compile that mysteriously always stopped at the same byte count no
matter the source file's real size or content — traced to `TURBO.MSG`'s
own trailing bytes, still open, shadowing every later open at that FCB
address). Random I/O (`F_READRAND`/`F_WRITERAND`/`F_WRITEZF`, functions
33/34/40) goes through `find_or_reopen_file()` rather than
`find_open_file()` — real CP/M's random-access functions work directly
off the FCB's own `EX`/`S1`/`S2` fields, which a `F_CLOSE` doesn't erase,
so a program reusing an already-closed FCB for random I/O without an
intervening `F_OPEN` is relying on real (if informally documented)
CP/M behavior, not committing a bug; `find_or_reopen_file()` transparently
opens the file by the FCB's own filename when no `open_files[]` entry
already exists, the same real-hardware reasoning `alloc_open_file()`
already applies to the sequential-I/O FCB-reuse case above. Found via a
real Ashton-Tate dBASE II binary: `CREATE`ing then `USE`ing a database
printed `End of file found unexpectedly`, and `QUIT`ing it afterward
printed `Disk is full` — both traced to the exact same cause (dBASE
re-reads a just-written `.DBF`'s header via random I/O through an FCB it
had already closed during `CREATE`, without reopening), not two separate
bugs, and not a disk-space or DPB issue at all despite dBASE's own
wording; both messages are gone with `find_or_reopen_file()` in place.
`cpm/asm/examples/file_test.asm`'s check 6 is the regression test. Sequential
`F_READ`/`F_WRITE` (functions 20/21) go through `find_or_reopen_file()`
too, for the identical reason applied to a different BDOS function: found
validating BDS C's own `CC.COM` on a source file large enough to need a
`#include` — its `#include` handling reuses a single FCB (`0x005C`) for
both the main source file and each included header, `F_CLOSE`ing it
after the header and resuming the outer file's `F_READ` from its saved
`EX`/`CR` with no intervening `F_OPEN`. Before this, that second
`F_READ` failed with error 9 ("unopened FCB"), which `CC.COM` surfaced to
the user as `Disk read error` partway through any file needing more than
one `#include`. `cpm/asm/examples/file_test.asm`'s check 7 is the regression
test.

**A fake Disk Parameter Block** (`DPH_BASE`/`DPB_BASE`/`DIRBUF_BASE`/
`ALV_BASE` in `cpm.c`, written once by `cpm_bios_init()`) backs BDOS
functions `DRV_DPB` (31) and `DRV_ALLOCVEC` (27), plus BIOS `SELDSK` —
previously unhandled, so a caller got back whatever `HL` already
contained rather than a real DPB address. Found via Turbo Pascal's `D`ir
command showing `Bytes Remaining On A: 0k` despite writes succeeding
(now reports a plausible `8160k`) — see `cpm/docs/CPM_REFERENCE.md`'s File
I/O Implementation status section for the exact values (an ~8MB fixed
disk, computed per the real CP/M 2.2 Alteration Guide's DPB formulas)
and the full story, including a real Ashton-Tate dBASE II binary whose
own `Disk is full` message looked like the same bug but turned out to
have a different, still-open cause.

**Booting a CCP (`main.c`, `cpm.c`)**: `bin/z80 --ccp <path>` loads a CP/M
Console Command Processor (the `A>` shell — see `cpm/resources/ccp/`) at
`CCP_BASE` (`0xE400`) instead of loading a single program at `0x100`, and
calls `cpm_set_ccp_mode(1)`. With CCP mode on, `check_cpm_bios()`'s
`WBOOT` handling — normally "set PC to 0, which the run loop treats as
the program terminating" — instead re-enters the CCP at `CCP_BASE`,
first loading register `C` from `ram[0x0004]` (the persisted disk/user
byte the CCP itself maintains via its own `setdiska` routine before ever
running a program), matching what a real BIOS's `WBOOT` does before
jumping to the CCP's cold-boot entry point. Two places besides
`check_cpm_bios()` needed to become CCP-mode-aware
(`cpm_is_ccp_mode()`/the `ccp_boot` flag `main.c` threads through) since
both otherwise assume `PC == 0x0000` always means "halt the emulator":
`main.c`'s own loop-level check (skipped entirely in CCP mode, since the
`JP <wboot>` instruction at address `0x0000` needs to actually execute
so `check_cpm_bios()` can catch it at the real `WBOOT` vector address),
and a second, separate guard inside `z80_step()` itself (also skipped in
CCP mode — without that, `PC` would never advance off of `0x0000` at all,
since `main.c` no longer breaks its loop there either).

## Assembler (`cpm/asm/src/`)

A conventional two-pass design, no lexer/token-stream stage — each source
line is parsed directly as a string:

- `symtab.c`/`.h` — a simple linked-list symbol table. `symtab_define()`
  tolerates being called twice with the *same* value (pass 1 defines a
  label, pass 2 redefines it to the same address) but rejects a genuine
  conflicting redefinition.
- `expr.c`/`.h` — recursive-descent expression evaluator (`+ - * / % & |
  ^ ~`, parens, `$` for the current address, `low()`/`high()`, `'c'` char
  literals, `0FFh`/`0xFF` hex). On pass 1, an undefined symbol evaluates to
  `0` and sets `env->unresolved` instead of erroring, since it may be
  defined later in the source; pass 2 treats the same case as a real error.
- `encode.c`/`.h` — the instruction encoder. `parse_operand()` classifies
  each comma-separated operand purely syntactically (register name, `(...)`
  memory form, or fall through to `OP_IMM` expression text) *without*
  evaluating expressions, which is what lets the same `encode_instruction()`
  path run unchanged on both passes: instruction length in Z80 depends only
  on the addressing-mode syntax, never on an expression's value, so pass 1
  doesn't need real values, only correct byte counts. Mirrors the emulator's
  own decoder logic in reverse, including the `IXH`/`IXL`/`IYH`/`IYL`
  half-index-register encodings and the real-`H`/`L`-when-memory-is-the-other-
  operand quirk (`idx_rfield()` here is the encode-side counterpart of
  `get_idx_reg8`/`set_idx_reg8` in `z80.c`).
- `assemble.c`/`.h` — line-level driver: strips comments (quote-aware, so a
  `'` or `"` containing `;` isn't mistaken for a comment), extracts an
  optional `label:`, and either handles a directive (`ORG`, `EQU`, `DB`/
  `DEFB`, `DW`/`DEFW`, `DS`/`DEFS`, `END`) or calls `encode_instruction()`.
  Bytes are written straight into a 64KB `AsmCtx.image` buffer at the
  current `pc` via `asm_emit()`, which only actually writes on pass 2 (pass
  1 just advances `pc` and tracks the min/max address touched) — this is
  also why `DB`/`DW`/`DS` have no line-length limit despite the small,
  fixed-size `EncOut.bytes[8]` used for instructions (real Z80 instructions
  never exceed a handful of bytes; `DB "long string"` can be arbitrary
  length, so those directives write to the image directly instead of
  routing through `EncOut`).
- `main.c` — CLI entry point and the two-pass driver (`run_pass()`, called
  once per pass). On success, writes `image[min_addr..max_addr)` to the
  output file — i.e. the output covers only the address range something was
  actually assembled into, trimmed to whatever `ORG` the source used.
  `run_pass()` walks the preprocessed line array by index (not a plain
  `for`) specifically so it can handle `REPT count`/`ENDM`: on hitting a
  `REPT` line it finds the matching `ENDM` (nesting-aware, for `REPT`
  inside `REPT`), evaluates `count` against that pass's live `$`/symbol
  state, and reprocesses the enclosed line range that many times before
  continuing — `assemble_line()` can't do this on its own since it only
  ever sees one line at a time, and (like `IF`) `REPT`'s count can be
  `$`-dependent, so it can't be resolved by `preprocess.c` before
  addresses exist.

Preprocessing (`preprocess.c`/`.h`) runs once, before the two passes above,
flattening `MACRO`/`ENDM`/`LOCAL`-expanded and `INCLUDE`-spliced source into
a flat line list; conditional assembly (`IF`/`ELSE`/`ENDIF`) and `REPT`/
`ENDM` are integrated into the real two-pass loop instead (`assemble.c` and
`main.c` respectively), since both need live `$`/symbol state rather than
pure text substitution. One preprocessing-level wrinkle `REPT` introduces:
`MACRO`/`ENDM` and `REPT`/`ENDM` both close with the literal keyword
`ENDM`, so capturing a macro body that contains a nested `REPT` block (as
`zexall.mac`'s own `dss` macro does) needs `preprocess.c` to track that
nesting explicitly — otherwise the inner block's `ENDM` would be mistaken
for the end of the macro itself. See `cpm/docs/ASSEMBLER.md` for the syntax
this all produces/consumes, and `cpm/docs/ROADMAP.md` for exact project
status — as of the last update, `bin/z80asm` assembles the real,
unmodified ZSM4 sources `zexall.mac`/`zexdoc.mac` (not just the
Perl-generated `zexall.z80`/`zexdoc.z80`) with zero errors, and the result
runs cleanly through `bin/z80`.

## Disassembler (`cpm/disasm/src/`)

The inverse of `cpm/asm/src/encode.c`, in a separate binary rather than a
`z80asm` mode flag — reading is a different shape of problem than writing
(no expression evaluator or symbol table, but does need to re-derive
labels).

- `decode.c`/`.h` — `decode_instruction(mem, addr)` decodes exactly one
  instruction from a full 64KB memory image and returns its formatted
  text, byte length, and (if the instruction references an absolute
  address — a jump/call target or a `(nn)` memory operand) that address,
  tagged as code or data. Structured like the emulator's own dispatch
  (`decode_cb`/`decode_ed`/`decode_index`/`decode_index_cb` mirroring
  `z80_op_prefix_cb`/`_ed`/`_index`/`z80_op_index_cb`), except every path
  returns text instead of executing. The `DD`/`FD` "not one of my special
  cases" fallback recurses into `decode_instruction_impl` one byte later
  rather than falling back to a table lookup like the emulator does —
  which, as a side effect, correctly handles repeated/stacked `DD`/`FD`
  prefixes for free (each redundant prefix just adds 1 to the wrapping
  `length`, and whichever prefix byte is actually adjacent to the real
  opcode is the one whose special-case table gets consulted). Any byte
  pattern with no real decode (most of `0xED`'s space) falls back to `DB
  nnh` so the decoder never fails to make progress.
- `main.c` — two-pass driver, mirroring the assembler's pass structure in
  reverse: pass 1 linearly decodes the whole loaded image just to collect
  every instruction's referenced address into a label table (`Lxxxx` for
  code targets, `Dxxxx` for data); pass 2 decodes again and, per
  instruction, substitutes a label name for the raw hex address in the
  formatted text wherever pass 1 found one at that address (string search
  for the hex literal `decode_instruction` already produced, not a
  re-formatting step). A label definition line (`Lxxxx:`) is emitted
  before any instruction whose address appears in the table.

**Known limitation** (see `cpm/docs/ROADMAP.md` for the fuller writeup): this
is purely linear decoding, address by address, with no code/data
separation — fed a data region (e.g. embedded message strings), it
decodes those bytes as instructions too, which is correct behavior for
what linear disassembly *is*, not a bug, but does mean output quality
depends on the input being (close to) all code, or on the caller using
`-l` to bound the range. `cpm/docs/Z80_REFERENCE.md` documents the opcode
coverage in detail — this disassembler targets real Z80 machine code
generally, not just this project's own emulator's current capability (the
two are now aligned, since `IN`/`OUT`, `IM`, `RETI`/`RETN`, `LD A,I` etc.
are implemented on the emulator side too, but that wasn't always true and
isn't a given for any future gap).

## GTK terminal (`cpm/gtk/src/`, work in progress)

`bin/z80-gtk` (built via the opt-in `make gtk`, never part of
`make`/`make test`) is a thin GTK4 launcher, not a terminal emulator of
its own: it spawns the real, unmodified `bin/z80` attached to a pty and
hands that pty to a `VteTerminal` widget, which does the actual
VT100/ANSI interpretation — `z80.c`/`cpm.c`/`cpm/emu/src/main.c` needed zero
changes for this, since `console_emit()`/`console_read_char()` already
just talk to stdin/stdout, and a pty's slave side *is* stdin/stdout from
the child's point of view. `main()` calls `lower_fd_limit()` before
touching GTK/VTE at all, capping `RLIMIT_NOFILE` down to 4096 — this
shell's default open-file limit is over a million, and VTE's
`vte_terminal_spawn_async()` closes every inherited fd below that
ceiling in the child before `exec` (glib's `fdwalk()` fallback, since
macOS has no `/proc/self/fd`), which overflowed the spawn thread's stack
at that size (confirmed via a real crash report:
`vte::base::SpawnContext::exec` → `fdwalk` → `__chkstk_darwin`,
"Thread stack size exceeded"). Still blocked, separately, by an
intermittent (~2-3% of launches, matching Apple's own reported rate)
crash inside `libsystem_malloc`'s new "xzone" allocator during
`posix_spawn()` of the large `bin/z80-gtk` binary itself, before any of
this project's own code runs — a confirmed macOS 26 OS bug (see Apple
Developer Forums thread 821081, "Sporadic crash in
xzm_main_malloc_zone_init_range_groups when spawning large binaries"),
not a Homebrew bottle mismatch as first suspected and not fixable from
application code — see `cpm/gtk/README.md` for the full diagnostic writeup
and `cpm/docs/ROADMAP.md`'s Phase 4 for status.

