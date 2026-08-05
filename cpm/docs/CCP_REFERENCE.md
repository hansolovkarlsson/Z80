# CCP (Console Command Processor) Reference

A reference for the CP/M 2.2 shell this emulator can boot —
`cpm/resources/ccp/`, genuine, unmodified Digital Research source (see
`cpm/resources/ccp/upstream/README.md` for provenance and
`cpm/docs/ROADMAP.md`'s "Get a real CP/M 2.2 CCP booting" entry for how it
got running here, including two real bugs that surfaced along the way).
Unlike `cpm/docs/TASTYBASIC_REFERENCE.md`/`cpm/docs/MBASIC_REFERENCE.md`, this
document describes the actual DRI CCP source directly (read from
`cpm/resources/ccp/ccp_cpm.asm`), not a language built on top of it — the
CCP itself has no "language", just a command line.

## Quick start

```
bin/z80 --ccp cpm_disk/ccp.com
```

```
A>DIR

A: CONSOLE  COM : SELFTEST COM : MACROTST COM : INCLTEST COM
A: HELLO    COM : SARGON   COM : GAPSTEST COM : MBASIC   COM
A: README   MD  : PHROGZ   DIN : REPTTEST COM : ADVENTUR COM
A: TASTYBAS COM : FILETEST COM : CCP      COM
A>HELLO

Hello from z80asm!5

A>
```

## Command-line syntax

- Input is read a full line at a time and **uppercased automatically**
  (`dir` and `DIR` are identical).
- **Delimiters** separating a command/filename from what follows: space,
  `=`, `_` (or the historical left-arrow character, `5Fh`), `.`, `:`,
  `;`, `<`, `>`.
- **Drive prefix**: `d:filename` selects drive `d` (`A`-`P`) for that one
  command. This build only ever has one real drive (`A:` — see
  `CLAUDE.md`'s File I/O section on why every drive/user number collapses
  onto the same host directory), so a drive prefix other than `A:` is
  accepted syntactically but doesn't select a different file set.
- **Wildcards**: `?` matches any single character in a name/type field;
  `*` expands to fill the rest of the field with `?`s (so `*.*` means
  "everything", same as a bare `DIR` with no name). `DIR`/`ERA` accept
  wildcards; `TYPE`/`SAVE`/`REN` require an unambiguous (wildcard-free)
  name.
- **Line editing** while typing a command is handled by this emulator's
  own BDOS `C_READSTR` (function 10) implementation, not by the CCP
  itself — see `CLAUDE.md`'s Console input section for exactly what that
  supports (echo, Backspace/DEL).
- **Errors**: an unrecognized command or bad argument echoes the
  offending text back followed by `?` (e.g. `A>FOO` → `FOO?`), the
  classic CP/M convention, then returns to the prompt.

## Built-in commands

| Command | Syntax | Effect |
|---|---|---|
| `DIR` | `DIR [name.typ]` | Lists matching files (default: everything). Only shows names that fit CP/M's real 8.3 limit — anything longer was never written to the directory in the first place, not filtered out (see `cpm_disk/README.md`). Wraps to a new line, with a fresh `d:` prefix, every 4 entries. |
| `ERA` | `ERA name.typ` | Deletes matching file(s). `ERA *.*` (or any fully-wildcarded pattern) asks `ALL (Y/N)?` first — anything but `Y` cancels. |
| `TYPE` | `TYPE name.typ` | Prints a file's contents to the console, stopping at a `^Z` (`1Ah`) byte if present. |
| `SAVE` | `SAVE n name.typ` | Writes the first `n` 256-byte pages of the TPA (starting at `0x100`) to a new file — the classic CP/M way to capture a hand-assembled/POKEd memory image without a `.COM` header. Deletes any existing file of that name first. |
| `REN` | `REN new=old` (or `new_old`) | Renames `old` to `new`. Fails if `new` already exists (`FILE EXISTS`) or `old` doesn't (`old?`). |
| `USER` | `USER n` | Sets the current user number (0-15, decimal only). This build's file mapping doesn't distinguish user numbers (see `CLAUDE.md`), so this just changes what's echoed back, not which files are visible. |

## Running a program

Anything typed that isn't one of the six built-ins above is looked up as
`name.COM` and, if found, loaded at `0x100` and run — e.g. `HELLO` runs
`HELLO.COM`. Text after the program name on the same command line is
passed through to it exactly as real CP/M does: the first token becomes
the default FCB at `0x5C` (so simple single-argument programs work
unmodified) and the whole tail is also copied into the command-line
buffer at `0x80`, length-prefixed. The CCP prompt returns automatically
once the program exits — normal `RET`, `jp 0`, and BDOS function 0
(`P_TERMCPM`) all correctly land back at `A>` (see `cpm/docs/ROADMAP.md` for
what it took to make that work).

## Known limitations (of this build, not of DRI's CCP itself)

- **One drive.** `d:` prefixes other than `A:` parse but don't do
  anything meaningful — see Command-line syntax above.
- **No SUBMIT files.** `readcom` (the command-line reader) still checks
  for a pending `$$$.SUB` file exactly like real CP/M, so the mechanism
  is present, but nothing in this project creates one, so it's never
  exercised.
- **Serialization check disabled.** The real DRI source includes an
  anti-piracy check comparing bytes at the nominal BDOS location against
  an embedded serial number, self-patching the CCP into a crash trap on
  mismatch — deliberately omitted when building this binary, since this
  design has no resident BDOS bytes for it to compare against (BDOS is
  emulated entirely on the host side). See
  `cpm/resources/ccp/preprocess.py`'s own comment and
  `cpm/docs/ROADMAP.md`.

## Verified against this emulator

Actually run through `bin/z80 --ccp`, not just read from source: booting
to a real `A>` prompt, `DIR` (both the initial 8.3-filename-visibility
finding and, after the binary-literal assembler fix in
`cpm/docs/ROADMAP.md`, correct 4-per-line wrapping), `TYPE`, and running
other programs (`HELLO`, `SARGON`) by name with a correct return to the
prompt across multiple commands in a row. `ERA`/`REN`/`SAVE`/`USER` are
documented from source above but not yet individually exercised here — a
good set of next things to try.
