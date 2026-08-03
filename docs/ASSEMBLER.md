# Assembler Reference (`z80asm`)

Syntax reference for `bin/z80asm` (`asm/src/`). For what's implemented vs.
not yet, and known limitations/trade-offs, see `docs/ROADMAP.md`'s Phase 2
section — this document describes the syntax as it exists today without
re-litigating that history.

## Usage

```
z80asm <input.asm> [-o output.com]
```

Assembles `input.asm` and writes a raw binary covering only the address
range something was actually assembled into (i.e. from the lowest to the
highest byte written, trimmed to whatever `ORG` the source used — not a
fixed 64KB image). Without `-o`, the output path defaults to the input's
basename with a `.com` extension (`hello.asm` → `hello.com`).

Two passes run over the source: pass 1 resolves labels (forward
references are fine — a label doesn't need to be defined before it's
used), pass 2 emits the real bytes and reports errors for anything still
unresolved.

## Source line format

```
[label[:]] [mnemonic [operands]] [; comment]
```

- **Labels**: `name:` (colon form — preferred) or a bare `name` with no
  colon — either followed by an instruction/directive on the same line
  (colon-less form, e.g. `bdos  push  af`), or alone on its own line with
  the instruction/directive on a later line (e.g. a bare `welcome` line
  followed by `DB "hello"` on the next). Either colon-less form is
  disambiguated from a genuine unknown instruction by checking whether
  the first word is a recognized mnemonic or directive — if not, it's
  treated as a label. Prefer the colon form for your own code; the
  colon-less forms exist for compatibility with sources like `zexall.z80`
  and Tasty Basic's `tastybasic.asm` that use them.
- **Comments**: `;` to end of line, except inside a `'...'` or `"..."`
  literal (so a comment character inside a quoted string isn't mistaken
  for a comment start).
- Mnemonics, directives, and register names are case-insensitive.
  **Labels are case-sensitive.**

## Numbers and literals

| Form | Example | Meaning |
|---|---|---|
| Decimal | `123` | |
| Hex (suffix) | `0FFh` | must start with a digit, so `FFh` alone is invalid — use `0FFh` |
| Hex (prefix) | `0xFF` | |
| Binary (suffix) | `1100000b` | every digit before the `b` must be `0`/`1` |
| Character | `'A'` | ASCII value of the character |
| Current address | `$` | the location counter at the start of the current line |

## Expressions

Precedence, low to high:

1. **Relational** (lowest, non-chaining — `a < b < c` is not meaningful):
   `= <> < <= > >=`, or word forms `eq ne lt le gt ge`. Result is `-1`
   (true) or `0` (false) — the traditional assembler convention (all bits
   set for true), not `1`/`0`. A single `<`/`>` is always relational; only
   a doubled `<<`/`>>` is ever read as a shift (see below), so `a>=b` and
   `a>>b` don't compete for the same input.
2. **Bitwise**: `& | ^`
3. **Shift**: `<< >>` (arithmetic left/right shift)
4. **Additive**: `+ -`
5. **Multiplicative**: `* / %`
6. **Unary** (highest): `-` `+` `~`, and `low`/`high` as prefix operators

`low X` / `high X` extract the low/high byte of a 16-bit value. Both the
prefix form (`low msbt`, no parens — the common assembler convention) and
call-like form (`low(msbt)`) work identically, since `low`/`high` bind as
a unary prefix operator and parentheses are just generic grouping — so
`low(expr)` parses the same as `low` applied to a parenthesized
sub-expression.

Symbol references: any identifier that isn't a register name or
directive/mnemonic keyword. Referencing a symbol that's still undefined
by pass 2 is an error (`undefined symbol`); pass 1 tolerates it silently
since the symbol may be defined later in the source.

## Directives

