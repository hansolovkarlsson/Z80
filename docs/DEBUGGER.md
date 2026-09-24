# Debugger Reference (`--debug`)

Command reference for the Z80 debugger built into all four machine CLIs:
`bin/z80`, `bin/abc80`, `bin/abc802` and `bin/abc806`. For how it is built
and why, see `CLAUDE.md`'s Debugger section; for what is planned next, see
`cpm/docs/ROADMAP.md`'s Phase 4. This document describes the debugger as it
exists today. [`TOOLCHAIN.md`](TOOLCHAIN.md) has a worked session.

## Starting it

Three options, the same on every CLI:

| Option | Effect |
|---|---|
| `--debug` | stop before the first instruction |
| `--break ADDR` | stop when PC reaches ADDR (hex, or a symbol); repeatable, and implies the debugger |
| `--debug-script FILE` | read commands from FILE instead of the terminal |
| `--symbols FILE` | name addresses from FILE, as written by `z80asm -s`; repeatable |

```
bin/z80 --debug cpm/cpm_disk/hello.com
bin/abc802 --break 3A76 --screen
bin/abc806 --break 3A6A --debug-script cmds.txt --screen
```

On `bin/z80` the options go **before** the program, since everything after
it is the program's own command line: `bin/z80 --break 0105 prog.com ARG`.

Commands are read from the terminal (`/dev/tty`), not from standard input,
because a CP/M program reads standard input as its console. At the prompt
the terminal has normal line editing, even while the machine has it in raw
mode. Debugger output goes to standard error, so it stays separable from
what the program prints.

With `--debug-script`, each command is echoed as it runs, so the log reads
like a session. **At the end of the file the debugger detaches** and the
run continues undisturbed, so a script can never leave a run waiting for
input. End it with `q` to stop the run there instead.

**Ctrl-C** during a debugged run stops at the next instruction and opens the
prompt. Without a debug option, Ctrl-C behaves as it always has. Under an
ABC machine's `--interactive`, where Ctrl-C belongs to BASIC, the key is
**Ctrl-]** instead; see below.

### Under `--interactive`

`bin/abc80`, `bin/abc802` and `bin/abc806` take the debug options with
`--interactive` too:

```
bin/abc802 --interactive --symbols abc802/resources/rom/abc802.sym
```

Any debug option turns the debugger on, so `--symbols` alone gives a live
session that runs undisturbed until **Ctrl-]** is pressed. That key is the
terminal's interrupt character for the session (telnet uses the same one
to reach its own prompt), so it stops the machine at the next instruction
whatever the program is doing, including when nothing is reading the
keyboard. Ctrl-C still reaches BASIC and Ctrl-\ still exits; the sign-on
line says which keys do what.

The prompt appears below the last frame drawn, with line editing, and the
screen stops updating while it is open. `c` resumes, and the next frame
replaces the debugger's text. **Time at the prompt does not count as the
machine's time**: execution is paced against the wall clock, and the
pacing subtracts the seconds spent stopped, so a machine resumed after a
minute carries on at 3 MHz instead of running flat out to catch the minute
up. `s` still works, though a frame drawn during a long `s` clears the
steps printed before it.
## Commands

Addresses and values are hex (`1234`, `0x1234`, `$1234` and `1234h` are
all accepted). Counts are decimal. An empty line repeats the last `s`,
with its count, or `n`.

| Command | Effect |
|---|---|
| `s [n]` | step n instructions (default 1), showing each and the registers after it |
| `n` | step, running a `CALL` or `RST` through to its return |
| `c` | continue until a breakpoint, a watchpoint, Ctrl-C or the end of the run |
| `b [addr...]` | set breakpoints, or list them |
| `d addr` / `d all` | delete one breakpoint, or all of them |
| `w [addr [len]]` | stop after any instruction that changes those bytes (default 1), or list watches |
| `r` | show all registers, the alternate set, `I` and `R` |
| `r reg=val` | set one: `a f b c d e h l i r af bc de hl ix iy sp pc af' bc' de' hl'` |
| `m addr [len]` | hex and ASCII dump (default 64 bytes) |
| `e addr byte...` | write bytes (hex) starting at addr, then show them |
| `u [addr] [n]` | disassemble n instructions (default: from PC, 8) |
| `q` | end the run |
| `h` | help, including the machine's memory spaces |

