# Disassembler Reference (`z80dasm`)

Reference for `bin/z80dasm` (`disasm/src/`). For a walk through using it
with the assembler and the debugger, see [`TOOLCHAIN.md`](TOOLCHAIN.md);
for how it is built, see `CLAUDE.md`'s Disassembler section. This document
describes the tool as it exists today.

## Usage

```
z80dasm <input> [-o origin] [-l length]
```

Loads the file at `origin` and prints a listing on standard output.

| Option | Default | Meaning |
|---|---|---|
| `-o origin` | `0100h` | the address the file's first byte is loaded at, and where decoding starts |
| `-l length` | the whole file | decode only this many bytes |

**`-o` is not the assembler's `-o`.** In `z80asm` it names the output file;
here it is the load address.

**Numbers follow C's rules, not the rest of the toolchain's.** `0x100`,
`100h` and `256` all mean 0x100, but **a leading zero means octal**: `-o
0100`, the way a Z80 programmer writes the CP/M origin, is octal 100 and
loads the file at `0040h`, with no warning. The listing's own first line
(`org 0040h`) is the only sign. Write `100h` or `0x100`. (The debugger
reads a bare number as hex, so the two tools differ here too.)

## What it prints

```
        org 0100h

        LD DE,0134h             ; 0100: 11 34 01
        LD C,09h                ; 0103: 0E 09
        CALL 0005h              ; 0105: CD 05 00
        ...
L010D:
        INC HL                  ; 010D: 23
        DJNZ L010D              ; 010E: 10 FD
        ...
        DB 48h                  ; 0134: 48
        DB 65h                  ; 0135: 65
```

One instruction per line, in `z80asm` syntax, with a comment giving its
address and bytes.

**Only code it can reach is decoded.** Decoding starts at `origin` and
follows every jump, call, `JR`, `DJNZ` and `RST` target, and each
instruction's fall-through unless it is an unconditional `JP`, `JR`, `RET`,
`RETI`, `RETN` or `JP (HL)`/`(IX)`/`(IY)`. Every byte never reached that
way prints as a single `DB`, one per line, which is how data such as the
message strings above comes out. This is what keeps data from being
mis-decoded as instructions.

The limit is the usual one for this kind of disassembler: **code reached
only through a computed jump** (`JP (HL)`, a jump table, an `RST` vector
nothing in the file calls) has no target to follow, so it prints as `DB`.
The ABC80 ROM's jump table at `0002` is an example: `JR 0068h` at `0000`
is decoded and the table beside it is not. The debugger's `u` command
decodes whatever address it is given, so it is the way to read such code.

**Labels.** A jump or call target inside the file becomes `Lxxxx`, and a
`(nn)` memory operand inside the file becomes `Dxxxx`, for example
`LD (D0214),HL`. Targets outside the file (`CALL 0005h`, CP/M's BDOS)
and immediate values (`LD DE,0134h`, even when that is an address) are
left as numbers, since nothing marks them as addresses.

**Undecodable bytes** (most of the unassigned `ED` range) print as `DB`, so
the tool never stops or skips.

## Reassembling the output

The listing is valid `z80asm` input. Across all twelve
`asm/examples/*.asm` programs, assembling, disassembling and assembling the
listing again gives byte-identical output for eleven. The twelfth,
`gaps_test.asm`, deliberately contains the undocumented duplicate
encodings of `IM 0`/`1`/`2`: `ED 66`, `ED 76` and `ED 7E` disassemble as
`IM 0`, `IM 1` and `IM 2` and reassemble as the documented `ED 46`,
`ED 56` and `ED 5E`. It is the same instruction in different bytes. (The
duplicates `ED 4E` and `ED 6E` print as `DB` and so do round-trip.)

## Disassembling a ROM

A ROM starts at `0000`, so give the origin explicitly:

```
z80dasm abc80/resources/rom/3506_3.a5.bin -o 0
```

A ROM made of several images, like the ABC80's four, is disassembled one
image at a time; references into the other images stay as plain numbers,
since only addresses inside the file get labels.
