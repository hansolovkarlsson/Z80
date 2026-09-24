# Toolchain Guide

A walk through the three tools together: write a Z80 program, assemble it
with `z80asm`, run it with `z80`, read it back with `z80dasm`, and step
through it in the debugger. Every command and every line of output below
was run as shown.

The references hold the details this guide skips:
[`ASSEMBLER.md`](ASSEMBLER.md) for the source syntax,
[`DISASSEMBLER.md`](DISASSEMBLER.md) for the listing, and
[`DEBUGGER.md`](DEBUGGER.md) for every command.

## Setting up

Build everything and put `bin/` on your `PATH`. `scripts/config.sh` takes
the repo root from the directory it is sourced in, so **source it from the
repo root**:

```
cd path/to/Z80
make
source scripts/config.sh
```

Then work somewhere of your own. `z80` creates a `cpm_disk/` directory
wherever it runs, for the files a CP/M program opens, so a scratch
directory keeps that out of the checkout:

```
mkdir -p ~/z80-work && cd ~/z80-work
cp $BASEDIR/asm/examples/hello.asm .
```

## The program

`hello.asm` prints a message, counts to five in `HL` with a `DJNZ` loop,
checks the count, and prints it:

```
BDOS:   equ 5

        org 100h

start:  ld de, msg
        ld c, 9
        call BDOS               ; print string

        ld b, 5
        ld hl, 0
count_loop:
        inc hl
        djnz count_loop         ; HL should end up holding 5

        ld a, l
        cp 5
        jp nz, fail

        add a, '0'
        ld e, a
        ld c, 2
        call BDOS               ; print '5'

        ld de, crlf
        ld c, 9
        call BDOS

        jp 0                    ; warm boot

fail:   ld de, failmsg
        ld c, 9
        call BDOS
        jp 0

msg:    db 'Hello from z80asm!$'
crlf:   db 13, 10, '$'
failmsg: db 'FAIL: loop counter wrong$'
```

`org 100h` is where CP/M loads a program, and `call 5` is how a CP/M
program asks the operating system for something: function 9 prints the
`$`-terminated string at `DE`, function 2 prints the character in `E`, and
`jp 0` returns to the system.

## Assemble

```
$ z80asm hello.asm -o hello.com
```

It prints nothing when it succeeds. `hello.com` is 99 bytes: the code and
data from `0100h` to the last byte of `failmsg`, and nothing else.

## Run

```
$ z80 hello.com
Loaded 'hello.com' (99 bytes) at 0x0100
Starting Z80 Execution Loop...

Hello from z80asm!5

Program terminated normally at PC=0x0000.
Finished. Total T-states executed: 241
```

`z80` provides the CP/M calls itself, so the program runs without a real
CP/M. The last line is the total run time in Z80 clock cycles.

## Disassemble

```
$ z80dasm hello.com
        org 0100h

        LD DE,0134h             ; 0100: 11 34 01
        LD C,09h                ; 0103: 0E 09
        CALL 0005h              ; 0105: CD 05 00
        LD B,05h                ; 0108: 06 05
        LD HL,0000h             ; 010A: 21 00 00
L010D:
        INC HL                  ; 010D: 23
        DJNZ L010D              ; 010E: 10 FD
        LD A,L                  ; 0110: 7D
        CP 05h                  ; 0111: FE 05
        JP NZ,L0129             ; 0113: C2 29 01
        ADD A,30h               ; 0116: C6 30
        ...
L0129:
        LD DE,014Ah             ; 0129: 11 4A 01
        LD C,09h                ; 012C: 0E 09
        CALL 0005h              ; 012E: CD 05 00
        JP 0000h                ; 0131: C3 00 00
        DB 48h                  ; 0134: 48
        DB 65h                  ; 0135: 65
        ...
```

Set it beside the source. The labels are gone, so `z80dasm` makes its own
for the places something jumps to: `count_loop` is now `L010D` and `fail`
is `L0129`. `LD DE,0134h` stays a number, because nothing in the
instruction says `0134h` is an address rather than a value. The message
text comes out one `DB` per byte: the disassembler decodes only what it
can reach by following the code from `0100h`, and nothing jumps into a
string.

To stop before the data, give a length: `z80dasm hello.com -l 34h` ends at
the `JP 0000h` at `0131`. Write lengths and origins with `h` or `0x`: a
leading zero means octal here, so `-o 0100` would load the file at
`0040h`.

The listing is itself `z80asm` source, and assembling it gives the same
bytes back:

```
$ z80dasm hello.com > round.asm
$ z80asm round.asm -o round.com
$ cmp hello.com round.com
```

`cmp` prints nothing: the files are identical.

## Debug

`--debug` stops before the first instruction and waits for commands. The
session below is typed at that prompt. First, a look at the loop:

```
$ z80 --debug hello.com
Loaded 'hello.com' (99 bytes) at 0x0100
Starting Z80 Execution Loop...

[stopped] 0100  11 34 01     LD DE,0134h
(z80dbg) u 0108 5
0108  06 05        LD B,05h
010A  21 00 00     LD HL,0000h
010D  23           INC HL
010E  10 FD        DJNZ 010Dh
0110  7D           LD A,L
```

`u` disassembles from any address. A breakpoint on the loop's first
instruction, and `c` to run to it:

```
(z80dbg) b 010D
breakpoint 010D
(z80dbg) c
Hello from z80asm![breakpoint] 010D  23           INC HL
```

