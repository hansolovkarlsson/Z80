# ABC80 BASIC Reference

The BASIC dialect built into the real ABC80's ROM — the language
`bin/abc80` actually runs, distinct from `cpm/docs/ASSEMBLER.md`'s `z80asm`
syntax (a completely different tool, targeting the CP/M side of this
repo). This is a language reference, not a status log — for what this
emulator does and doesn't yet support around BASIC's runtime environment
(keyboard, cassette, sound, disk), see `abc80/docs/ABC80_ROADMAP.md`;
for hardware-level facts, see `abc80/docs/ABC80_REFERENCE.md`.

**Source**: Scandia Metric AB, *Kort beskrivning av ABC-80 BASIC* ("Brief
description of ABC-80 BASIC") — a real, vendor-published quick-reference
card, retrieved from abc80.net's archive. Every command, statement,
function, and operator below is drawn directly from it (translated from
Swedish; keyword names, syntax, and example code are unchanged). Where
noted, facts are additionally cross-checked against the real, committed
BASIC ROM (`abc80/resources/rom/`) via this project's own tools. String
variables use `$` (confirmed against the real character-generator ROM,
`bin/abc80-chargen-dump`'s `0x24` glyph is an unambiguous dollar sign —
the source PDF's scan rendered it as `¤` in one place, which is a
transcription artifact of the scan, not a real ABC80 character; the same
source document's own text says this character "corresponds to `$` in
American computer literature").

## Commands (direct/immediate use)

| Command | Effect | Example |
|---|---|---|
| `NEW` | Deletes all program lines and closes all files | `NEW` |
| `SCR` | Deletes all program lines and closes all files | `SCR` |
| `CLEAR` | Resets all variables to zero and closes all files | `CLEAR` |
| `LIST` | Lists the program in memory to the screen | `LIST` / `LIST 100-200` |
| `LIST PR:` | Lists the program in memory to the printer | `LIST PR:` |
| `RUN` | Runs the program in memory (all variables reset automatically) | `RUN` |
| `REN` | Renumbers every line (and matching `GOTO`/`GOSUB`/etc. targets) | `REN` (→ 10,20,30,...) / `REN 200,20` (→ 200,220,240,...) |
| `ED` | Edits a program line in place, without retyping it | `ED 40` |

## Statements

| Statement | Effect | Example |
|---|---|---|
| `REM` | Comment | `20 REM Anything can go here` |
| `DIM` | Reserves space for an array, string array, or vector | `30 DIM A(10,10)` / `35 DIM A$(10,10)=5` |
| `FOR`/`STEP`/`NEXT` | Runs a block a fixed number of times | `5 FOR I=1 TO 10 STEP 2` ... `20 NEXT I` |
| `LET` | Assigns a value to a variable (`LET` itself is optional) | `40 LET A=B+100` or just `40 A=B+100` |
| `IF-THEN-ELSE` | Conditional | `10 IF A=B THEN C=A ELSE C=B` |
| `GOTO` | Jumps program execution to a given line | `70 GOTO 100` |
| `GOSUB` | Jumps to a subroutine | `50 GOSUB 800` |
| `RETURN` | Ends a subroutine, resuming after the matching `GOSUB` | `800 RETURN` |
| `DEF FN` | Defines a user function | `100 DEF FNZ(X,Y)=X*X+Y*Y` |
| `STOP` | Halts execution and prints the stopping line number | `50 STOP` |
| `END` | Halts execution without printing a line number; closes all files | `90 END` |

## Input/output

| Statement/function | Effect | Example |
|---|---|---|
| `PRINT` | Writes to the screen | `30 PRINT "NOW IT'S BEEN"; A; "SEC"` |
| `;` (leading) | Shorthand for `PRINT` | `35; A,B,C` |
| `INPUT` | Waits for keyboard input | `40 INPUT A,B,C` |
| `INPUTLINE` | Reads a full line from the keyboard, including spaces and quote characters (note: the trailing CR/LF is included too) | `50 INPUTLINE A$` |
| `GET` | Reads one character from the keyboard into a string variable, without echoing it | `100 GET A$` |
| `READ` | Reads variable values from a `DATA` statement | `60 READ A,B,C$,D` |
| `DATA` | Stores values for `READ` to consume | `55 DATA 12,3,45,"I LAGER",2` |
| `RESTORE` | Makes the next `READ` start over from the first (or a given) `DATA` statement | `70 RESTORE` / `80 RESTORE 55` |
| `TAB(K)` | Moves the cursor to column `K` on the current line | `90 PRINT TAB(10);"COL.10"` |
| `CUR(R,K)` | Moves the cursor to row `R` (0-23), column `K` (0-39) | `100 PRINT CUR(11,12);"MIDDLE OF SCREEN"` |

## Program file handling (cassette/floppy)

| Statement | Effect | Example |
|---|---|---|
| `LOAD` | Loads a program saved with `SAVE` or `LIST filename`; `LOAD CAS:` loads the next program on the cassette | `LOAD LUFFAR` |
| `SAVE` | Saves the program to cassette or floppy, tokenized/compressed | `SAVE GOLF` |
| `UNSAVE` | Deletes a file from floppy | `UNSAVE KASSPROG` |
| `NAME ... AS ...` | Renames a file on floppy | `NAME OLDNAME AS NEWNAME` |
| `LIST filename` | Saves the program to cassette or floppy *uncompressed*, as text | `LIST PROGTEXT` |
| `RUN prog` | Runs a program from cassette | `RUN DEMO` |
| `MERGE` | Loads a program from cassette without clearing the one already in memory | `MERGE SUBRUT` |
| `CHAIN` | Loads and runs a new program from cassette or floppy | `100 CHAIN PROG2` |

Filenames can be up to 8 characters, optionally plus `.BAS`, `.BAC`, or
`.TXT`.

## Data file handling

| Statement | Effect | Example |
|---|---|---|
| `PREPARE ... AS FILE n` | Creates a new file with logical number `n` (1-255) | `40 PREPARE "BREV2.TXT" AS FILE 2` |
| `OPEN ... AS FILE n` | Assigns a logical file number (1-255) to a file | `30 OPEN "BREV.TXT" AS FILE 1` |
| `CLOSE` | Closes a file (ends reading/writing; writes an end-of-file marker on close) | `50 CLOSE 2` |
| `PRINT #n` | Writes to a file on cassette or floppy | `35 PRINT 1,A,B,C` |
| `INPUT #n` | Reads data from a file on cassette or floppy | `41 INPUT 1,X,Y` |
| `INPUTLINE #n` | Reads a line from a file (same semantics as plain `INPUTLINE`) | `52 INPUTLINE 1,B0` |

Filenames follow the same rule as program files (up to 8 characters plus
`.BAS`/`.BAC`/`.TXT`).

## String functions

| Function | Effect | Example |
|---|---|---|
| `LEFT$(A$,J)` | The first `J` characters of `A$` | `10 C$=LEFT$(A$,J)` |
| `MID$(A$,I,J)` | `J` characters of `A$` starting at position `I` | `20 D$=MID$(A$,I,J)` |
| `RIGHT$(A$,I)` | Every character of `A$` from position `I` onward | `30 D2$=RIGHT$(A$,I)` |
| `LEN(A$)` | Current length of string `A$` | `40 L=LEN(A$)` |
| `ASC(A$)` | ASCII value of the first character of `A$` | `50 V=ASC(A$)` |
| `CHR$(I)` | A character with ASCII value `I` (up to 4 arguments) | `5 PRINT CHR$(7)` / `15 PRINT CHR$(65,66,67,33)` |
| `INSTR(I,A$,B$)` | Searches for `B$` inside `A$` starting at position `I`; returns the position of the first match, or 0 if not found | `60 P=INSTR(I,A$,B$)` |
| `SPACE$(I)` | A string of `I` spaces | — |
| `STRING$(I,C)` | A string of `I` copies of `CHR$(C)` | — |
| `NUM$(A)` | The string that would result from `PRINT`ing `A` | — |
| `VAL(A$)` | The numeric value of `A$`, parsed as a number | — |
| `+` | Concatenates two strings | `10 A$=B$+C$+"!"` |

For fastest execution, prefer integer variables and integer literals
(e.g. `J%`, `2%`).

## ASCII (string-based decimal) arithmetic

A notable ABC80-specific feature: arithmetic performed directly on
numeric-*looking* strings (digits, `+`, `-`, `.`, no exponent, up to 29
digits), useful for exact decimal math beyond floating-point's own range/
precision. Results are rounded to the given number of decimals.

| Function | Effect | Example |
|---|---|---|
| `ADD$(A$,B$,I)` | Adds `A$` and `B$` (parsed as numbers), rounded to `I` decimals | — |
| `SUB$(A$,B$,N)` | Subtracts, same convention | — |
| `MUL$(A$,B$,N)` | Multiplies, same convention | — |
| `DIV$(A$,B$,N)` | Divides, same convention | — |
| `COMP%(A$,B$)` | Compares `A$` and `B$` as numbers: `-1` if `A$<B$`, `0` if equal, `1` if `A$>B$` | — |

## Graphics

| Statement/function | Effect | Example | Range |
|---|---|---|---|
| `SETDOT R,K` | Lights a graphics dot on screen | `110 SETDOT 18,19` | R: 0-72, K: 2-79 |
| `CLRDOT R,K` | Clears a graphics dot | `120 CLRDOT 18,19` | same |
| `DOT(R,K)` | True if the dot at `R,K` is lit | `130 IF DOT(R,K) THEN SETDOT R+1,K+1` | same |

`SETDOT`/`CLRDOT`/`DOT` address a higher-resolution virtual grid than the
40×24 text/block-graphics character cells (matching the real
2×3-subcell block graphics mode's own per-character addressing:
`abc80/docs/ABC80_REFERENCE.md`'s GRAPHICS mode section) — R going up to
72 and K up to 79 implies roughly 2× the resolution of the 24×40
character grid in each direction. Confirmed by real execution
(`abc80/docs/ABC80_ROADMAP.md`'s Milestone 11): the *usable* R range is
actually `0`-`71`, not `0`-`72` — `SETDOT 72,K` raises a real `ERR 62`.

`SETDOT`/`CLRDOT` only poke a target cell's raw dot-pattern byte — they
do *not* also switch that row into GRAPHICS mode. A row's GRAPHICS/TEXT
mode is a persistent per-row latch that resets to TEXT at the start of
every row (matching `CHR$(151)`'s own "starts graphics mode for **one
line**" wording below), so a bare `SETDOT` call on a row that hasn't
already had `CHR$(151)` printed onto it renders as garbled chargen text
instead of a block dot — confirmed via real execution, identically in
both this project's terminal and GTK-window renderers, so it's real
ABC80 behavior, not an emulator bug. Precede a row's `SETDOT` calls with
`PRINT CUR(row,0);CHR$(151);` (`CUR` moves to that row without disturbing
its own dots) to actually see them.

## `ON` statements

| Statement | Effect | Example |
|---|---|---|
| `ON ... GOTO` | Jumps to one of several lines depending on a variable's value | `10 ON I GOTO 100,200,30` |
| `ON ... GOSUB` | Jumps to one of several subroutines depending on a variable's value | `20 ON I GOSUB 100,200,30` |
| `ON ... RESTORE` | `RESTORE`s to one of several `DATA` statements depending on a variable's value | `30 ON I RESTORE 50,60,70` |
| `ON ERROR GOTO` | Jumps to a given line if a runtime error occurs (the `ERRCODE` function holds the error number) | `5 ON ERROR GOTO 999` |

Specific numeric error codes aren't enumerated in the source consulted
for this document, and haven't been independently derived from the ROM
(a real error-message/code table almost certainly exists in it, but
wasn't traced) — a known gap, not a guess dressed up as a fact. `ERR 11`
was observed during this project's own Milestone 3 testing (see
`abc80/docs/ABC80_ROADMAP.md`) as the result of a real keyboard-input
buffer overflow, which at least narrows what code 11 covers, but this
document doesn't assert a specific meaning for it or any other code
without a firmer source.

## Math functions

| Function | Effect |
|---|---|
| `SIN(X)` | sin X, X in radians |
| `COS(X)` | cos X, X in radians |
| `TAN(X)` | tan X, X in radians |
| `ATN(X)` | arctan X (radians) |
| `LOG(X)` | Natural logarithm of X (ln X) |
| `LOG10(X)` | Base-10 logarithm of X |
| `EXP(X)` | e^X |
| `SQR(X)` | Square root of X |
| `INT(X)` | Largest integer ≤ X |
| `FIX(X)` | Integer part of X |
| `ABS(X)` | Absolute value of X |
| `SGN(X)` | -1 if X<0, 0 if X=0, +1 if X>0 |
| `PI` | 3.14159 |
| `SWAP%(I%)` | An integer equal to `I%` with its high and low bytes swapped |

## Random numbers

| Statement/function | Effect | Example |
|---|---|---|
| `RND` | A random number between 0 and 0.999999 inclusive | `100 A=RND` |
| `RANDOMIZE` | Seeds `RND` with a random starting value | `110 RANDOMIZE` |

## Debugging

| Statement | Effect | Example |
|---|---|---|
| `TRACE` | Prints each executed line's number as the program runs (can also be used as a direct command) | `130 TRACE` |
| `NOTRACE` | Turns `TRACE` off | `700 NOTRACE` |

## Memory addressing and I/O ports

| Statement/function | Effect | Example |
|---|---|---|
| `PEEK(X)` | Reads one byte from memory address X | `5 A%=PEEK(X)` |
| `POKE` | Writes data, byte by byte, to a memory address | `10 POKE 65008,21,47` |
| `INP(I)` | Reads one byte from I/O port I | `15 B%=INP(I)` |
| `OUT` | Writes data to an I/O port | `20 OUT 6,D%` |

## Machine-code routines

| Statement | Effect | Example |
|---|---|---|
| `CALL(A)` | Calls a machine-code routine at address A; returns the HL register pair, interpreted as an integer | `100 Q%=CALL(65408)` |
| `CALL(A,D%)` | As above, but also loads `D%` into the DE register pair before the call | `110 Q%=CALL(A,1024%)` |

## Operators

| Operator | Meaning | Example |
|---|---|---|
| `NOT` | Logical NOT — true if the operand is false | `10 IF NOT A>B THEN 100` |
| `OR` | Logical OR — true if at least one operand is true | `20 IF A>B OR B>C THEN 100` |
| `AND` | Logical AND — true if both operands are true | `30 IF A>B AND B>C THEN 100` |
| `XOR` | Exclusive OR — true if exactly one operand is true | `40 IF A>B XOR B>C THEN 100` |
| `IMP` | Implication — `C IMP D` is false only when C is true and D is false | `50 IF A>B IMP B>C THEN 100` |
| `EQV` | Equivalence — true if both operands are true or both are false | `60 IF A>B EQV B>C THEN 100` |
| `**` | Exponentiation to an integer power, e.g. `2**3%=8` (the exponent must be an integer variable or literal — write `A**INT(B)` for an integer-valued exponent) | `70 C=B**5%` / `80 Y=2.23**X%` |

All of the above can also be used as bitwise operators on integers, e.g.
`80 A%=A% AND 7%`.

## Special characters

| Sequence | Effect | Example |
|---|---|---|
| `CHR$(7)` | "BELL" — sounds a beep on ABC80's built-in speaker | `10 PRINT CHR$(7)` |
| `CHR$(12)` | "FORMFEED" — clears the screen completely | `20 PRINT CHR$(12)` |
| `CHR$(151)` | "START GRAPHICS" — starts graphics mode for one line | `20 PRINT CHR$(151);` |
| `CHR$(135)` | "END GRAPHICS" — ends graphics mode for the line | `30 PRINT CHR$(135);` |

`CHR$(151)`/`CHR$(135)` correspond to the real attribute PROM's
TEXT/GRAPHICS mode-toggle bits — see `abc80/docs/ABC80_REFERENCE.md`'s
"Video generation" section for the underlying hardware state machine
these two codes drive.

### Keyboard

| Key | Effect |
|---|---|
| `CTRL-C` | Aborts the running program or a listing |
| `CTRL-X` | Backspaces the entire current input line |

## Variable types

ABC80 BASIC has four kinds of variables:

1. **Floating-point variables**: name is a single letter A-Ö, or a letter
   plus a digit 0-9 (e.g. `A`, `Z1`, `Ö9`). Range: ±0.1E-127 to
   ±0.999999E+127 (`E` denotes a power of 10, e.g. `0.1E-6` means
   `0.1 × 10⁻⁶`). Every calculation's result is automatically rounded to
   6 significant digits.
2. **Integer variables**: named like a floating-point variable but
   suffixed with `%` (e.g. `I%`, `Y1%`, `A%`). Range: -32768 to +32767 —
   no decimals, but noticeably faster to compute with, so commonly used
   for loop counters and array indices.
3. **String variables**: named like a floating-point variable but
   suffixed with `$` (e.g. `A$`, `K$`, `PI$`). A string variable's length
   generally doesn't need to be declared — one not declared via `DIM`
   automatically gets a maximum length of 80 characters.
4. **Arrays**: one or two subscripts (one = vector, two = matrix);
   indices start at 0. Elements can be floating-point, integer, or
   string. A floating-point element is 5 bytes, an integer element 2
   bytes. Referencing an undeclared array implicitly reserves it as if
   `DIM A(10)` or `DIM A(10,10)` had been written.

String arrays specifically:
```
10 DIM A$ = 80          ' a string holding up to 80 characters
20 DIM B$(60)           ' an array of 61 elements, B$(0)..B$(60), each 80 chars
30 DIM C$(10,10) = 5    ' an 11x11-element array, each element 5 chars
40 DIM D$(20) = 10      ' an array of 21 elements, each element 10 chars
```
`C$` above is called a string *matrix*, `D$` a string *vector*. In the
`DIM B$(60)` case (no explicit element length given), each element takes
no memory until it's actually assigned a value, but is at least 80
characters once it is.

## V:24 interface (serial/modem)

Primarily intended for connecting ABC80 to a modem or for using it as an
"intelligent terminal," though nothing prevents wiring up LEDs, buttons,
relays, etc. instead.

The V:24 connector is wired to port 58 (decimal) — a Zilog PIO port. Bits
0-2 are inputs, bits 3-4 are outputs; reading the port returns both input
and output bits together (mask off the output bits with `AND` if only the
inputs are wanted, e.g. `Z%=7% AND INP(58%)`).

| PIO bit | Weight | Function |
|---|---|---|
| 0 | 1 | RxD in |
| 1 | 2 | CTS in |
| 2 | 4 | DCD in |
| 3 | 8 | TxD out |
| 4 | 16 | RTS out |
| 5 | 32 | Motor relay out |
| 6 | 64 | Tape out |
| 7 | 128 | Tape in |

Connector pinout (DB9): pin 1 = +12V (DTR), pin 2 = OB3 (TxD), pin 3 = IB0
(RxD), pin 4 = OB4 (RTS), pin 5 = IB1 (CTS), pin 6 = +12V (DSR), pin 7 =
GND, pin 8 = IB2 (DCD), pin 9 = -12V. Signal levels: ±12V out, max 10mA;
±5V to ±12V in, where a negative voltage produces a `1` bit at the PIO.

Examples from the source:
```
90 Z%=7% AND INP(58%)          ' read only the input bits
100 OUT 58%,8%    :REM 8=2^3   ' set bit 3 (TxD), clear the other output bits
30 IF (1% AND INP(58%))=0% THEN 30   ' wait for input bit 0 to go from 0 to 1
```

## Sources

- Scandia Metric AB, *Kort beskrivning av ABC-80 BASIC* —
  <https://www.abc80.net/archive/luxor/ABC80/Kort-beskrivning-av-abc80-basic.pdf>
  (the primary source for this entire document).
- This project's own tooling: `bin/abc80-chargen-dump` (verifying the
  real `$` character glyph against the committed chargen ROM, resolving
  an OCR ambiguity in the source PDF scan).
- `abc80/docs/ABC80_ROADMAP.md` (Milestone 3's `ERR 11` finding, cited
  above for context on error codes).

Not yet consulted, but real and available at abc80.net if this document
is extended further: `ABC-om-BASIC.pdf` (a full-length BASIC programming
book, "About BASIC," 27MB — likely contains the numeric error-code table
this document doesn't have) and the Superbasic manuals (a third-party
enhanced BASIC variant, not the native ROM BASIC this emulator runs, so
out of scope here).
