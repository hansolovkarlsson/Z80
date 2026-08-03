# Tasty Basic Language Reference

A reference for the BASIC dialect implemented by [Tasty
Basic](https://github.com/dimitrit/tastybasic) (`asm/tastybasic/`), the
real CP/M program this emulator has been validated against — see
`docs/CPM_REFERENCE.md`'s Implementation status section and
`docs/ROADMAP.md` for how that testing surfaced and fixed real bugs in
this project's own emulator and assembler. This document describes Tasty
Basic's *language*, independent of this codebase — for what it's
implemented on top of (Palo Alto Tiny BASIC), see
`asm/tastybasic/upstream/README.md`.

Gathered from Tasty Basic's own command tables (`tab1`/`tab2`/`tab4`/
`tab5`/`tab6`/`tab8` in `asm/tastybasic/upstream/tastybasic.asm`) and its
upstream `README.md`, not just the upstream doc alone, so this reflects
what's actually wired into the interpreter's dispatch tables.

## Quick start

```
bin/z80asm asm/tastybasic/tastybasic_cpm.asm -o tastybasic.com
bin/z80 tastybasic.com
```

```
OK
>10 PRINT "HELLO WORLD"
>RUN
HELLO WORLD

OK
>
```

## Limitations (inherited from Palo Alto Tiny BASIC)

- Integers only, range -32768..32767 (16-bit signed).
- Exactly 26 variables, `A` through `Z` — no arrays, no string variables
  (string *literals* are fine in `PRINT`, just not stored in a variable).
- Line numbers, like variables, are part of the same integer range.

## Direct commands

Only valid typed directly at the `>` prompt, not as a numbered program
line.

| Command | Effect |
|---|---|
| `LIST` | Lists the stored program. |
| `RUN` | Runs the stored program from the beginning. |
| `NEW` | Clears the stored program and all variables. |
| `CLEAR` | Clears variables (keeps the stored program). |
| `BYE` | Exits to CP/M (`P_TERMCPM`, BDOS function 0). |

## Statements

Usable directly at the prompt (executed immediately) or as part of a
numbered program line.

| Statement | Effect |
|---|---|
| `LET var=expr` | Assigns `expr` to `var`. |
| `PRINT list` | Prints a comma/semicolon-separated list of expressions and/or string literals. A leading `#n` before an item sets its print field width, e.g. `PRINT "X=",#5,X`. A trailing comma suppresses the final newline. |
| `IF expr <relop> expr statement` | Executes `statement` if the comparison is true — no literal `THEN`, whatever follows the comparison is the statement to run. |
| `GOTO expr` | Jumps to the given line number. |
| `GOSUB expr` | Calls the given line number as a subroutine (own call stack, nestable). |
| `RETURN` | Returns from the innermost `GOSUB`. |
| `FOR var=expr TO expr [STEP expr]` | Starts a loop; `STEP` defaults to 1 (can be negative). |
| `NEXT var` | Closes the loop started by the matching `FOR`. |
| `INPUT var[,var...]` | Reads one value per variable from the console. |
| `REM ...` | Comment; rest of the line is ignored. |
| `END` | Stops program execution (back to the `OK` prompt). |
| `DATA const[,const...]` | Declares inline constant data, read by `READ`. Can appear anywhere in the program. |
| `READ var` | Reads the next value from the `DATA` list into `var`. |
| `RESTORE` | Resets the `READ` pointer back to the first `DATA` value. |
| `POKE addr,val` | Writes a byte directly to memory. |
| `OUT port,val` | Writes a byte to an I/O port. |
| `LOAD "name"` | *(CP/M only)* Loads a `.TBA` file from disk, replacing the current program and clearing variables. |
| `SAVE "name"` | *(CP/M only)* Saves the current program to a `.TBA` file. |

## Functions