On the ABC802 and ABC806, `m`, `e` and `w` also take `space:offset`
(`m chr:0 80`, `w plane:7209`) for memory the machine keeps outside the
64K the CPU addresses; see [Memory spaces](#memory-spaces).

A stop names its reason and the instruction about to run:

```
[breakpoint] 0105  CD 05 00     CALL 0005h
(z80dbg) r
AF=0000 BC=0009 DE=0134 HL=0000 IX=0000 IY=0000 SP=F000 PC=0105  ........  IM0 DI
AF'=0000 BC'=0000 DE'=0000 HL'=0000 I=00 R=02
```

The reasons are `stopped` (after `--debug` or a finished step),
`breakpoint`, `returned` (the end of an `n`), `interrupted` (Ctrl-C), and,
for watchpoints, a `[watch]` line naming the byte, its old and new value,
and the address of the instruction that wrote it.

The flags read `SZYHXPNC`, with a dot for each clear bit. `Y` and `X` are
the undocumented bits 5 and 3.

## Symbols

`z80asm -s hello.sym` writes a program's symbols, and `--symbols
hello.sym` gives them to the debugger. Then:

- **Any address can be a name**, or a name plus or minus a hex offset:
  `b count_loop`, `u fail`, `m msg 20`, `u count_loop+3`, `r pc=start`,
  and `--break count_loop` on the command line.
- **A labelled address is shown by name**: a `count_loop:` line above it
  in `u` and step output, `[breakpoint] count_loop:` at a stop, and
  `breakpoint 010D count_loop` from `b`.
- **Jump, call and `(nn)` targets are named in the instruction**, as
  `DJNZ count_loop` or `JP NZ,fail`. Immediate values stay numbers, as in
  `z80dasm`: nothing in `LD DE,0134h` says `0134h` is an address.

Only **labels** name addresses in output. An `EQU` can be typed as an
address (`b BDOS`), but is never shown in place of a number, since it may
be a count or a character that only happens to equal some address. A file
written by hand without the `; label`/`; equ` comments is read as labels.

**The ABC ROMs have symbol files too**, in each machine's
`resources/rom/`: `abc80.sym` (with `abc80-abcdos.sym` for the DOS ROM),
`abc802.sym` and `abc806.sym`. There is no source for a ROM, so these are
written by hand, from addresses the repo's documents state and the ROM
bytes confirm.

A name wins over a number spelled the same way: with a label called
`beef`, `b beef` means the label. `$BEEF`, `0xBEEF` and `0BEEFh` always
mean the number, since no name starts with `$` or a digit.

```
$ z80asm hello.asm -o hello.com -s hello.sym
$ z80 --symbols hello.sym --break count_loop hello.com
[z80dbg: 7 symbols from hello.sym]
Loaded 'hello.com' (99 bytes) at 0x0100
Starting Z80 Execution Loop...

Hello from z80asm![breakpoint] count_loop:
010D  23           INC HL
(z80dbg) u count_loop 2
count_loop:
010D  23           INC HL
010E  10 FD        DJNZ count_loop
(z80dbg) b fail
breakpoint 0129 fail
```

### In the GTK windows

`bin/abc80-gtk`, `bin/abc802-gtk` and `bin/abc806-gtk` take the same debug
options. **The prompt is the terminal the window was started from**, and
the window stays live while the machine is stopped: it keeps repainting
and responding, because the debugger runs in an async mode where a stop
opens the prompt and returns instead of waiting for a command. The window
watches the terminal and hands each typed line to the debugger, which
runs it with the same code as the blocking prompt.

```
bin/abc802-gtk --symbols abc802/resources/rom/abc802.sym
```

**Ctrl-C in that terminal** stops the machine, and so does **Ctrl-] in
the window**. With a debug option Ctrl-C no longer closes the window;
File > Quit, closing it, or `q` at the prompt do. Time at the prompt is
subtracted from the pacing, as under `--interactive`.

`--screenshot` runs under the debugger too, stepping through the same
function the window's timer does, and waiting on the prompt at a stop;
that is how the async path is tested without opening a window. On
`bin/abc80-gtk`, the File menu's Save and Load of a `.bas` program run the
machine themselves to type and list the lines, and are refused while it
is stopped. The `.bac` forms only copy memory and work at any time.

A window started from the Finder has no terminal, so a debug option there
fails at start-up with the same message the CLIs give; `--debug-script`
works without one.

## What it sees

**A plain address is the flat 64K array**, the bytes instruction fetch
reads, never the machine's bus hooks. Reading through the hooks would
change the machine being inspected: on the ABC806, reading character RAM
latches that cell's attribute byte. So `m 7800` on an ABC802 shows the ROM
code fetched there, not the character RAM a data read at that address
gets. Disassembly is faithful, since fetch reads the same array. What a
machine diverts elsewhere is reached through its memory spaces, below.

`e` writes to the same array. That makes it a way to **patch code**,
including a ROM image, since the array is what the CPU fetches from, and
it bypasses the machines' write hooks, so a ROM that is read-only to the
program is writable from the prompt. Every byte on the line is checked
before any is written, so a typo writes nothing, and a write to a watched
byte does not trigger the watch.

### Memory spaces

A machine whose bus diverts accesses out of the flat array registers that
memory as a named space, and `m`, `e` and `w` take `name:offset` for it.
The offset is hex and counts from the start of that memory, not from a
CPU address; symbols do not apply, since a symbol file describes the 64K.
A dump or watch stops at the end of a space rather than wrapping, and an
`e` that would run past it writes nothing. `h` lists the spaces a machine
has.

| Machine | Space | Size | What it is |
|---|---|---|---|
| ABC802 | `chr` | 2K | character RAM: what a data read at `7800`-`7FFF` gets |
| ABC802 | `lowram` | 32K | the RAM at `0000`-`7FFF`, which LRS swaps out while ROM is resident |
| ABC806 | `chr` | 2K | character RAM, as on the ABC802 |
| ABC806 | `attr` | 2K | attribute RAM, one byte per character cell |
| ABC806 | `plane` | 128K | the high-resolution video RAM, 32K per bank |

**A space is read without the side effects of a CPU access.** Each
machine supplies its own peek and poke, which touch the storage directly
and never go through its bus hooks, so looking at `chr` on the ABC806 does
not move the attribute latch. `debugger-spaces` in the ABC806 suite pins
this: printing coloured text while peeking at character RAM after every
write renders the same colours as a run without the debugger, and a peek
made to latch turns that text white. `lowram` finds the RAM wherever LRS
currently has it, in the flat array or set aside, so `e lowram:0 AA` while
ROM is resident leaves `m 0` showing ROM.

A plane watch is how to find what draws a pixel: `w plane:0 30720`
covers bank 0's visible area, and the `[watch]` line names the plane
offset and the instruction that wrote it:

```
(z80dbg) w plane:0 30720
watching plane:00000, 30720 bytes
(z80dbg) c
[watch] plane:07209: 00 -> 0F, written by the instruction at 7E31
```

A watch that large checks every byte after every instruction, so it slows
the run while it is set.

**`n` watches the stack, not the return address.** It stops at the first
instruction where SP is back to its value before the call. On a plain
machine the two agree, but CP/M's BDOS emulation runs an intercepted call
and the instruction it returns to as one step, so the return address is
never seen. For the same reason, **`s` at `0005` on `bin/z80`** reports
`JP` to the BDOS entry but runs the whole BDOS call and the instruction
after it; `n` over the `CALL 0005h` is the natural way past one.

## Not yet

- `bin/z80-gtk` passes its options through to `bin/z80`, which runs on the
  window's own terminal, so `--debug` there should put the prompt inside
  the window. It has not been tried.
- The live GTK windows' own pieces (the terminal watch, Ctrl-] in the
  window, `q` closing it) are checked by hand only; see
  [In the GTK windows](#in-the-gtk-windows).
