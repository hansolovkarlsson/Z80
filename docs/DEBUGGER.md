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
| `--break ADDR` | stop when PC reaches ADDR (hex); repeatable, and implies the debugger |
| `--debug-script FILE` | read commands from FILE instead of the terminal |

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
prompt. Without a debug option, Ctrl-C behaves as it always has.

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
| `u [addr] [n]` | disassemble n instructions (default: from PC, 8) |
| `q` | end the run |
| `h` | help |

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

## What it sees

**Memory is the flat 64K array**, the bytes instruction fetch reads, never
the machine's bus hooks. Reading through the hooks would change the machine
being inspected: on the ABC806, reading character RAM latches that cell's
attribute byte. The consequence is that `m` and `w` cannot see memory a
machine diverts elsewhere: the ABC802's and ABC806's character RAM, and the
ABC806's high-resolution plane. Disassembly is unaffected, since fetch
reads the same array.

**`n` watches the stack, not the return address.** It stops at the first
instruction where SP is back to its value before the call. On a plain
machine the two agree, but CP/M's BDOS emulation runs an intercepted call
and the instruction it returns to as one step, so the return address is
never seen. For the same reason, **`s` at `0005` on `bin/z80`** reports
`JP` to the BDOS entry but runs the whole BDOS call and the instruction
after it; `n` over the `CALL 0005h` is the natural way past one.

## Not yet

- `--interactive` is refused with a debug option: that mode owns the
  terminal for the keyboard and the screen.
- The GTK apps have no debugger.
- No symbols: addresses only.
- No memory writes from the prompt; registers only.
