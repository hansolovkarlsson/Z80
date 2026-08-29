# ABC802 BASIC II Reference

The BASIC dialect built into the real Luxor ABC802's ROM — the language
`bin/abc802` actually runs. This is a *language and usage* reference: for
what the emulator does and doesn't support see
[`ABC802_ROADMAP.md`](ABC802_ROADMAP.md), for hardware-level facts see
[`ABC802_REFERENCE.md`](ABC802_REFERENCE.md), and for the older, quite
different ABC80 dialect see
[`../../abc80/docs/ABC80_BASIC_REFERENCE.md`](../../abc80/docs/ABC80_BASIC_REFERENCE.md).
`z80asm` assembly (`../../docs/ASSEMBLER.md`) is a completely different
tool and syntax.

**Sources.** Three, used in this order of authority:

1. **The real ROM**, `abc802/resources/rom/ABC802-basic.{02,12,22}-11.bin`
   plus `ABC802-dos.32-31.bin`. Every keyword table in this document was
   read out of those images rather than transcribed from a manual — see
   [How the keyword tables were read](#how-the-keyword-tables-were-read).
2. **Direct execution**, via `bin/abc802 --type`. Anything marked
   **(verified)** was run against the real ROM in this emulator and the
   result is what is printed here. Where the manual and the machine
   disagree, the machine wins and the disagreement is called out.
3. **Luxor Datorer AB, *ABC 800 BASIC II* (English edition, © 1984)** —
   the vendor manual, retrieved from abc80.net's archive. It documents the
   ABC 800 and marks ABC 802 differences in the margin, collected in its
   Appendix 5. Full URL under [Sources](#sources).

Nothing here is asserted from memory of "how BASICs usually work."

---

## Contents

- [Getting a prompt](#getting-a-prompt)
- [The character set trap](#the-character-set-trap)
- [Writing and editing a program](#writing-and-editing-a-program)
  - [Free format](#free-format)
- [Commands](#commands)
- [Statements](#statements)
- [Functions](#functions)
- [Operators and precedence](#operators-and-precedence)
- [Variables, types and declarations](#variables-types-and-declarations)
- [Screen output](#screen-output)
- [Attributes and colour words](#attributes-and-colour-words)
- [Graphics](#graphics)
- [Disk drives and storage](#disk-drives-and-storage)
  - [Leaving BASIC for the DOS](#leaving-basic-for-the-dos)
- [Error codes](#error-codes)
- [Differences from ABC80 BASIC](#differences-from-abc80-basic)
- [How the keyword tables were read](#how-the-keyword-tables-were-read)
- [Sources](#sources)

---

## Getting a prompt

The ABC802's BASIC prompt is the string `ABC802`, printed on its own line.
There is no `Ready.`

```
$ bin/abc802 --interactive --columns 80
```

is a live session (Ctrl-C reaches BASIC, Ctrl-\ exits). For scripted use:

```
$ bin/abc802 --columns 80 --screen --type $'\nPRINT 6*7\n'
```

Two things about `--type` that matter in practice:

- **Lead with a newline.** The ROM shows its sign-on banner and then waits
  for a keypress, which it consumes to dismiss the banner and does not
  echo. Without a leading `\n` the first real character is eaten.
- **Budget T-states.** Keys are fed at roughly human speed (~0.1 s apart,
  ~300,000 T-states each) because the DART holds exactly one received
  byte. A 40-character script therefore needs `--cycles` in the tens of
  millions. The default 20,000,000 runs out partway through anything
  longer than a line or two.

`--type-at N` holds the text back until N T-states have run; it exists for
disk work, where the ROM reports the keyboard ready long before a booting
program is listening.

## The character set trap

The ABC802 uses the Swedish/Finnish ISO 646 variant (SEN 850200 Annex B),
not ASCII. Nine ASCII positions carry different characters, and this bites
when typing programs — the manual's own examples are affected too.

**(verified)** `PRINT CHR$(64,91,92,93,94,95,96,123,124,125,126)` prints
`ÉÄÖÅÜ_éäöåü`:

| Code | ASCII | ABC802 | | Code | ASCII | ABC802 |
|---|---|---|---|---|---|---|
| 64 | `@` | `É` | | 96 | `` ` `` | `é` |
| 91 | `[` | `Ä` | | 123 | `{` | `ä` |
| 92 | `\` | `Ö` | | 124 | `\|` | `ö` |
| 93 | `]` | `Å` | | 125 | `}` | `å` |
| 94 | `^` | `Ü` | | 126 | `~` | `ü` |
| 95 | `_` | `_` | | | | |

Consequences worth knowing before you are confused by them:

- `PRINT USING`'s exponential marker is typed as `^^^^` and **displays as
  `ÜÜÜÜ`**. It still works. **(verified)**
- The manual writes the numeric field marker as `£` and the file-number
  marker as `£file number`. Both are typographic artefacts of the era's
  UK/Swedish keyboards: on this machine they are `#`. A literal `£` in a
  `PRINT USING` string gives `Error 146`. **(verified)**
- `bin/abc802` accepts host UTF-8 on `--type` and converts it
  (`abc802_utf8_to_chars()`), so `--type 'PRINT "ÅÄÖ"'` works. A codepoint
  the machine has no character for is dropped silently.

## Writing and editing a program

A program line is a line number followed by one or more statements
separated by `:`. `REM` (or `!`) starts a comment; `!` may follow code on
the same line. **(verified)** — and note that `'` is *not* a comment
character here: `10 X=1 ' COMMENT` gives `Error 221`.

```
10 A=1 ! COMMENT
20 REM ANOTHER
```

Typing a line number alone deletes that line; `ERASE` deletes a range.

### Free format

**(verified)** Spaces inside a keyword are ignored entirely. `P RINT 6*7`
prints ` 42` and `PRI NT 1+1` prints ` 2`. This is not a quirk to exploit
but it explains a compatibility question that would otherwise look like a
gap: ABC80's one-word `SETDOT`, `CLRDOT` and `INPUTLINE` are all accepted
here as written. Entering `10 SETDOT 1,1` and then `LIST`ing it prints
`10 SET DOT 1,1` — the keyword is tokenized on entry and re-rendered in
this ROM's own spelling, so an ABC80 program does not need rewriting for
this and a listing always shows BASIC II's canonical form.

**The line editor has no cursor movement at all.** Its entire vocabulary
is backspace (`0x08`), discard-line (`0x18` / Ctrl-X), clear-screen
(`0x0C` / Ctrl-L) and the terminators `0x03` (Ctrl-C), `0x0A` and `0x0D`.
Editing is delete-and-retype. This is hardware, not a missing emulator
feature — see [`ABC802_REFERENCE.md`](ABC802_REFERENCE.md)'s "Line
editing" section, which establishes it by sweeping every byte `0x00`-`0x1F`
through the ROM's own editor. `ED` opens a line for editing; `AUTO`
generates line numbers automatically.

Note that `0x7F` (DEL) is **not** a delete here — it is an ordinary
character. That is what a modern terminal's Backspace key sends, which is
why `bin/abc802` rewrites it.

## Commands

Commands are typed at the prompt, without a line number. Read out of the
ROM's own command table at `0x4057`-`0x40B2` (17 entries, two pairs of
which are synonyms sharing a token):

| Command | Syntax | Effect |
|---|---|---|
| `RUN` | `RUN [[device:]file[.ext]]` | Run the program in memory, or load and run one from storage |
| `NEW` / `SCR` | `NEW` | Clear program and variables (synonyms — same ROM token `0x81`) |
| `CLEAR` | `CLEAR` | Clear all variables and close all open files; the program stays **(verified)** |
| `LIST` | `LIST [[device:]file[.ext]] [,line[-line]]` | List to screen, or to a file/printer |
| `LOAD` | `LOAD [device:]file[.ext]` | Load a program |
| `SAVE` | `SAVE [device:]file[.ext]` | Save the program (tokenized, default extension `.BAC`) |
| `UNSAVE` | `UNSAVE [device:]file[.ext]` | Delete a file |
| `MERGE` | `MERGE [device:]file[.ext]` | Merge a program file into the one in memory |
| `CON` | `CON` | Continue after `STOP` or Ctrl-C **(verified)** — `STOP` reports `Stop in line 20.` |
| `GOTO` | `GOTO line` | Resume execution at a line, keeping variables **(verified)** |
| `RENUMBER` / `REN` | `REN [line[,interval[,from-to]]]` | Renumber (synonyms — same ROM token `0x87`) |
| `ERASE` | `ERASE line [-line]` | Delete one or more program lines |
| `AUTO` | `AUTO [first[,interval]]` | Automatic line numbering |
| `ED` | `ED [line]` | Edit a program line |
| `RESUME` | `RESUME [line]` | Resume after an error |

**(verified)** `SCR` really is accepted on this machine and behaves as
`NEW` — the manual's ABC 802 appendix lists `SCR` among the omitted
keywords, but that entry refers to the ABC 806's high-resolution `SCR`
instruction, not the scratch command. Both share token `0x81` with `NEW`
in the ROM, which is what settles it.

**(verified)** `CON` with nothing to continue gives `Error 207`.

## Statements

Read out of the ROM's statement table at `0x08A5`-`0x0A22`, plus the
extension table at `0x4BFA` that adds `WIDTH`. Grouped here by what they
do rather than by token order.

### Control flow

| Statement | Syntax |
|---|---|
| `IF`-`THEN`-`ELSE` | `IF cond THEN statements/line [ELSE statements/line]` |
| `FOR`-`TO`-`STEP` / `NEXT` | `FOR v=expr TO expr [STEP expr]` … `NEXT v` |
| `WHILE` / `WEND` | `WHILE cond` … `WEND` |
| `GOTO` / `GOSUB` / `RETURN` | `GOTO line`, `GOSUB line`, `RETURN [variable]` |
| `ON` … `GOTO` / `GOSUB` / `RESTORE` | `ON expr GOTO line[,line…]` |
| `ON ERROR GOTO` / `RESUME` | `ON ERROR GOTO [line]`, `RESUME [line]` |
| `STOP` / `END` | `END` closes all files but does not clear variables |

`WHILE`/`WEND` are new relative to ABC80. **(verified)** Both are program
statements only — used directly at the prompt they give `Error 208`
("invalid as a command").

### Functions and procedures

| Statement | Syntax |
|---|---|
| `DEF FN` | single line: `DEF FNname[(arg)]=expr` |
| | multi-line: `DEF FNname[%$][(arg)] [LOCAL v[,v…]]` … `RETURN value` … `FNEND` |
| `FNEND` | terminates a multi-line function |

**(verified)** A multi-line function with `LOCAL`:

```
10 DEF FNA%(X%) LOCAL T%
20 T%=X%*2
30 RETURN T%+1
40 FNEND
50 PRINT FNA%(20)
```
prints ` 41`.

**(verified)** Calling a `DEF FN` function from the prompt before the
program has been run gives `Error 206` ("use RUN command") — the
definition only exists once execution has passed over it.

### Data and declarations

`DATA`, `READ`, `RESTORE`, `DIM`, `COMMON`, `OPTION BASE`, `LET`,
`INTEGER`, `FLOAT`, `SINGLE`, `DOUBLE`, `DIGITS`, `EXTEND`, `NO EXTEND` —
see [Variables, types and declarations](#variables-types-and-declarations).

### Input, output and files

`PRINT`, `;` (a leading `;` is shorthand for `PRINT` **(verified)** — `20 ;"VALUE=";A` lists back as `20 ; "VALUE=";A`), `PRINT USING`,
`INPUT`, `INPUT LINE`, `GET`, `PUT`, `OPEN`, `PREPARE`, `CLOSE`, `POSIT`,
`WIDTH`, `NAME` … `AS`, `KILL`, `CHAIN` — see
[Screen output](#screen-output) and
[Disk drives and storage](#disk-drives-and-storage).

### Machine level and debugging

| Statement | Syntax | Notes |
|---|---|---|
| `POKE` | `POKE address,data[,data…]` | |
| `OUT` | `OUT port,data` | Real port writes — the machine is its own test harness for port work |
| `TRACE` / `NO TRACE` | `TRACE [#file]` | **(verified)** prints each executed line number |
| `RANDOMIZE` | `RANDOMIZE` | Seed `RND`; use once per program |
| `SET DOT` / `CLR DOT` / `TXPOINT` | see [Graphics](#graphics) | |

## Functions

Read out of the ROM's function table at `0x0676`-`0x079A` (56 entries).
`LEFT`/`LEFT$`, `RIGHT`/`RIGHT$`, `MID`/`MID$` and `ASC`/`ASCII` are each
pairs of synonyms sharing one token, so the `$` is optional on the three
string-slicing functions.

### Mathematical

| Function | Result |
|---|---|
| `ABS(x)` | absolute value |
| `SGN(x)` | −1 / 0 / +1 |
| `INT(x)` | greatest integer ≤ x |
| `FIX(x)` | integer part (truncation) |
| `SQR(x)` | square root |
| `EXP(x)` | e**x |
| `LOG(x)` / `LOG10(x)` | natural / common logarithm |
| `SIN` `COS` `TAN` `ATN` | radians |
| `PI` | 3.14159 (single precision) |
| `MOD(a,b)` | remainder of integer division |
| `RND` | random, 0 … 0.9999999 |

**(verified)** `MOD` is a *function*, not an infix operator: `PRINT
MOD(17,5)` gives ` 2`, while `PRINT 17 MOD 5` gives `Error 223`. This is
a real difference from BASICs where `MOD` is an operator.

**(verified)** `SQR(-1)` and `LOG(0)` both give `Error 142` ("incorrect
argument in function").

### String

| Function | Result |
|---|---|
| `LEFT[$](a$,n%)` | first n% characters |
| `RIGHT[$](a$,n%)` | characters from position n% onward |
| `MID[$](a$,p%,k%)` | k% characters from position p% |
| `LEN(a$)` | length |
| `ASC(a$)` / `ASCII(a$)` | code of the first character |
| `CHR$(n[,n…])` | string from character codes |
| `INSTR(n%,a$,b$)` | position of b$ in a$ from n%, else 0 |
| `SPACE$(n%)` | n% spaces |
| `STRING$(i%,k%)` | i% copies of character k% |
| `NUM$(x)` | the string `PRINT x` would produce |
| `VAL(a$)` | numeric value of a$ |
| `HEX$(x)` / `OCT$(x)` | hexadecimal / octal string |
| `a$+b$` | concatenation |

**(verified)** `HEX$(255)` → `FF`, `OCT$(8)` → `10`, `VAL("ABC")` →
`Error 210`, `MID$("AB",9,1)` → `Error 134`.

`MID[$]` is also a *statement* (a separate ROM token, `0xA2`): `MID$(A$,
P%,K%)=…` replaces characters in place rather than returning a substring.

### String (decimal) arithmetic

Exact decimal arithmetic on numeric-looking strings, inherited from ABC80.
`P%` is the number of decimals, or digits if written negative.

| Function | Result |
|---|---|
| `ADD$(a$,b$,[-]p%)` | a$ + b$ |
| `SUB$(a$,b$,[-]p%)` | a$ − b$ |
| `MUL$(a$,b$,[-]p%)` | a$ × b$ |
| `DIV$(a$,b$,[-]p%)` | a$ / b$ |
| `COMP%(a$,b$)` | −1 / 0 / +1 |

**(verified)** `COMP%("2","10")` → `-1`: the comparison is numeric, not
lexicographic.

### Conversion, system and machine level

| Function | Result |
|---|---|
| `CVT%$(i%)` `CVT$%(a$)` `CVTF$(f)` `CVT$F(a$)` | pack/unpack numbers as strings |
| `SWAP%(n%)` | n% with its two bytes exchanged |
| `PEEK(a)` / `PEEK2(a)` | one byte / two bytes (little-endian word) |
| `INP(port)` | read an I/O port |
| `CALL(a%[,d%])` | call machine code at a%, `DE`=d%, result from `HL` |
| `VARPTR(v)` / `VAROOT(v)` | address of a variable / of the variable root |
| `ERRCODE` | the most recent error code |
| `TIME$` | `year-month-day hour.min.sec` |
| `POSIT(n)` | current file/cursor position |
| `TAB(i%)` / `CUR(l%,n%)` | see [Screen output](#screen-output) |
| `DOT(l%,n%)` | see [Graphics](#graphics) |
| `SYS(i%)` | system status, below |

**(verified)** `PEEK(0)` → ` 24`, `PEEK(1)` → ` 114`, `PEEK2(0)` →
` 29208`. The ROM's first two bytes really are `18 72`, and
`0x7218` = 29208, so `PEEK2` is a little-endian 16-bit read.

`SYS(i%)` arguments, per the manual's ABC 802 appendix:

| Call | Returns | **(verified)** on a bare machine |
|---|---|---|
| `SYS(2)` | total storage space | ` 29062` |
| `SYS(3)` | program size | ` 28` |
| `SYS(4)` | remaining storage space | ` 29001` |
| `SYS(5)` | keyboard flag (−1 when a key is pressed) | |
| `SYS(6)` | push the last input character back into the keyboard buffer | |
| `SYS(8)` | (listed for ABC 800; the ABC 802 appendix renumbers 8 → 11/12) | ` 0` |
| `SYS(11)` | starting address of the program | ` -3735` |
| `SYS(12)` | variable root | ` -212` |

**(verified)** `SYS(0)` and `SYS(1)` give `Error 143` ("incorrect SYS
function") — the valid arguments start at 2. Addresses come back as
*signed* 16-bit integers, so add 65536 to read them as addresses.

## Operators and precedence

The ROM's operator table at `0x0625`-`0x0661` is stored **in precedence
order, with `0xFF` closing each level**, so the precedence below is read
directly out of the ROM rather than inferred:

| Level (lowest first) | Operators |
|---|---|
| 1 | `EQV` |
| 2 | `IMP` |
| 3 | `OR`, `XOR` |
| 4 | `AND` |
| 5 | `NOT` |
| 6 | `<=` `<>` `<` `>=` `>` `=` |
| 7 | `+` `-` |
| 8 | `*` `/` |
| 9 | `^`, `**` |

`^` and `**` share a token — they are the same operator spelled two ways.
Remember that `^` displays as `Ü`.

`EQV` (equivalence), `IMP` (implication) and `XOR` behave as on ABC80, and
all of the logical operators double as bitwise operators on integers.

**(verified)** `PRINT 2**0.5` gives ` 1.414213562373095` under `DOUBLE`.
This is a real improvement over ABC80, where `**` required an *integer*
exponent.

## Variables, types and declarations

Four kinds of variable, as on ABC80: floating point (`A`), integer (`A%`),
string (`A$`) and arrays of any of those. Indices start at 0 unless
changed with `OPTION BASE`.

Declarations that change how *undeclared* names are interpreted, or how
much precision floating point carries:

| Statement | Effect |
|---|---|
| `INTEGER` | undeclared variables are integers |
| `FLOAT` | undeclared variables are floating point |
| `SINGLE` | floating point is single precision (7 digits) |
| `DOUBLE` | floating point is double precision (16 digits) |
| `DIGITS n` | how many digits `PRINT` shows |
| `EXTEND` / `NO EXTEND` | allow / disallow long variable names |
| `OPTION BASE n` | lowest array index |
| `COMMON` | variables to carry across a `CHAIN` |

**(verified)**, on a machine that has not yet assigned a variable:

```
DOUBLE
PRINT 1/3          →  .3333333333333333
```
and after `SINGLE` (or by default), `PRINT 1/3` gives ` .333333`.
`DIGITS 3` then gives ` .333`.

**(verified)** `DOUBLE` *after* a variable has been assigned gives
`Error 211` ("precision must not be changed"). Put precision declarations
at the top of the program.

**(verified)** `EXTEND` genuinely enables long names — `COUNTER=5` then
`PRINT COUNTER` prints ` 5`. Without it, a name is one letter plus an
optional digit, as on ABC80: `A1=2` is fine and `AB=1` gives `Error 220`.

Strings and string arrays:

```
10 DIM N$(3)=12      ! 4 elements, each up to 12 characters
20 N$(1)="LUXOR"
30 PRINT LEN(N$(1))  ! prints 5 - the assigned length, not the declared one
```
**(verified.)**

**(verified)** `DIM` on an already-dimensioned array gives `Error 137`
("extending DIM not permitted"); an out-of-range index gives `Error 131`;
`A%=99999` gives `Error 132` ("integer overflow"); assigning a string to a
numeric variable gives `Error 224`.

## Screen output

| Construct | Effect |
|---|---|
| `PRINT expr[;expr…]` | write to the screen |
| `; expr` (leading) | shorthand for `PRINT` |
| `TAB(i%)` | move to column i% |
| `CUR(l%,n%)` | move to line l% (0-23), column n% (0-39 or 0-79) |
| `PRINT USING "format";values` | formatted output |
| `WIDTH n` | 40 or 80 characters per line |
| `INPUT` / `INPUT LINE` / `GET` | keyboard input |
| `CHR$(12)` | clear the screen |

**(verified)** `WIDTH 40` and `WIDTH 80` both work at the prompt and take
effect immediately. In 40-column mode the video hardware draws every
*other* cell at double width, so `--screen` (which dumps all 80 cells)
shows a space after every character — that is the real hardware, not a
rendering bug. See
[`ABC802_REFERENCE.md`](ABC802_REFERENCE.md)'s "40 vs 80 columns".

Which mode the machine *starts* in is DIP switch S3, delivered through a
DART modem-status input; `bin/abc802 --columns 80` sets it, and the
default is 40 to match MAME.

`PRINT USING` format characters, **(verified)**:

| Format | Input | Output |
|---|---|---|
| `"###.##"` | `3.14159` | `  3.14` |
| `"###.##^^^^"` | `31415.9` | ` 31.42E+03` |
| `"SEK ###,###.##"` | `1234567` | `SEK % 1.23457E+06` — grouping commas are **not** supported; the `%` marks a value that would not fit, and the number falls back to free format |

## Attributes and colour words

BASIC II carries the ABC800 family's teletext-style attribute words. On
the monochrome ABC802 the colours have no visible effect, but three of the
attributes are real and load-bearing — in particular **the only way to
turn on graphics mode for a row**.

Each attribute word simply stores one *character code* into the current
screen cell. The code is exactly the ROM token minus `0x80`, which makes
the whole table derivable from the ROM's attribute table at
`0x0810`-`0x088C`:

| Word | Code | Effect on the ABC802 |
|---|---|---|
| `RED` `GRN` `YEL` `BLU` `MAG` `CYA` `WHT` | 1-7 | Row Graphic **off** — back to text for the rest of the row |
| `FLSH` | 8 | Row Flash **on** |
| `STDY` | 9 | Row Flash **off** |
| `NRML` | 12 | not an attribute code in this font |
| `DBLE` | 13 | not an attribute code in this font |
| `GRED` `GGRN` `GYEL` `GBLU` `GMAG` `GCYA` `GWHT` | 17-23 | Row Graphic **on** — mosaic for the rest of the row |
| `HIDE` | 24 | Row Clear **on** |
| `GCON` `GSEP` | 25, 26 | not attribute codes in this font |
| `BLBG` `NWBG` | 28, 29 | not attribute codes in this font |
| `GHOL` `GREL` | 30, 31 | not attribute codes in this font |

**(verified)** by printing each word at a known cell and reading the byte
back with `PEEK`: `RED`→1, `WHT`→7, `FLSH`→8, `STDY`→9, `NRML`→12,
`GRED`→17, `GWHT`→23, `HIDE`→24.

The words whose codes are not attribute codes still store their byte into
the cell — **(verified)**, `NRML` really does leave a 12 there — the
character generator simply does not treat it as a command, so it draws
whatever glyph the font holds at that position. Those words exist for the
ABC806, whose font differs.

What those codes mean to the hardware comes from the *character generator
ROM*, not from the video logic (see
[`ABC802_REFERENCE.md`](ABC802_REFERENCE.md)'s "Attributes"). In the
committed font exactly seventeen codes are attribute commands:

| Codes | Attribute |
|---|---|
| 1-7 | Row Graphic **off** (the seven text colours) |
| 8 | Row Flash **on** |
| 9 | Row Flash **off** |
| 17-23 | Row Graphic **on** (the seven graphics colours) |
| 24 | Row Clear **on** |

So on this machine `GRED` through `GWHT` are seven spellings of "switch
this row to mosaic graphics", and `RED` through `WHT` are seven spellings
of "switch it back".

**You cannot substitute `CHR$()` for an attribute word. (verified)**
`PRINT GWHT;` stores byte 23 in the cell; `PRINT CHR$(23);` stores byte 32
— a space. The print routine filters control codes below 32, so only the
attribute words themselves reach the screen memory. This is worth stating
plainly because the obvious workaround silently does nothing.

An attribute occupies the cell it is written into, and the attribute
applies from there to the end of that row only; every row starts fresh.

## Graphics

The ABC802 has **no bitmap graphics**. The ABC800's and ABC806's
high-resolution instructions (`FGCTL`, `FGFILL`, `FGLINE`, `FGPAINT`,
`FGPOINT`) do not exist here — the manual's ABC 802 appendix says so, and
the ROM's tables contain none of them.

What does exist is 2×3 mosaic block graphics, addressed through the
character cells:

| Statement/function | Effect |
|---|---|
| `SET DOT l%,n%` | light a dot |
| `CLR DOT l%,n%` | clear a dot |
| `DOT(l%,n%)` | −1 if lit, 0 if not |
| `TXPOINT x,y[,1/0]` | set (1) or clear (0) a dot, **origin at lower left** |

**Coordinate ranges, established by bisection against the real ROM
(verified).** The manual's numbers are inconsistent between entries; these
are what the machine actually accepts, with `Error 176` ("graphic dot
outside screen") on anything beyond:

| | `SET DOT` / `CLR DOT` (origin upper left) | `TXPOINT` (origin lower left) |
|---|---|---|
| 40 columns | line 0-71, position 0-**79** | y 0-71, x 0-**77** |
| 80 columns | line 0-71, position 0-**159** | y 0-71, x 0-**157** |

The line range is always 0-71: 24 character rows × 3 sub-rows. The
horizontal range follows `WIDTH`, at 2 sub-columns per character cell.
Note that `SET DOT` and `TXPOINT` do **not** cover the same area, and
their origins are at opposite corners.

**A dot is only visible if its row is in graphics mode.** `SET DOT` pokes
the mosaic bit-pattern byte of a character cell and nothing else; a row
that has not been switched to Row Graphic renders those bytes as text
characters instead. **(verified)** — this is the same behaviour ABC80 has,
and the same trap.

**(verified)** — this draws a zig-zag band of real mosaic blocks:

```
10 PRINT CHR$(12);
20 PRINT CUR(10,0);GWHT;         ! character row 10 is now mosaic
30 FOR K%=2 TO 60
40 SET DOT 30+MOD(K%,3),K%       ! dot rows 30-32 are all character row 10
50 NEXT K%
```

Character row *r* owns dot rows *3r* to *3r+2*, so keeping a figure inside
one row means keeping the line coordinate inside one such group. Because
the attribute cell occupies column 0 of the row, and because the attribute
does not carry to the next row, a figure spanning several rows needs its
own `GWHT` on each of them.

**(verified)** `DOT()` reads back the cell's bit pattern regardless of
mode, so it will happily report dots inside ordinary text characters.

## Disk drives and storage

### Device names

Storage is addressed by a device prefix ending in `:`. The complete list
is the DOS ROM's own device-name table at `0x6E37`-`0x6EB4`, read out of
`ABC802-dos.32-31.bin`:

| Device | Unit | What it is |
|---|---|---|
| `DR0:` `DR1:` `DR2:` | 0, 1, 2 | logical drives — whatever is fitted |
| `MO0:` `MO1:` | 12, 13 | ABC830, 160 KB |
| `MF0:` `MF1:` `MF2:` | 8, 9, 10 | ABC832 / ABC834, 640 KB |
| `SF0:` `SF1:` `SF2:` | 16, 17, 18 | 8-inch floppy |
| `HD0:`…`HD3:` | 4, 5, 6, 7 | hard disk |
| `UFD:` | 30 | the UFD-DOS system device |

Five more device names are not drives:

| Device | Meaning | Found at |
|---|---|---|
| `CAS:` | cassette recorder (SIO channel B) | ROM `0x7481` |
| `MEM:` | 32 KB RAM-floppy — the low RAM the ROM overlays | ROM `0x7368` |
| `PR:` | printer | ROM `0x7081`, in the printer/terminal ROM |
| `NUL:` | the null device — swallows output **(verified)** | ROM `0x1111` |
| `CON:` | keyboard and screen | manual; **(verified)** by use |

**The default device is `DR0:`, with `DR1:` as secondary.** A bare
`SAVE "PROG"` therefore goes to drive 0 and you only need a prefix to
reach anything else.

**(verified)** `LOAD "DR0:NOPE"` gives `Error 21` ("file not found") even
with nothing attached, while `bin/abc802` fits a card only when `--disk`
is given. `LOAD "CAS:…"` blocks forever — the SIO models its registers but
has no cassette attached, so nothing is ever received.

**(verified, and a limitation)** `MEM:` is present in the ROM and
`SAVE "MEM:1"` reports no error, but reading it back with `LOAD "MEM:1"`
gives `Error 37` ("incorrect sector format") in this emulator, for several
different numbers. The RAM-floppy is therefore **not usable under
`bin/abc802` today**. It lives in the 32 KB of RAM the ROM overlays, so a
bank-selection path is the likely cause; this has not been diagnosed
further. Per the manual, `MEM:` takes a *number* instead of a filename,
and the address it uses is `number × 253`.

### File names

Up to **eight** alphanumeric characters, the first a letter, optionally
followed by a **three**-character extension. Two extensions are special:

| Extension | Meaning |
|---|---|
| `.BAC` | a program saved by `SAVE` — tokenized, compact, loads fast |
| `.BAS` | a program saved by `LIST filename` — plain text, editable, portable |

When no extension is given, commands try `.BAC` first and then `.BAS`.
`SAVE` defaults to writing `.BAC`; `LIST` to a file defaults to `.BAS`.
`MERGE` cannot be used on a `.BAC` file (`Error 204`).

The `.BAS`/`.BAC` split is how you move a program between machines: the
manual's own advice for porting an ABC80 program to the ABC800 family is
to store it with `LIST filename` as text, load it on the newer machine,
and fix the lines that then report errors.

### Program file commands

Every one of these was run live against a disk `bin/abcdisk` formatted,
under `--interactive`, with the result confirmed by reading the directory
back afterwards:

| Command | Effect | |
|---|---|---|
| `SAVE "MO0:GAME"` | save the program, tokenized, as `GAME.BAC` | **(verified)** |
| `LOAD "MO0:GAME"` | load it back | **(verified)** |
| `RUN "MO0:GAME"` | load and run in one step | **(verified)** |
| `LIST "MO0:GAME"` | save as readable text instead, as `GAME.BAS` | **(verified)** |
| `MERGE "MO0:SUBS.BAS"` | merge another program into this one | **(verified)** |
| `UNSAVE "MO0:GAME"` | delete the file | **(verified)** |
| `KILL "MO0:GAME.BAC"` | delete, as a statement | **(verified)** |
| `NAME "MO0:OLD.BAC" AS "NEW.BAC"` | rename in place | **(verified)** |
| `CHAIN "MO0:PART2"` | in a program: load and run another | not tested |

`KILL` and `NAME` … `AS` are *statements* from the DOS ROM's own command
table at `0x6F87`, whose complete contents are exactly four entries:
`BYE`, `KILL`, `NAME`, `AS`.

**`MERGE` only works on a text file. (verified)** Against a `.BAC` it
gives `Error 204` ("MERGE cannot be used on a BAC file"), which is the
manual's own rule confirmed from both sides — the same program saved with
`LIST` instead of `SAVE` merges correctly:

```
NEW
100 PRINT "MERGED IN"
LIST "MO0:SUBST"          ! writes SUBST.BAS, text
NEW
10 PRINT "MAIN"
MERGE "MO0:SUBST.BAS"
LIST
10 PRINT "MAIN"
100 PRINT "MERGED IN"
```

Two behaviours worth knowing, both observed rather than documented:

- **`UNSAVE` and `KILL` genuinely release the file's clusters.** Deleting
  the only file on a fresh disk returns the free-list to exactly its
  as-formatted state, 24 of 640 clusters used. **(verified)**
- **Freed clusters are not reused by the next save.** After deleting a
  file at clusters 24-25 and saving a new one, the new file lands at 28,
  with 24-25 left free. The allocator appends rather than filling holes.
  **(verified)** — harmless, but it means a disk churned through many
  saves fragments rather than compacting.

`LIST` also re-indents loop bodies when it prints a program back, so a
listing is the ROM's own formatting rather than the text you typed:

```
20 FOR I%=1 TO 3
30   PRINT I%;
40 NEXT I%
```

### Listing a disk's files

**There is no `DIR` or `LIB` command in ROM. (verified)** Typing `LIB`
gives `Error 220` ("spelling error") — the ROM has no such keyword, and
the error is correct rather than a controller failure. Listing a directory
is a *program that lives on the disk*:

```
RUN "MO0:LIB"
```

**(verified)** against a real Luxor system disk, which opens `LIB`'s own
menu:

```
                       ** ABC800 LIB **
                      1 - Skrivare (Printer)
                      2 - Storlek
                      3 - Filstatus
                      4 - Viss drivenhet
                         Välj (1-4)?
```

It is interactive, so drive it under `--interactive`. Scripted `--type`
can start it but cannot reliably answer more than one of its prompts: the
DART holds a single received byte, so every keystroke typed while the
program is not reading is discarded, and `--type-at` offers only one delay
point in a run.

`BYE` is the other route — see below.

### Leaving BASIC for the DOS

`BYE` hands control to the disk's own command interpreter (`CMDINT.SYS`,
named in the DOS ROM alongside `BASICINI.SYS`), and `$BAS` hands it back.
**(verified)**, against a real Luxor system disk:

```
BYE

ABC800 DISC OPERATING SYSTEM

VERS 1.01  Feb '81

*   R E A D Y   *
```

**If `BYE` prints `Abort 48` instead, the interleave is wrong.** Error 48
is "failure in system data", and it is telling the exact truth: with the
wrong sector mapping, `CMDINT.SYS` is read from the wrong places and *is*
garbage. This document previously recorded `BYE` as broken and
undiagnosed; it was only ever the `--interleave 0` that `.dsk` images
need.

**The DOS's commands are the `.ABS` programs on the disk**, not a fixed
built-in set — an unrecognized name answers `Förstår ej` ("don't
understand"). `SYSTEM` prints the inventory of whatever the disk carries;
on a Luxor system disk that is:

| Command | Swedish | What it does |
|---|---|---|
| `COPY` | Kopiering en fil | copy one file |
| `COPYLIB` | Kopiering flera filer | copy several files |
| `DELETE` | Radering flera filer | delete several files |
| `DISCHECK` | Testning | test the media |
| `DOSGEN` | Formattering | **format a disk** |
| `ERRCOPY` | Kopiering felaktig fil | copy a damaged file |
| `LIB` | Bibliotek | directory listing |

`DOSGEN` is the machine's real formatter, and it is why the claim "there
is no `FORMAT`" needs qualifying: there is one, it just lives on a disk
rather than in ROM, so you need a working system disk to make a disk.
`bin/abcdisk` exists to break that circularity.

**`DOSGEN` does not run under `bin/abc802` today**, and the reason is
worth knowing because it is not a defect in the program. Traced on the
bus, it always selects `0x2C` — the ABC832/834 (`MF`) controller —
whatever drive number or density it is given. Every disk here that carries
`DOSGEN.ABS` is a 160K ABC830, which fits an `MO` controller at `0x2D`, and
this emulator models **one controller at a time**. So its format commands
reach no card: it prints `Skivan formatteras !` and the image is left
byte-for-byte untouched. A real ABC802 could have an ABC830 and an ABC832
on the bus together, which is what would be needed. See
`ABC802_ROADMAP.md`'s "Two drive types, one card".

**`LIB` works — on media whose controller matches.** It was never broken.
The DOS's logical drives map to select `0x2C`, the ABC832/834, so on a
160K ABC830 system disk `LIB`'s free-space read finds no card, is retried
three times and returns carry, and it skips its whole listing section.
Given a real 640K UFD-DOS system disk it does exactly what it should:

```
-LIB
** Library list **
   Ver 6.03, 1983-02-10

Drive MF0:

SYSDIR  .SYS BASICINI.SYS ADDOPT  .ABS DEVDES  .REL OPTROSH .REL OPTROSL .SYS
ISAMOPT .REL TERMOPT .REL SOFTOPT .REL CMDINT  .SYS SYSTEM  .ABS COPY    .ABS
...
 1960 av  2528 sektorer lediga.
```

Two things that settles. `DR0:` really is `MF0:` — a 640K system disk's
own `BASICINI.SYS` announces `DOS är UFD-DOS ver. 20 / DR_: motsvarar
MF_:` at boot, which is the mapping this document previously had to infer.
And a 640K disk's capacity is **2528 sectors**, i.e. all 640 clusters less
the eight-cluster system area — the number `bin/abcdisk` now agrees with,
having briefly had it wrong.

The same applies to `DOSGEN`: on a 640K system it starts and reaches its
media-verify pass, where it currently scans past the end of the modeled
drive and reports every sector beyond it as bad. That is a separate, open
issue — see `ABC802_ROADMAP.md`.

`bin/abcdisk list` reads the same directory correctly and needs no
machine, so nothing is blocked by this.

### Data files

Up to **seven** files may be open at once (`Error 19` beyond that). File
numbers run 0-255. The manual writes the file-number marker as `£`; on
this machine it is `#`.

| Statement | Effect |
|---|---|
| `OPEN "[device:]file[.ext]" AS FILE n` | open an existing file |
| `PREPARE "[device:]file[.ext]" AS FILE n` | create and open a new file |
| `CLOSE [n[,n…]]` | close; writes the end-of-file marker |
| `PRINT #n, data[,data…]` | write in ASCII format |
| `PRINT #n USING "fmt"; data` | write formatted |
| `INPUT #n, var[,var…]` | read values |
| `INPUT LINE #n, a$` | read a whole line |
| `GET #n, a$ [COUNT k]` | read k characters |
| `PUT #n, a$` | write a string |
| `POSIT #n, position` | move the file pointer (random access) |

**(verified)** `CON:` works as a file device with no media at all, which
makes it a good way to try the file statements out:

```
OPEN "CON:" AS FILE 1
PRINT#1,"VIA FILE 1"     →  VIA FILE 1
CLOSE 1
```

**(verified)** `PRINT#3,1` on an unopened file gives `Error 32`;
`OPEN "NOSUCH" AS FILE 1` gives `Error 21`.

**A random-access record is 253 bytes, not 256.** The manual's ABC80 → ABC
800 porting notes give the idiom directly:

```
POSIT #n, sector*253 : GET #n, D0$ COUNT 253     ! read sector
POSIT #n, sector*253 : PUT #n, A$                ! write sector
```

253 is 256 minus a three-byte per-sector header, which is also why `MEM:`
addresses are `number × 253`.

### How it is stored on the disk

The ABC-bus floppy system stores **256-byte sectors**. Two drive classes
are modeled, and their geometry is what makes an image file recognizable:

| Drive | Selector | Geometry | Sectors | Image size |
|---|---|---|---|---|
| ABC830 (`MO`) | `0x2D` | 40 tracks × 1 side × 16 sectors | 640 | **163,840 bytes** (160 KB) |
| ABC832 / ABC834 (`MF`) | `0x2C` | 80 tracks × 2 sides × 16 sectors | 2560 | **655,360 bytes** (640 KB) |

Two more selectors exist in the ROM's scan — `0x2E` (8-inch, `SF`) and
`0x24` (hard disk, `HD`) — but are not modeled, because their sector
interleave cannot be inferred from the two that are.

Three facts about the layout that were established the hard way and are
easy to get wrong:

- **`MO` media is sector-interleaved with factor 7; `MF` media is not.**
  The two drives interleave in *opposite* directions, each established by
  booting real media both ways. But the factor belongs to the **dump**, not
  only the drive: those defaults suit abc80.net's `.img` archive, which
  stores sectors physically, while `.dsk` images of the same media are in
  logical order and need `--interleave 0`. Nothing inside an image says
  which. The wrong choice does not look like a broken disk — the card is
  found and the directory lists correctly, and then every real file read
  gives `Error 37`.
- **A readable hex dump proves nothing about interleave.** Both formats
  keep their directory at sector 16, and track-boundary sectors map to
  themselves under either mapping, so the directory is legible even when
  every other sector is wrong. Only booting settles it.
- **The `MF` drive uses four sectors per cluster**, the `MO` drive one,
  which changes how a command header's sector address decodes.

#### How a file's extent is recorded

Established by disassembly, bus tracing, and by converting a real 160K
disk to 640K until it booted.

- **The allocation unit is the cluster, not the sector.** The descriptor's
  `last` field is `start + clusters - 1`. On the ABC830 a cluster is one
  sector, so this reads as `sectors - 1` and the distinction is invisible;
  on the ABC832 it is four. Checked against 23 of the 25 files on real
  640K media.
- **Iteration is a flat logical index.** Sector *i* of a file lives at
  cluster `start + i/spc`, offset `i mod spc`. The bus trace shows exactly
  that: on `MO` the cluster advances with the offset always 0, on `MF` the
  offset runs 0-3 and then the cluster advances.
- **The directory's byte-length field is optional.** Some disks record it
  and some leave it zero — `bin/abcdisk list` prints "(size not
  recorded)" for the latter. When present the DOS honours it exactly: set
  it too large and a load fails with `Error 38`, "sector number outside
  the file".

**A file's own sectors carry a three-byte header** — a file id, a sequence
number and a zero — leaving 253 bytes of payload each. The id is
`0x10 × (directory slot + 1)`, so moving a file to a different directory
slot invalidates every one of its sectors. The file's *first* sector is a
descriptor rather than data: `id, 00, 00, FF, last(2), FF, FF`, where
`last` is the start field plus the number of sectors after the descriptor.

**Start positions are cluster addresses, not sector numbers.** The
directory field's top 11 bits are a cluster and its low 5 bits an offset;
the real sector is `cluster × sectors-per-cluster + offset`. On the ABC830
a cluster is one sector so the two coincide, which is exactly why this is
easy to get wrong — `bin/abcdisk` reported the raw field as a sector and
was a factor of four out on ABC832 media until this was noticed. Reading a
file, the DOS steps the offset `0`-`3` and then advances the cluster.

The directory itself is a list of fixed-size entries holding an
eight-character name, a three-character extension and a start position,
kept in two copies. That description comes from this project's analysis of
**ABC80's** ABC-DOS on 160 KB media
([`ABC80_COMPLETED.md`](../../abc80/docs/ABC80_COMPLETED.md), Milestone 6),
where entry-by-entry decoding was carried out; the ABC802's UFD-DOS has
not been decoded to the same depth here, so treat the per-byte layout as
ABC80-derived rather than confirmed for this machine. What *is* confirmed
for both is the sector size, the geometry, the interleave and the
directory's position at sector 16.

### How it is stored on the host

A disk is a **raw image file**: sector 0 first, then sector 1, and so on,
with no header, no metadata and no container format. Nothing but the
sectors.

```
bin/abc802 --disk system.img                 # drive 0
bin/abc802 --disk system.img --disk work.img # drives 0 and 1
bin/abc802 --disk 1:work.img                 # pin to drive 1, no drive 0
```

- **Which controller is fitted is decided by the image's size**, not by a
  flag: 163,840 bytes selects an ABC830, 655,360 an ABC832/834. Any other
  size is refused with both expected sizes named, since that is almost
  always a truncated download.
- **All attached drives must be the same type**, because one controller is
  modeled. Mixing gives
  `Disk image 'x.img' is a mf image, but a mo controller is already fitted`.
- **Writes go straight through to the file.** Copy an image before using
  it for anything you care about; the DOS writes to media on `SAVE`, and
  the regression suites copy for exactly this reason.
- **With no `--disk`, no card is fitted at all.** Every ABC-bus read
  floats high, which is precisely the `0xFF` the ROM's boot scan reads as
  "nothing there".
- **`--interleave N` overrides the sector interleave** (0 disables it) for
  images dumped in logical rather than physical order. The startup line
  reports what is in force:
  `ABC-bus: mo floppy controller, 1 drive attached, interleave 0 (overridden)`.

An all-zero file of the right size is *not* a blank disk: it attaches and
is recognized, then fails every `SAVE` with `Error 41` ("disk space
full"), because an unformatted image has no free-list to allocate from.
**(verified.)**

**Neither ROM has a `FORMAT` command.** The machine's real formatter is
`DOSGEN`, a program on a Luxor system disk reached by leaving BASIC with
`BYE` — see [Leaving BASIC for the DOS](#leaving-basic-for-the-dos). That
means a real ABC802 needs a working system disk before it can make a disk.
`bin/abcdisk` breaks the circularity:

`bin/abcdisk` writes a real, formatted, empty one:

```
bin/abcdisk create work.dsk                # 160K ABC830
bin/abcdisk create big.dsk --type mf       # 640K ABC832/834
bin/abcdisk list work.dsk                  # what is on it
```

Its images are in logical sector order, so attach a 160K one with
`--interleave 0` — the `create` command prints the exact line to use.
See `abcbus/mkdisk.c` for the on-disk format it builds.

A complete session, start to finish:

```
$ bin/abcdisk create work.dsk
Created 'work.dsk': mo, 640 sectors, 163840 bytes, 616 free clusters
Attach it with: bin/abc802 --disk work.dsk --interleave 0

$ bin/abc802 --interactive --columns 80 --interleave 0 --disk work.dsk
ABC802
10 PRINT "HELLO FROM A BLANK DISK"
20 FOR I%=1 TO 3
30 PRINT I%;
40 NEXT I%
RUN
HELLO FROM A BLANK DISK
 1  2  3
ABC802
SAVE "MO0:HELLO"
ABC802
                                    (Ctrl-\ exits)

$ bin/abcdisk list work.dsk
work.dsk: mo, 640 sectors
  HELLO    BAC  sector 24    512 bytes
  26 of 640 clusters used, 614 free
```

**(verified)** — that is a real transcript, and a *separate* live session
then `LOAD`s, `LIST`s and `RUN`s the program back. Both drive types were
checked this way.

This repository still deliberately does not commit third-party disk
images; `ABC802_TEST_DISKS` points the regression suite at a directory of
them.

For worked, verified `SAVE`/`LOAD` round trips against real media —
including a negative control proving the drives are independent — see
[`ABC802_COMPLETED.md`](ABC802_COMPLETED.md), Milestones 5 to 7.

## Error codes

The ROM reports errors as bare numbers: `Error 130.` at the prompt, and
`Error 141 in line 10.` during a program. **(verified)** There is no
message text anywhere in the ROM — the single string `Error` at `0x3D6E`
is all there is, so the table below is the only way to read them.

Ranges, per the manual:

| Range | Class |
|---|---|
| 19-68 | I/O errors |
| 120-129 | ISAM errors (ISAM option only) |
| 130-176 | errors during program execution |
| 180-191 | logical errors |
| 200-211 | general errors |
| 220-234 | formal BASIC (syntax) errors |

Codes marked ✓ were reproduced on the real ROM in this emulator; the rest
are the manual's, transcribed but not independently confirmed.

### I/O errors

| Code | Meaning | |
|---|---|---|
| 19 | Cannot open more files (seven are open) | |
| 20 | Line overflow (> 160 characters) | |
| 21 | File not found | ✓ |
| 32 | File not opened | ✓ |
| 34 | End of file | |
| 35 / 36 | CRC or address-mark error during read / write | |
| 37 | Incorrect sector format | ✓ |
| 38 | Sector number outside the file | |
| 39 / 40 | File write-protected / delete-protected | |
| 41 | Disk space full | ✓ |
| 42 | Disk not ready (no disk, or the flap is open) | |
| 43 | Disk write-protected | |
| 44 / 45 | Logical file not opened / illegal logical file number | |
| 46 | Illegal unit number | |
| 47 | Illegal trap number | |
| 48 | Failure in system data | |
| 49 | Incorrect physical file number | |
| 51 | Unit busy | |
| 52 | Illegal device operation | |
| 53 | Function key pressed during `INPUT`/`INPUT LINE` | |
| 54-56 | IEC bus errors (IEC option) | |
| 57 | Character from keyboard too late | |
| 58 | Invalid character loaded | |
| 64 | Incorrect `NAME` (the new name already exists) | |
| 68 | Incorrect time specification | |

### Execution errors

| Code | Meaning | |
|---|---|---|
| 130 | Floating point overflow | ✓ (`1/0`, `1E30*1E30`) |
| 131 | Index outside array | ✓ |
| 132 | Integer overflow | ✓ |
| 133 | Error in ASCII arithmetic expression | |
| 134 | Index outside string | ✓ |
| 135 | Index too great or negative | |
| 136 | Negative `SPACE$`/`STRING$`, or `TAB` < 1 | |
| 137 | Extending `DIM` not permitted | ✓ |
| 138 | String too long / receiving string too small | |
| 139 | Incorrect value in `ON` expression | |
| 140 | `RETURN` without `GOSUB` | ✓ |
| 141 | End of data — `READ` with no `DATA` left | ✓ |
| 142 | Incorrect argument in function | ✓ (`SQR(-1)`, `LOG(0)`) |
| 143 | Incorrect `SYS` function | ✓ |
| 144 | Invalid line | |
| 145 | `FNEND` not preceded by `RETURN` | |
| 146 | `PRINT USING` error | ✓ |
| 147 | Wrong data | |
| 148 | Too little input data | |
| 149 | `RESTORE` not on a `DATA` line | |
| 150 | Too much input data | |
| 151 | `RESUME` without error | |
| 176 | Graphic dot outside screen | ✓ |

### Logical errors

| Code | Meaning | |
|---|---|---|
| 180 | Cannot find this line number | ✓ |
| 181 | Incorrect jump into function | |
| 182 | `NEXT` or `WEND` missing | |
| 183 | `FOR` or `WHILE` missing | |
| 184 | Wrong variable after `NEXT` | |
| 185 | Mixed `FOR` loops with the same variable | |
| 186 | `FOR` loop with a local variable not permitted | |
| 187 | Function not defined | |
| 188 | More than one function with the same name | |
| 189 | Incorrect function (mixing several `DEF`s) | |
| 190 | Wrong number of indexes | |
| 191 | Not allocable (in a function) | |

### General errors

| Code | Meaning | |
|---|---|---|
| 200 | Unit not connected | |
| 201 | End of memory | |
| 202 | `LIST`-protected program | |
| 203 | Incorrect program format | |
| 204 | `MERGE` cannot be used on a `.BAC` file | |
| 205 | `COMMON` error | |
| 206 | Use `RUN` command | ✓ (calling a `DEF FN` before running) |
| 207 | Cannot continue | ✓ (`CON` with nothing to continue) |
| 208 | Invalid as a command | ✓ (`NEXT`, `WHILE`, `WEND` typed directly) |
| 209 | Wrong data with command | |
| 210 | Incorrect number | ✓ (`VAL("ABC")`) |
| 211 | Precision must not be changed | ✓ (`DOUBLE` after an assignment) |

### Formal BASIC errors

| Code | Meaning | |
|---|---|---|
| 220 | Spelling error | ✓ (`QWERTY`, `LIB`, an unknown function) |
| 221 | Illegal character after statement | |
| 222 | Must be first on a line | |
| 223 | Wrong number or types of arguments | ✓ (`PRINT CHR$`, `17 MOD 5`) |
| 224 | Illegal mixture of numbers and strings | ✓ (`B=A$`) |
| 225 | Not a single variable | |
| 226 | Wrong statement after `ON` | |
| 227 | `,` missing | |
| 228 | `=` missing | |
| 229 | `)` missing | |
| 230 | `AS FILE` missing (in `OPEN`/`PREPARE`) | |
| 231 | `AS` missing (in `NAME … AS …`) | |
| 232 | `TO` missing (in `FOR`) | |
| 233 | Line number missing | |
| 234 | Wrong variable | |

## Differences from ABC80 BASIC

BASIC II is an extension of ABC80 BASIC and is largely compatible with it.
What is genuinely new or different, restricted to things confirmed here:

**New in BASIC II**

- `WHILE` / `WEND` loops.
- Multi-line `DEF FN` with `LOCAL` variables, `RETURN value` and `FNEND`.
- `RESUME`, to return from an error handler. (`ON ERROR GOTO` and
  `ERRCODE` already exist on ABC80; resuming from the handler does not.)
- `PRINT USING` with format strings.
- `EXTEND` — long variable names.
- `DOUBLE` / `SINGLE` / `DIGITS` — 16-digit floating point on request.
- `INTEGER` / `FLOAT` — change the default type of undeclared names.
- `OPTION BASE`, `COMMON`.
- `HEX$`, `OCT$`, `MOD`, `TIME$`, `SYS`, `VARPTR`, `VAROOT`, `PEEK2`,
  `POSIT`, and the `CVT` family.
- `GET #` / `PUT #` / `POSIT #` — random access on files.
- `WIDTH` — 40/80 columns from software (ABC802-specific; it is in the
  ROM's extension table at `0x4BFA`, not the main statement table).
- The attribute and colour words.

**Changed**

- `**` accepts a non-integer exponent. On ABC80 the exponent had to be an
  integer variable or literal.
- `SET DOT`'s position range follows `WIDTH` (0-79 or 0-159), against
  ABC80's fixed 0-79 on a 40-column screen. `TXPOINT` is new, and uses the
  opposite origin corner.
- The line editor is *simpler*, not richer: ABC80 has a non-destructive
  cursor-right at `0x09` and the ABC802 has nothing equivalent.
- `CHAIN ""` becomes `CHAIN "NUL:"` or `END`.
- `END` should be the only statement on its line; it closes files but does
  not clear variables.

**Absent**

- All bitmap/high-resolution graphics (`FGCTL`, `FGFILL`, `FGLINE`,
  `FGPAINT`, `FGPOINT`) — the ABC802 has no bitmap mode.

**Not a difference, despite appearances.** ABC80's one-word `SETDOT`,
`CLRDOT` and `INPUTLINE` all work here unchanged, because BASIC II ignores
spaces *inside* keywords — see [Free format](#free-format) above. They are
simply re-listed under this machine's own spelling.

## How the keyword tables were read

Every keyword list above was extracted from the committed ROM images
rather than typed out of the manual, which is why they can be trusted to
match what `bin/abc802` will actually accept.

The tables live in the first BASIC ROM (`ABC802-basic.02-11.bin`, mapped
at `0x0000`) and use one format throughout: **a token byte with bit 7 set,
immediately followed by the keyword's ASCII characters**, with `0xFF`
closing a group. Two synonyms are stored as two entries carrying the same
token, which is how `NEW`/`SCR`, `RENUMBER`/`REN`, `LEFT`/`LEFT$` and
`ASC`/`ASCII` are identifiable as synonyms rather than merely similar.

| Table | Address | Contents |
|---|---|---|
| Operators | `0x0625`-`0x0661` | 18 entries, in nine precedence groups |
| Functions | `0x0676`-`0x079A` | 56 entries |
| Attributes | `0x0810`-`0x088C` | 25 entries |
| Statements | `0x08A5`-`0x0A22` | 62 entries, statements then secondary keywords |
| Commands | `0x4057`-`0x40B2` | 17 entries |
| Extension | `0x4BFA`-`0x4C0B` | 1 entry (`WIDTH`) |
| Device names | `0x6E37`-`0x6EB4` | 16 entries, in the DOS ROM |
| DOS commands | `0x6F87`-`0x6F99` | 4 entries (`BYE`, `KILL`, `NAME`, `AS`) |

Two things fell out of that format for free. The operator table's `0xFF`
separators are **precedence group boundaries**, which is where the
precedence table in this document comes from — read out of the ROM, not
inferred from testing. And the attribute words' tokens are exactly their
character codes plus `0x80`, which is what made the attribute table
derivable and then checkable with `PEEK`.

## Sources

- **Luxor Datorer AB, *ABC 800 BASIC II*** (English edition, © 1984,
  document 70876) —
  <https://www.abc80.net/archive/luxor/ABC80x/ABC800-manual-BASIC-II.pdf>.
  155 pages with an OCR text layer. Chapter 15 is the error-message table,
  chapter 16 the alphabetical syntax summary, chapter 14 the ABC80
  differences, and **Appendix 5 the ABC 802 differences** — the last of
  which is what makes this manual usable for this machine rather than only
  for the ABC 800.
- **The committed ROM images**, `abc802/resources/rom/` — provenance and
  per-chip CRC32/SHA1 verification against MAME in that directory's own
  `README.md`.
- **Direct execution** of those ROMs through `bin/abc802`, for everything
  marked **(verified)**.
- [`ABC802_REFERENCE.md`](ABC802_REFERENCE.md) — the hardware side of
  everything above: the character-generator attribute decode, the 40/80
  column mechanism, the line editor's byte-level vocabulary, the ABC-bus
  protocol and status byte.
- [`ABC802_COMPLETED.md`](ABC802_COMPLETED.md) — the milestone write-ups,
  including the verified disk round trips and the interleave experiments.