The program printed its greeting on the way. It printed no newline, so the
debugger's line follows straight on. Now step through the loop, two
instructions at a time:

```
(z80dbg) s 2
010D  23           INC HL
AF=0000 BC=0509 DE=0134 HL=0001 IX=0000 IY=0000 SP=F000 PC=010E  ........  IM0 DI
010E  10 FD        DJNZ 010Dh
AF=0000 BC=0409 DE=0134 HL=0001 IX=0000 IY=0000 SP=F000 PC=010D  ........  IM0 DI
[breakpoint] 010D  23           INC HL
(z80dbg)
010D  23           INC HL
AF=0000 BC=0409 DE=0134 HL=0002 IX=0000 IY=0000 SP=F000 PC=010E  ........  IM0 DI
010E  10 FD        DJNZ 010Dh
AF=0000 BC=0309 DE=0134 HL=0002 IX=0000 IY=0000 SP=F000 PC=010D  ........  IM0 DI
[breakpoint] 010D  23           INC HL
```

Each step shows the instruction, then the registers after it. `INC HL`
counts up; `DJNZ` counts `B` down (the `05` in `BC=0509`, then `04`, then
`03`) and jumps back while it is not zero. The empty line repeated `s 2`.

Enough of the loop. Delete that breakpoint, set one just after the loop,
and look at the result:

```
(z80dbg) d 010D
breakpoint 010D deleted
(z80dbg) b 0110
breakpoint 0110
(z80dbg) c
[breakpoint] 0110  7D           LD A,L
(z80dbg) r
AF=0000 BC=0009 DE=0134 HL=0005 IX=0000 IY=0000 SP=F000 PC=0110  ........  IM0 DI
AF'=0000 BC'=0000 DE'=0000 HL'=0000 I=00 R=0F
```

`HL` is 5 and `B` is 0, as the source says they should be. The debugger
can also change them. Set `HL` to 4 and let the program's own check find
it:

```
(z80dbg) r hl=4
AF=0000 BC=0009 DE=0134 HL=0004 IX=0000 IY=0000 SP=F000 PC=0110  ........  IM0 DI
(z80dbg) c
FAIL: loop counter wrong
Program terminated normally at PC=0x0000.
Finished. Total T-states executed: 206
```

That is the `fail` branch at `0129`, taken because `CP 5` no longer
matched.

Three more things worth knowing:

- **`n` steps over a call.** On `CALL 0005h` it runs the whole CP/M call
  and stops after it. Stepping into one with `s` is less useful: the step
  at `0005` runs the entire CP/M call and the instruction after it at once,
  since `z80` provides CP/M itself rather than as Z80 code.
- **`w addr` stops when memory changes**, and names the instruction that
  wrote it.
- **The same session can be scripted.** Put the commands in a file and run
  `z80 --debug --debug-script cmds.txt hello.com`; the commands are echoed
  so the output reads like the session above. When the file runs out, the
  program carries on to its end.

## The same tools on a ROM

The ABC machines run real ROMs, and the same two tools read them. A ROM
starts at `0000`, so give the disassembler that origin:

```
$ z80dasm abc80/resources/rom/3506_3.a5.bin -o 0
        org 0000h

        JR L0068                ; 0000: 18 66
        DB 0C3h                 ; 0002: C3
        DB 0EAh                 ; 0003: EA
        DB 02h                  ; 0004: 02
        ...
```

The bytes at `0002` are a jump table, `JP 02EAh` and so on, that the ROM
enters through computed jumps. The disassembler cannot follow those, so it
prints bytes; `u 0002` in the debugger decodes them as instructions.

The debugger works the same way on every machine. Here the ABC802 boots,
stops at `3A76`, and is let go to finish its boot and answer a line of
BASIC (commands from a script, as above):

```
$ abc802 --break 3A76 --debug-script cmds.txt --screen --type $'PRINT 6*7\r'
ABC802: loaded 32K ROM from 'abc802/resources/rom' (DOS ROM 'ABC802-dos.32-31.bin')
[breakpoint] 3A76  F5           PUSH AF
(z80dbg) u 3A76 4
3A76  F5           PUSH AF
3A77  E5           PUSH HL
3A78  21 F5 FF     LD HL,0FFF5h
3A7B  34           INC (HL)
(z80dbg) r
AF=0054 BC=0000 DE=00E9 HL=74B5 IX=FF4C IY=FF00 SP=F4D4 PC=3A76  .Z.H.P..  IM2 DI
AF'=0000 BC'=0000 DE'=F4D8 HL'=FF7B I=FF R=22
(z80dbg) d all
all breakpoints deleted
(z80dbg) c
...
|ABC802                                  |
|PRINT 6*7                               |
| 42                                     |
```

The registers say what this code is. Interrupts are off (`DI`), the CPU
is in interrupt mode 2, and the routine saves registers and adds one to a
byte at `FFF5`: an interrupt handler counting ticks. Left in place, this
breakpoint fires about 90 times per second of emulated time, which fits
the ABC802's 93.75 Hz clock interrupt. That is an inference from the
numbers, not something read out of a manual, which is exactly the kind of
question these tools are for.

(Run the `abc802` example from the repo root, since it finds its ROMs by a
relative path. The `\r` in `--type` is the Return key: without it BASIC
echoes the line and never runs it.)
