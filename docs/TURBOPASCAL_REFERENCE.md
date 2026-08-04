# Turbo Pascal 3.01A Reference

A reference for Borland's Turbo Pascal 3.01A — `resources/turbopascal/`,
the real CP/M-80 integrated environment (full-screen editor, single-pass
compiler, and compile-and-run, all in one program) this emulator has been
validated against (see `docs/ROADMAP.md`'s "Real-world validation: Turbo
Pascal 3.01A" entry for what got it running, including a real emulator
bug it surfaced). Sourced directly from Borland's own 1985 *TURBO Pascal
Reference Manual, Version 3.0* (a genuine scan, not summarized secondary
material — see the citations throughout). Two parts: the editor (a
keyboard reference — this is the part that prompted "I totally forgot
the editor Ctrl-keys from that era"), and the language (what Turbo Pascal
adds on top of standard Wirth Pascal — strings, typed constants, direct
memory/port access, and a large standard library, considerably more than
the ISO-standard language most "Pascal" tutorials describe).

## Quick start

```
bin/z80 cpm_disk/turbo.com
```

Answer `N` to "Include error messages" (saves ~1.5K of memory; skip if
you want in-editor error explanations rather than just an error number).
At the main menu: `W` to name a work file, `E` to edit it, `Ctrl-K-D` to
leave the editor back to this menu, `C`/`R` to compile/run, `S` to save
to disk. See [The main menu](#the-main-menu) below for all of them.

## The TURBO Editor

WordStar-compatible by design (see the manual's own note: "the logic of
these commands, once learned, quickly become so much a part of you that
the editor virtually turns into an extension of your mind" — the actual
CP/M-80 build has **no arrow-key or Delete-key support**; the WordStar
diamond below is the only way to move the cursor and edit text).

Control characters are entered by holding Control and pressing a letter
— `Ctrl-S` means hold Control, press S. Two-character commands (`Ctrl-K`
followed by another letter, etc.) are entered as two separate keystrokes,
not held simultaneously.

### Cursor movement

| Command | Keys | Effect |
|---|---|---|
| Character left | `Ctrl-S` | Non-destructive, doesn't work across line breaks |
| Character right | `Ctrl-D` | Non-destructive, doesn't work across line breaks |
| Word left | `Ctrl-A` | Works across line breaks |
| Word right | `Ctrl-F` | Works across line breaks |
| Line up | `Ctrl-E` | |
| Line down | `Ctrl-X` | |
| Scroll up | `Ctrl-W` | Screen scrolls down; cursor stays on its line |
| Scroll down | `Ctrl-Z` | Screen scrolls up; cursor stays on its line |
| Page up | `Ctrl-R` | One screenful less one line, with overlap |
| Page down | `Ctrl-C` | One screenful less one line, with overlap |
| To left of line | `Ctrl-Q S` | Column 1 |
| To right of line | `Ctrl-Q D` | Past the last printable character (trailing blanks are always stripped) |
| To top of screen | `Ctrl-Q E` | |
| To bottom of screen | `Ctrl-Q X` | |
| To top of file | `Ctrl-Q R` | |
| To end of file | `Ctrl-Q C` | |
| To beginning of block | `Ctrl-Q B` | Works even if the block isn't visibly marked |
| To end of block | `Ctrl-Q K` | Works even if the block isn't visibly marked |
| To last cursor position | `Ctrl-Q P` | Handy after a Save or a find/find-replace |

### Insert and delete

| Command | Keys | Effect |
|---|---|---|
| Insert mode on/off | `Ctrl-V` | Toggles Insert (default) vs. Overwrite; shown in the status line |
| Delete left character | `Ctrl-H` | **This is the "Del"/Backspace-equivalent command** — real WordStar-era keyboards had no separate Delete key, so a modern Backspace/Delete key sends this. Works across line breaks. |
| Delete character under cursor | `Ctrl-G` | Does not work across line breaks |
| Delete right word | `Ctrl-T` | Works across line breaks |
| Insert line | `Ctrl-N` | Inserts a line break at the cursor; cursor doesn't move |
| Delete line | `Ctrl-Y` | No undo — "take care!" per the manual |
| Delete to end of line | `Ctrl-Q Y` | |
| Restore line | `Ctrl-Q L` | Undoes changes to the current line, but only *as long as you haven't left the line* |

### Block commands

All two-character (`Ctrl-K` + letter). A block is marked with Begin/End
markers; commands work on it whether or not it's currently displayed
(toggle display with `Ctrl-K H`).

| Command | Keys | Effect |
|---|---|---|
| Mark block begin | `Ctrl-K B` | |
| Mark block end | `Ctrl-K K` | |
| Mark single word | `Ctrl-K T` | Marks the word the cursor is in (or to its left) — shortcut for the Begin/End pair |
| Hide/display block | `Ctrl-K H` | Copy/move/delete/write-to-disk only work while displayed |
| Copy block | `Ctrl-K C` | Original is left in place |
| Move block | `Ctrl-K V` | |
| Delete block | `Ctrl-K Y` | No undo |
| Read block from disk | `Ctrl-K R` | Reads a file in at the cursor as if it were a moved/copied block; prompts for filename (`.PAS` assumed if no type given) |
| Write block to disk | `Ctrl-K W` | Block is left unchanged; prompts for filename |

### Find and replace

| Command | Keys | Effect |
|---|---|---|
| Find | `Ctrl-Q F` | Prompts for a search string (up to 30 chars, control chars enterable via `Ctrl-P` prefix), then search options |
| Find and replace | `Ctrl-Q A` | Prompts for search string, replacement string, then options |
| Repeat last find | `Ctrl-L` | Repeats the last Find or Find & Replace exactly |

Find options (enter any combination, terminate with Enter): `B` search
backwards, `G` search the whole file regardless of cursor position, a
number `n` finds the *n*th occurrence, `U` ignore case, `W` whole words
only. Find & Replace adds `N` (replace without asking `Replace (Y/N)?`
each time). Abort an in-progress search/replace with `Ctrl-U`.

### Miscellaneous

| Command | Keys | Effect |
|---|---|---|
| End edit (back to main menu) | `Ctrl-K D` | Does **not** save to disk — editing happens entirely in memory; use the menu's `S)ave`, or let a compile do it automatically |
| Tab | `Tab` or `Ctrl-I` | No fixed tab stops — jumps to the start of each word on the line *immediately above* the cursor, handy for lining up declarations |
| Auto indent on/off | `Ctrl-Q I` | On by default; new lines start at the previous line's indent column |
| Control character prefix | `Ctrl-P` | Prefixes a literal control character into the text itself (e.g. to embed a `Ctrl-A` in a string constant) |
| Abort operation | `Ctrl-U` | Aborts whatever's in progress (a `Replace (Y/N)?` prompt, entering a search string or filename) |

### The status line

Shown at the top of the editor: `Line n`, `Col n`, `Insert` (vs.
`Overwrite`, toggled by `Ctrl-V`), `Indent` (shown only when auto-indent
is active), and the file being edited (`d:FILENAME.TYP`).

## The main menu

| Key | Command | Effect |
|---|---|---|
| `L` | Logged drive | Change the current drive (`A`-`P`); also forces a disk reset, so use it after swapping disks even if not changing the letter |
| `W` | Work file | Set the file `E`/`C`/`R`/`X`/`S` all operate on. No type given → `.PAS` assumed; a trailing `.` with nothing after → no type at all |
| `M` | Main file | For programs using `{$I file}` includes — compiles this file instead of the Work file, with the Work file treated as one of its includes |
| `E` | Edit | Invoke the built-in editor on the Work file |
| `C` | Compile | Compiles the Work file (or Main file if set); result goes to memory, a `.COM` file, or a `.CHN` file depending on the `O)ptions` setting |
| `R` | Run | Runs a program already in memory, or compiles first if needed |
| `S` | Save | Saves the Work file to disk (old version renamed to `.BAK`) |
| `D` | Dir | Directory listing; prompts for an optional drive/mask (`*`/`?` wildcards) |
| `X` | eXecute | Runs another `.COM` file from within Turbo (needs `TURBO.OVR` present) |
| `Q` | Quit | Leave Turbo Pascal, prompting to save first if the Work file was edited |
| `O` | compiler Options | Compile destination (Memory/`.COM`/`.CHN`) and other compiler defaults |

Source: [Borland's *TURBO Pascal Reference Manual, Version 3.0*](https://electrickery.nl/comp/tp30/tp30_toc.html)
(1985), Chapter 1.

## The language: beyond standard Wirth Pascal

Turbo Pascal 3.0's reserved-word list marks several as non-standard
extensions: `absolute`, `external`, `inline`, `overlay`, `shl`, `shr`,
`string`, and `xor`. The extensions run deeper than just those keywords,
though — see below.

### Numbers

Hex constants use a `$` prefix rather than a suffix: `$ABC` (not
`0ABCh`). Range: `$0000`-`$FFFF`. Decimal integers: -32768 to 32767
(`Integer`, 2 bytes) or 0-255 (`Byte`, 1 byte, freely mixed with
`Integer`). `Real`: 1E-38 to 1E+38, 11 significant digits, 6 bytes;
overflow halts the program, underflow silently yields 0.

### String type — the headline addition

Standard Pascal has no string type at all (just fixed-length `packed
array of char`, no dynamic length, no `+`). Turbo's `string[n]` (`n` =
1-255, no default — must always be specified) is a real variable-length
type: a length byte (index 0, so `Length(S)` is `Ord(S[0])`) followed by
up to `n` characters, indexed 1..length.

| Operator/function | Effect |
|---|---|
| `+` (or `Concat(S1,S2,...)`) | Concatenation |
| `=` `<>` `<` `>` `<=` `>=` | Lexicographic comparison (shorter-but-equal-prefix strings compare smaller) |
| `Copy(St,Pos,Num)` | Substring: `Num` chars starting at `Pos` |
| `Delete(St,Pos,Num)` | Removes `Num` chars from `St` starting at `Pos`, in place |
| `Insert(Obj,Target,Pos)` | Inserts `Obj` into `Target` at `Pos`, in place |
| `Pos(Obj,Target)` | Index of `Obj` within `Target`, or 0 if not found |
| `Length(St)` | Number of characters |
| `Str(Value,St)` | Number → string |
| `Val(St,Var,Code)` | String → number; `Code` is 0 on success, else the position of the first bad character |
| `Chr(Num)` / `Ord(St[0])` | Char ↔ ordinal, as usual |

A single character (`'A'`) is type-compatible with `Char`; a `char array`
is compatible with `string` of the same length, so old-style fixed
arrays and new-style strings can be mixed freely.

Control characters can be embedded directly in string literals: `#13`
(by decimal ASCII code) or `^G` (Control-G/Bell, etc.) — `'Wait! '^G^G`
`'` embeds two bells mid-string with no explicit concatenation needed.

### Typed constants — initialized variables

`const Digits: array[0..9] of Char = '0123456789';` — a "TURBO
specialty": syntactically a constant (must be declared with a value),
but usable exactly like a variable, including as a `var` parameter. This
is how Standard Pascal's complete lack of variable initializers gets
worked around — array, record, and set constants are all supported, not
just scalars.

### Sets, records, arrays

Mostly standard, with a couple of TURBO-specific conveniences: `case`
statements support an `else` clause (not in the ISO standard), multi-
dimensional arrays can be declared as `array[S1,S2] of T` instead of
nesting `array[S1] of array[S2] of T`, and `with` statements can open
multiple records at once (`with A,B,C do`) instead of nesting three
separate `with`s.

### Direct memory, ports, and machine code

Not present in standard Pascal at all:

| Identifier | Purpose |
|---|---|
| `Mem` | Predefined `array of Byte` mapped directly onto CPU memory |
| `Port` | Predefined `array of Byte` mapped onto I/O ports |
| `Addr(Var)` | Address of a variable |
| `absolute` | `Var A: T absolute Addr;` — declares `A` to live at a fixed address rather than being allocated normally (used for `Mem`/`Port`-style access with a real type instead of raw bytes) |
| `Move(Src,Dst,Num)` | Raw byte copy between two variables of any type (correctly handles overlap, unlike a naive loop) |
| `FillChar(Var,Num,Value)` | Fills `Num` bytes with `Value` |
| `inline` | Embeds raw machine code bytes directly in the source |
| `external` | Declares a procedure/function implemented outside the Pascal source (linked in separately) |

### File handling — Assign-based, not parameter-based

Standard Pascal ties files to program-heading parameters
(`program P(Input,Output)`); Turbo uses an explicit `Assign` call
instead, decoupling the file *variable* from the file *name*:

```pascal
var F: file of Integer;
begin
  Assign(F, 'DATA.DAT');
  Rewrite(F);   { create, or Reset(F) to open an existing file }
  Write(F, 42);
  Close(F);
end.
```

`Read`/`Write` transfer whole components and auto-advance the file
pointer; `Seek(F,n)` jumps to component `n` (0-based) for random access
— `Seek(F, FileSize(F))` is the standard idiom for "append." `Erase`,
`Rename`, `Flush`, `EOF`, `FilePos`, `FileSize` round out the set.
**Untyped files** (`var F: file;`, no component type) skip the type
system entirely for `BlockRead`/`BlockWrite` (128-byte-record raw
transfers) — the standard "copy any file" idiom.

**Text files** (`var F: Text;`) are the odd one out: line-structured
(CR/LF-terminated) rather than fixed-size components, sequential-only,
and support `Readln`/`Writeln`/`Eoln` alongside `Read`/`Write`. `Write`'s
parameters take an optional `:width` (and `:decimals` for `Real`) field
specifier, e.g. `Write('Total: ', Total:8:2)`. **Logical devices** — `Con`
(buffered console), `Trm` (unbuffered terminal), `Kbd` (keyboard, no
echo), `Lst` (printer), `Aux` (auxiliary/serial) — are pre-declared
`Text` files needing no `Assign`/`Reset`, so `Write(Lst, ...)` just
works. `Input`/`Output` are the default `Read`/`Write` targets when no
file variable is given.

### Pointers — mostly standard, with two heap-management styles

`^Type` pointers and `New(P)` work as in standard Pascal. Turbo adds a
second heap-management scheme on top: `Mark(V)`/`Release(V)` (bulk-free
everything allocated since the `Mark`) as an alternative to the standard
`Dispose(P)` (free one specific variable) — **never mix the two in the
same program**, the manual is explicit that this produces
"unpredictable results." `GetMem`/`FreeMem` allocate/free a raw byte
count instead of a typed variable, for when the `New`/`Dispose` type
system gets in the way. `MemAvail`/`MaxAvail` report free heap space.

## Standard library quick reference

Beyond what's covered above (string/file/pointer functions): a
selection of the rest of Turbo's ~100 standard identifiers.

**Arithmetic**: `Abs`, `ArcTan`, `Cos`, `Exp`, `Frac`, `Int`, `Ln`,
`Sin`, `Sqr`, `Sqrt`.
**Scalar/ordinal**: `Pred`, `Succ`, `Odd`, `Chr`, `Ord`, `Round`,
`Trunc`, plus the `Retype` facility (`TypeName(value)`, e.g.
`Month(10)`) for converting between scalar types by ordinal value —
standard Pascal has `Ord` (scalar→integer) but no way back.
**Misc**: `Hi`/`Lo` (high/low byte of an `Integer`), `Swap` (swap an
`Integer`'s two bytes), `KeyPressed`, `Random`/`Random(n)`/`Randomize`,
`ParamCount`/`ParamStr(n)` (command-line arguments), `SizeOf(Name)`.
**Screen** (needs a terminal profile installed via `TINST` — see
`docs/ROADMAP.md`): `ClrScr`, `ClrEol`, `GotoXY(X,Y)`, `InsLine`,
`DelLine`, `LowVideo`/`NormVideo`, `CrtInit`/`CrtExit`.
**Program control**: `Halt` (stop, return to CP/M), `Exit` (return from
the current block early — like a `goto` to just before `end`), `Delay(ms)`.

Source: same manual, Chapters 2-17 (Basic Language Elements through
Including Files).

## Known limitations (of this build, not of Turbo Pascal itself)

- **The editor genuinely has no arrow-key or dedicated-Delete-key
  support** on CP/M-80 — confirmed from the manual itself, not just this
  emulator: "IBM PC systems come with arrows and dedicated function keys
  already installed" (i.e., only the IBM PC version does). Use the
  WordStar diamond above.
- **`Ctrl-H`/Backspace/Delete used to move the cursor left without
  erasing — root-caused, and now fixed with a single-key rebind.** By
  default, Turbo Pascal binds byte `0x08` (`Ctrl-H`, and whatever a
  `Backspace`-labeled key sends on many real terminals) to the *same*
  function as `Ctrl-S` — non-destructive character-left — reserving true
  delete for the distinct `<DEL>`/`<RUBOUT>` byte (`0x7F`), confirmed
  straight from the manual's own wording ("The `<BACKSPACE>` key which
  normally backspaces non-destructively like `Ctrl-S` may be installed
  to have the same effect [delete] if... your keyboard lacks a
  `<DELETE>` key"). This project's own `console_read_char()` (`cpm.c`)
  always translates a modern keyboard's Delete/Backspace key (`0x7F`)
  into `0x08` before any program sees it — a real fix for Tasty Basic,
  which only recognizes `0x08` and ignores `0x7F` entirely (confirmed
  from its own source, `cpmio.asm`) — so Turbo Pascal, out of the box,
  never got the raw `0x7F` it wanted for delete; `0x08` always landed on
  its non-destructive-left binding instead.
  **Fixed** by using Turbo Pascal's own reconfiguration tool the way real
  users would: `resources/turbopascal/derive.sh` now also runs
  `TINST.COM`'s `[C]ommand installation` (a *second*, separate `TINST`
  invocation layered on top of the ANSI Screen-installed `TURBO.COM` —
  entering `Q` at `TINST`'s own top-level menu saves and exits
  immediately, so Screen and Command installation can't be answered in
  one continuous session) and binds item 27, "Delete left character", to
  `0x08` — the same rebind a real Turbo Pascal user with this exact
  keyboard conflict would have made by hand. All other 44 commands are
  left on their defaults (a bare `<RETURN>` at each prompt); Command
  installation offers no partial-edit shortcut, so answering all 45 is
  the only way to change one. Verified directly: typing `ABCD` then two
  `Ctrl-H` in the editor and saving now produces `AB`, not `ABCD` —
  genuine destructive delete. The two-key `Ctrl-S`-then-`Ctrl-G`
  (move left, then delete-under-cursor) still works too, if ever needed.
- **`Ctrl-S`/`Ctrl-Q` appearing to do nothing was a real, separate bug,
  now fixed** — classic Unix software flow control (`IXON`): the host
  terminal driver intercepts these as XOFF/XON (pause/resume output)
  and never delivers the byte to the emulator at all. `cpm_console_init()`
  (`cpm.c`) now disables `IXON` in its raw-mode setup, alongside the
  existing `ICRNL`/`INLCR`/`IGNCR` fix for the same reason (a genuine
  raw serial line has no such host-side interception either).
- **Ships configured for a "Microbee VDU" terminal**, not ANSI — this
  project's `resources/turbopascal/derive.sh` reconfigures it via
  `TINST.COM`'s real "ANSI" profile before use. See
  `resources/turbopascal/upstream/README.md`.

## Verified against this emulator

Actually run through `bin/z80`, not just documented: booting to the real
banner and main menu, `TINST.COM`'s terminal reconfiguration (Microbee →
ANSI) and command reconfiguration (`Ctrl-H` → real delete), entering the
editor and typing text (echo, `Insert` status, line/column tracking all
correct), `Ctrl-K D` to exit the editor, and `R)un` compiling and
executing a program from the main menu. The much larger remainder of the
language above is documented from the manual but not yet individually
exercised here.