| Directive | Effect |
|---|---|
| `ORG addr` | Sets the location counter. |
| `label EQU expr` | Defines `label` as `expr`'s value — a label using `EQU` is *not* bound to the current address. |
| `DB expr[,expr...]` (or `DEFB`) | Emits bytes. A quoted `'...'`/`"..."` field emits one byte per character (arbitrary length — no line-length limit). |
| `DW expr[,expr...]` (or `DEFW`) | Emits 16-bit values, little-endian. |
| `DS count[,fill]` (or `DEFS`) | Reserves `count` bytes, each set to `fill` (default `0`). |
| `END` | No-op — assembly just continues to end-of-file regardless. |
| `ASEG` / `CSEG` / `DSEG` | No-ops — this is a single flat-image assembler; segment selection doesn't apply. Accepted so real-world sources that use them (as a habit from segmented/relocatable assemblers) don't need editing. |
| `IF expr` / `ELSE` / `ENDIF` | Conditional assembly, up to 32 levels deep. Non-zero is true. Lines inside a false branch are fully skipped — no bytes emitted, no labels defined, no PC advance — but `IF`/`ELSE`/`ENDIF` themselves are still tracked so nesting stays correct. |
| `ERROR 'message'` | Unconditionally fails assembly with `message` — typically used inside a macro's own self-check (`IF ... ERROR '...' ENDIF`, e.g. to catch a caller passing the wrong number of arguments). |
| `REPT count` / `ENDM` | Repeats the enclosed lines `count` times. `count` can reference `$` (evaluated fresh for each pass, using that pass's real address at the `REPT` line) — the common use is padding to a target address, e.g. `REPT target-$` / `DB 'X'` / `ENDM` fills with `'X'` up to (not including) `target`. Works both at the top level and inside a `MACRO` body (the nesting is tracked correctly even though both `MACRO` and `REPT` close with the same `ENDM` keyword — see `asm/examples/rept_test.asm`). |

## Macros

```
name MACRO param1,param2,...
    ; body
    LOCAL local1,local2,...
    ...
ENDM
```

- **Definition**: the macro's name comes *before* the `MACRO` keyword
  (`name MACRO ...`, not `MACRO name ...`) — matches the common M80/ZSM4
  convention that e.g. `zexall.z80`'s own `tstr`/`tmsg` macros use. The
  name may optionally have a trailing `:` (`tstr: macro ...`), which is
  stripped.
- **Invocation**: `name arg1,arg2,...` — a line whose first word matches
  a defined macro name.
- **Parameter substitution**: `&param` or a bare `param` (no `&`) in the
  body is replaced with that argument's text. Real source may mix both
  forms for the same parameter set in one macro body — match both, since
  that's what real-world source (`zexall.z80`'s `tstr` macro) actually
  does. This is more permissive than strict ZSM4 semantics and carries a
  small risk: a parameter name that happens to coincide with an unrelated
  identifier elsewhere in the *same macro's own body* would also get
  substituted. Keep parameter names distinctive if that's a concern.
- **Grouped arguments**: wrap a call argument in `<...>` to bundle a
  comma-containing value into one argument — e.g. `tstr
  <0EDh,042h>,...` passes `0EDh,042h` (a 2-byte instruction encoding) as
  a *single* argument, not two. The `<>` are stripped from the stored
  value; the substituted text is just `0EDh,042h`.
- **`LOCAL name1,name2,...`**: declares names local to this expansion.
  Each invocation of the macro gets its own uniquely-renamed instance, so
  the same macro can define a label (e.g. a loop target) and be invoked
  more than once without the label colliding across invocations. `LOCAL`
  names are referenced with `&` too, exactly like parameters (`local lab`
  then `&lab:`, not bare `lab:`) — this matches `zexall.z80`'s own usage.
- Macro bodies can invoke other macros (including ones defined via an
  `INCLUDE`d file) and use `INCLUDE` themselves.

Macro expansion is a one-time text-substitution pass that runs before
assembly proper: the expanded, flattened line list is what actually gets
assembled (both passes see identical, already-expanded source).

## Include

```
INCLUDE "path"
INCLUDE path
```

Splices another file's contents in place (recursively — an included file
can itself `INCLUDE` further files, and can contain macro
definitions/invocations). Paths are resolved **relative to the including
file's own directory**, not the current working directory `z80asm` was
invoked from — so an `INCLUDE "defs.inc"` next to the including source
works the same regardless of where you run `z80asm` from.

## Operand syntax

| Kind | Forms |
|---|---|
| 8-bit registers | `A B C D E H L` |
| 16-bit register pairs | `BC DE HL SP`, plus `AF`/`AF'` (only valid for `PUSH`/`POP`/`EX AF,AF'`) |
| Index registers | `IX IY`, and undocumented halves `IXH IXL IYH IYL` |
| Memory | `(HL)` `(BC)` `(DE)` `(SP)` (EX only) `(C)` (IN/OUT only) `(nn)` `(IX+d)` `(IY+d)` `(IX-d)` `(IX)` (displacement defaults to 0) |
| Immediate | any expression |
| Condition codes | `NZ Z NC C PO PE P M` for `JP`/`CALL`/`RET`; only `NZ Z NC C` for `JR`/(n/a for `DJNZ`, which takes no condition) |

See `docs/Z80_REFERENCE.md` for the full instruction set the encoder
supports, including the undocumented forms (`IXH`/`IXL`/`IYH`/`IYL`,
`SLL`) and how they're encoded.

## Errors

Errors are reported as `file:line: error: message` (or `file:line (macro
NAME): error: message` for a line that came from a macro expansion, so
you can tell where the *invocation* was even though the text came from
the macro body) and printed for every error found in a pass, not just the
first — pass 1 must be completely clean before pass 2 runs. A relative
jump (`JR`/`DJNZ`) whose target is more than 127 bytes away or less than
-128 bytes away is reported as `relative jump out of range`.