| Function | Returns |
|---|---|
| `ABS(n)` | Absolute value of `n`. |
| `RND(n)` | A pseudorandom number. |
| `SIZE` | Bytes of free memory remaining. |
| `PEEK(addr)` | The byte at `addr`. |
| `IN(port)` | The byte read from I/O port `port`. |
| `USR(expr)` | Calls a user machine-code routine at a fixed vector (`$0BFE`/`$0BFF`, default `$0C00` for the CP/M build), passing `expr` in `DE` and returning whatever the routine leaves in `DE`. A good way to exercise raw Z80 code called *from* BASIC. |

## Relational operators

`=` `<>` (written `#`) `<` `<=` `>` `>=` — used in `IF`, not as general
expression operators.

## Known upstream quirks

Found while testing this port against the emulator (see
`docs/ROADMAP.md`/git history for the fixes these prompted on the
emulator/assembler side — none of these are emulator bugs, they're
Tasty Basic's own behavior):

- **Filenames are capped at 7 characters, not 8.** `fname:`'s length
  check (`asm/tastybasic/upstream/cpmio.asm`) decrements its counter
  *then* checks for zero, so an 8-character name between the quotes
  (e.g. `SAVE "TEST.BAS"`, exactly 8 characters counting the dot) hits
  `HOW?` instead of succeeding — one character short of the 8-byte FCB
  name field's real capacity.
- **`SAVE`/`LOAD` don't split the name at a `.`.** Whatever's between the
  quotes goes into the FCB's 8-character name field verbatim (a literal
  `.` included), and the 3-character type is always forced to `TBA`
  regardless of what you typed — so `SAVE "TEST.BAS"` (if it fit in 7
  characters) would create `TEST.BAS.TBA` on disk, not `TEST.BAS`. Just
  use a bare name with no extension, e.g. `SAVE "TEST"` → `TEST.TBA`.
- **`.TBA` files aren't plain text.** Each saved line is stored as a
  2-byte little-endian line number followed by the line's raw text, `CR`-
  terminated, with a trailing `^Z` (0x1A) marking end-of-program and the
  rest of the last 128-byte record zero-padded. `cat`-ing the file won't
  look like the program listing; `LOAD` it back into Tasty Basic (or
  `xxd`/`hexdump` it) to actually inspect it.
- **`^C` (break) is polled before every statement during `RUN`**
  (`chkio`, called from `runsml`) — on a real terminal this only matters
  if you actually press `^C` while a program is running. If you're
  driving Tasty Basic with piped/scripted input instead of typing live,
  this poll can consume characters intended for whatever command comes
  *after* `RUN` finishes, since piped input is all immediately available
  rather than arriving key-by-key — not a bug, just something to know if
  scripting a session rather than typing it.
- **`LOAD` silently truncates a program at any line number whose *low*
  byte is `0x1A`** — i.e. any line numbered 26, 282, 538, 794, **1050**,
  1306, 1562, 1818, 2074, ... (26 plus any multiple of 256). Each saved
  line is a raw 2-byte little-endian line number followed by its text
  (see above), and `LOAD`'s end-of-program scan (`cpmio.asm`) just looks
  for the *byte value* `0x1A` anywhere in the file with no awareness that
  it might be reading the middle of a binary line-number field rather
  than the real `^Z` terminator — so a line numbered, say, 1050 (`0x041A`,
  low byte `0x1A`) gets misread as "end of program" and everything from
  that line onward is silently lost, even though the file on disk still
  has the real content. Confirmed by saving a *single*-line program
  (`1050 PRINT "HELLO"`) and reloading it: `LIST` shows nothing at all,
  since the truncation happens on the very first two bytes read. This
  bit the CP/M example programs bundled in `resources/tastybasic-main/`
  directly — `examples/tictac.tba` has a real line 1050, so `LOAD`ing it
  (rather than typing `examples/TICTAC.BAS`'s source in directly, which
  never goes through `LOAD` at all) truncates the stored program right
  there, later surfacing as `HOW?` on `GOTO 1050` once execution reaches
  the missing part. A real bug in Tasty Basic's own file format/`LOAD`
  routine, not this emulator or assembler - work around it by avoiding
  line numbers with a low byte of `0x1A` in anything you intend to `SAVE`.
