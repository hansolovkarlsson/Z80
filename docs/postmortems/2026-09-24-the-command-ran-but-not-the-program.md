# The command ran, but not the program

**Date**: 2026-09-24
**Component**: `docs/TOOLCHAIN.md`, and `scripts/config.sh`, which set up
the shell the guide was verified in
**Found by**: noticing that `z80asm` printed a line the guide said it never
prints

## What happened

`docs/TOOLCHAIN.md` was written that afternoon with a stated standard:
every command and every line of output in it was run as shown. It was, in
a shell set up the way the guide tells the reader to, with
`source scripts/config.sh`. The guide said of the assembler, "It prints
nothing when it succeeds", because in that shell it printed nothing.

This repo's `z80asm` prints `z80asm: wrote 99 bytes to 'hello.com'` on
every success. The program that had run was **Homebrew's `z80asm`**, an
unrelated assembler at `/opt/homebrew/bin/z80asm`. It was found hours later
and by accident, when the next piece of work ran `bin/z80asm` by its full
path and the success line appeared.

## Root cause

`scripts/config.sh` added `bin/` to the **end** of `PATH`:

```
export PATH="$PATH:$BASEDIR/bin"
```

So a same-named program anywhere earlier on `PATH` won. `z80` and
`z80dasm` have no namesakes on this machine and resolved to the repo;
`z80asm` does, and did not. Nothing errors: both are Z80 assemblers, both
accept `hello.asm`, and for that file both produce the same 99 bytes.

## Why it survived

**The verification was real, and it verified the wrong thing.** Running
every command is a strong standard, and it was met. What it proves is that
*a* program by that name produced that output, not that the program the
document describes did. The check had a premise, "`z80asm` means this
repo's `z80asm`", and nothing tested it.

**The impostor was compatible enough to pass.** Its output bytes matched,
so the round-trip step (`cmp` printing nothing) passed too. The only
visible difference was an *absence*: a missing status line, which reads
exactly like a quiet tool. An absence cannot contradict a claim that says
"nothing is printed".

**The setup script was trusted because it was the documented setup.** The
guide told the reader to use it, so the author used it, which is correct
and is also why the author and every reader with Homebrew's `z80asm`
installed would share the same blind spot.

## What changed

- `scripts/config.sh` puts `bin/` **first** on `PATH`, so the repo's tools
  win over any namesake. `which z80asm` now names the repo's `bin/`.
- The guide's assembler output is re-captured with the right binary
  (`z80asm: wrote 99 bytes ...`), and its setup section tells the reader to
  check `which z80asm`.
- Every other command in the guide was rerun with the repo's tools. Only
  the assembler's own output had been wrong: `z80`, `z80dasm` and the
  debugger have no namesakes here, and the round trip is byte-identical
  with the right assembler too.

The habit:

> **When a check runs a program by name, confirm which program the name
> reaches.** `which -a` before trusting output, and prefer an explicit
> path in anything that claims to verify. A plausible impostor passes
> every check whose premise is only that the tool exists.
