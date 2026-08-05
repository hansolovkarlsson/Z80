# MBASIC (BASIC-80) Language Reference

A reference for Microsoft's BASIC-80 (MBASIC) — `cpm/resources/Mbasic.com`,
the other real CP/M program this emulator has been validated against (see
`cpm/docs/ROADMAP.md`'s "Real-world validation: MBASIC" milestone and
`cpm/docs/CPM_REFERENCE.md`'s BIOS section for how that testing led to a real
minimal BIOS implementation). The running copy identifies itself as:

```
BASIC-80 Rev. 5.21
[CP/M Version]
Copyright 1977-1981 (C) by Microsoft
```

Unlike `cpm/docs/TASTYBASIC_REFERENCE.md`, this document is **not** derived
from source (only the compiled `.com` is available, no `.asm`) — it's
gathered from Microsoft's own [BASIC-80 (MBASIC) Reference
Manual](https://archive.org/details/BASIC-80_MBASIC_Reference_Manual)
(the ["Extended/Disk"
edition](https://www.bitsavers.org/pdf/dec/terminal/vt180/AA-P226A-TV_BASIC-80_Reference_Manual_VT180_V5.21_1981.pdf),
matching this exact version, 5.21). So this describes the full language
*as documented*, not features individually re-verified against this
binary the way the Tasty Basic reference's table was cross-checked
against real source. See [Verified against this emulator](#verified-against-this-emulator)
at the end for what's actually been run and confirmed here.

## Quick start

```
bin/z80 cpm/resources/Mbasic.com
```

```
Ok
10 PRINT "HELLO"
RUN
HELLO

Ok
SYSTEM
```

`SYSTEM` exits back to CP/M (BDOS function 0, `P_TERMCPM`).

## Numbers and variables

- **Line numbers**: 1–65529.
- **Variable names**: start with a letter, up to 40 significant
  characters (this "Extended/Disk" edition; the plain "8K" edition only
  used the first 2). A trailing type-declaration character fixes a
  variable's type instead of the default (single precision):
  `%`=integer (-32768..32767), `!`=single precision (~7 digits), `#`=double
  precision (~16 digits), `$`=string. `DEFINT`/`DEFSNG`/`DEFDBL`/`DEFSTR`
  set the default type for a whole range of starting letters at once.
- **Numeric literals**: plain integers/decimals, exponential notation
  (`1.5E3` single, `1.5D3` double), hex (`&H76`), octal (`&O347` or
  `&347`).
- **Strings**: up to 255 characters, double-quoted.
- **Arrays**: `DIM`, up to 255 dimensions, up to 32767 elements per
  dimension, default lower bound 0 (`OPTION BASE 1` switches to 1).

## Commands (direct/command level only)

| Command | Effect |
|---|---|
| `RUN [line]` | Run the program, optionally starting at `line`. |
| `LIST [range]` / `LLIST [range]` | List the program to the console / to the printer. |
| `NEW` | Clear the program and all variables. |
| `LOAD "name"` / `SAVE "name"` | Load/save the program from/to disk. |
| `MERGE "name"` | Load a disk program's lines into the current one. |
| `DELETE range` | Remove lines by number range. |
| `RENUM` | Renumber all lines sequentially. |
| `AUTO` | Auto-generate line numbers after each Enter while typing. |
| `EDIT line` | Interactive line editor for one line. |
| `CLEAR` | Reset all variables to zero/null (optionally set memory limits). |
| `CONT` | Resume after a `^C` break or `STOP`. |
| `KILL "name"` | Delete a disk file. |
| `NAME "old" AS "new"` | Rename a disk file. |
| `CLOAD`/`CLOAD?`/`CLOAD*`, `CSAVE`/`CSAVE*` | Cassette-tape load/verify/save (not applicable under CP/M). |
| `SYSTEM` | Exit to CP/M. |

## Statements

| Statement | Effect |
|---|---|
| `LET var=expr` (`LET` optional) | Assignment. |
| `PRINT list` / `LPRINT list` | Print to console / printer; `,` and `;` separators as usual BASIC convention (tab to next zone / no gap). |
| `PRINT USING fmt,list` / `LPRINT USING` | Formatted print with a picture string (`#`, `.`, `,`, `+`, `$$`, `**`, etc.). |
| `INPUT ["prompt";] var[,var...]` | Read values from the console. |
| `LINE INPUT ["prompt";] var$` | Read one whole line (up to 254 chars) into a string variable, no delimiter parsing. |
| `IF expr THEN stmt [ELSE stmt]` | Conditional. `IF expr GOTO line` is a shorthand. |
| `FOR var=start TO end [STEP step]` / `NEXT [var]` | Loop. |
| `GOTO line` | Unconditional jump. |
| `GOSUB line` / `RETURN` | Subroutine call/return. |
| `ON expr GOTO line[,line...]` / `ON expr GOSUB line[,line...]` | Computed jump/call, 1-indexed by `expr`. |
| `ON ERROR GOTO line` | Install an error handler; `RESUME`/`RESUME line`/`RESUME NEXT` to continue. |
| `DIM var(dims)[,var(dims)...]` | Declare array bounds. |
| `ERASE var[,var...]` | Free an array so it can be re-`DIM`'d. |
| `DATA const[,const...]` / `READ var[,var...]` / `RESTORE [line]` | Inline constant data and a reset-able read cursor. |
| `DEF FN name(params)=expr` | Define a single-line user function, called as `FN name(args)`. |
| `DEF USR[n]=addr` | Set the entry address for the `USR[n]()` function (machine-code call). |
| `CALL addr` | Call a machine-code routine directly (no return value). |
| `POKE addr,val` / `OUT port,val` | Write a memory byte / an I/O port byte. |
| `REM ...` (or `'`) | Comment. |
| `END` | Stop the program, close files, return to command level. |
| `STOP` | Like `END` but resumable with `CONT`; used for breakpoint-style debugging. |
| `WIDTH n` | Set output line width. |
| `NULL n` | Pad each output line with `n` null characters (for slow hardcopy terminals). |
| `OPEN mode,#n,"name"[,reclen]` | Open a disk file (`mode`: `"O"` output, `"I"` input, `"R"` random) as file number `n`. |
| `CLOSE [#n[,#n...]]` | Close one or all open files. |
| `PRINT#n,list` / `INPUT#n,var[,var...]` / `LINE INPUT#n,var$` | Sequential file write/read. |
| `FIELD #n,width AS var$[,width AS var$...]` | Lay out a random-file record buffer. |
| `LSET var$=expr` / `RSET var$=expr` | Left/right-justify a string into a `FIELD`ed buffer. |
| `GET #n[,rec]` / `PUT #n[,rec]` | Read/write one random-file record. |
| `CHAIN "name"[,line]` / `CHAIN MERGE` | Load and run another program, optionally passing variables via `COMMON`. |
| `COMMON var[,var...]` | Declare variables to pass to a `CHAIN`ed program. |
| `ERROR n` | Simulate error number `n` (for testing an error handler), or define custom error codes. |
| `WAIT port,mask[,xor]` | Poll an I/O port until `(port_value XOR xor) AND mask` is nonzero. |

## Functions

**String**: `LEFT$(s,n)`, `RIGHT$(s,n)`, `MID$(s,start[,len])` (also
usable as an lvalue: `MID$(s,start,len)=repl` replaces in place),
`LEN(s)`, `INSTR([start,]s,sub)`, `STR$(n)`, `VAL(s)`, `CHR$(code)`,
`ASC(s)`, `SPACE$(n)`, `STRING$(n,code-or-char)`, `UCASE$`/`LCASE$`
(case conversion, naming varies by edition). `MKI$`/`MKS$`/`MKD$` and
`CVI`/`CVS`/`CVD` convert numbers to/from their raw binary representation
as a string, for packing into `FIELD`ed random-file records.

**Math**: `ABS`, `SGN`, `INT`, `FIX` (truncate toward zero, vs. `INT`'s
truncate toward negative infinity), `SQR`, `EXP`, `LOG` (natural log),
`SIN`, `COS`, `TAN`, `ATN`, `RND[(n)]` (`RND(0)` repeats the last value,
negative reseeds), `RANDOMIZE [seed]`.

**System/misc**: `PEEK(addr)`, `USR[n](arg)` (call the address `DEF
USR[n]` set, arg/result passed the implementation-defined way), `FRE(x)`
(bytes of free memory; `FRE("")` also forces string-space garbage
collection), `INP(port)`, `INKEY$` (non-blocking single-keystroke read),
`INPUT$(n[,#file])` (read exactly `n` characters), `EOF(#n)`, `LOC(#n)`/
`LOF(#n)` (current/total record position in an open file), `POS(x)`
(current print column), `TAB(n)`/`SPC(n)` (`PRINT` column-positioning
helpers), `ERR`/`ERL` (error code/line, valid inside an `ON ERROR`
handler).

## Operators

- **Arithmetic**: `^` `-`(unary) `*` `/` `\` (integer division) `MOD` `+` `-`
- **String concatenation**: `+`
- **Relational**: `=` `<>` `<` `>` `<=` `>=`
- **Logical/bitwise**: `NOT` `AND` `OR` `XOR` `IMP` `EQV`

## Control characters (typed at the console)

`^C` interrupt, `^A` edit current line, `^H` backspace, `^I` tab, `^O`
toggle output on/off, `^R` retype current line, `^S`/`^Q` suspend/resume
scrolling, `^U` delete current line, `^G` bell.

## Verified against this emulator

Actually run through `bin/z80`, not just documented: program entry (typed
line-by-line), `PRINT` with arithmetic expressions, `FOR`/`NEXT` with
computed values (`I*I` in a loop), the version-check at startup (BDOS
function 12), and `SYSTEM` (exits cleanly via `P_TERMCPM`). The much
larger remainder of the language above — file I/O, `PRINT USING`,
arrays, `DEF FN`/`DEF USR`/`CALL` for machine code, error handling, and
so on — is documented from the manual but not yet individually exercised
here; a good set of next things to try if testing further.
