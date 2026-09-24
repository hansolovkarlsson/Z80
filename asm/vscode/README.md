# z80asm for VS Code

Syntax colouring and completion for source written for this repository's
assembler, `bin/z80asm` ([`docs/ASSEMBLER.md`](../../docs/ASSEMBLER.md)).
It covers `.asm`, `.z80`, `.mac`, `.inc` and `.sym` files.

## Install

```
asm/vscode/install.sh
```

It packages the extension and installs it with `code --install-extension`.
Rerun it after changing anything here: VS Code keeps its own copy.

## What it does

**Colouring** of mnemonics, directives, registers, condition codes, the
word operators (`low`, `high`, `eq` ...), numbers in every form the
assembler reads (`0FFh`, `0xFF`, `$FF`, `1100b`, `123`), `$` as the
location counter, strings, comments, macro arguments (`&name`), and the
names a line defines: labels, `EQU` constants and macros. It follows the
assembler's own reading where that is unusual: a `'` after a letter, digit
or `_` does not open a string, which is what keeps `EX AF,AF'` from
swallowing the rest of the line.

**Completion** of mnemonics, directives, registers and conditions, and of
the labels, constants and macros defined in the file and in everything it
`INCLUDE`s (paths relative to the including file, as the assembler resolves
them), each with where it is defined. At the start of a statement the
instructions come first; in operands, the file's own names do. Mnemonics
are offered in the case the file already uses.

## Limits

- A label **without a colon** is only coloured as one in column 0. The
  assembler also accepts an indented one, but so indented a word is far
  more often a macro call, which the grammar cannot tell apart.
- Completion cannot evaluate `IF`, so a label inside a false branch is
  still offered.
- Names inside a `MACRO` or `REPT` body are not offered: each expansion
  defines its own copies.

## How it is kept honest

`test/run_tests.sh` (part of `make test`, through the CP/M suite) runs on
VS Code's own bundled Node and TextMate engine, so it needs no other
install and tests the grammar with the code that will run it. It checks:

- **the keyword lists against the assembler's source**, both ways. They are
  a copy of `asm/src`'s, written into `build_grammar.py`, and the check is
  what stops them drifting: its first run found `SL1`, an alias the lists
  had missed.
- **the symbol scanner against `z80asm -s`** on every example and on
  `zexall.mac`: the same labels and constants, no more and no fewer.
- **the grammar**, by tokenising real lines and asserting on their scopes.

The grammar and `keywords.json` are generated: edit `build_grammar.py` and
run `python3 asm/vscode/build_grammar.py`.
