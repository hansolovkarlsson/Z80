# BDS C Reference

A reference for [BDS C](https://www.bdsoft.com/cpm/resources/bdsc.html) v1.60,
Leor Zolman's real 8080/Z80 C compiler for CP/M-80 (`cpm/resources/bdsc/`) —
**public domain** since September 20, 2002 (the author released all
rights explicitly, source included; see
`cpm/resources/bdsc/upstream/README.md`). Unlike every other third-party
program this project has validated against, there's no copyright
caveat here regardless of repo visibility.

Gathered from the real BDS C v1.60 User's Guide (November 1988 edition,
BD Software) — the primary source, not generic C-compiler knowledge.
This document covers the toolchain and the compiler's own real
limitations; it does not attempt to teach the C language itself, the
same stance the User's Guide itself takes ("this guide deals only with
details specific to the BDS C implementation; it does not attempt to
teach the C language").

## Quick start

```
make                                 # builds bin/z80
bin/z80 cpm_disk/CC.COM HELLO.C     # compile
bin/z80 cpm_disk/CLINK.COM HELLO    # link -> HELLO.COM
bin/z80 cpm_disk/HELLO.COM          # run
```

`CC.COM`/`CLINK.COM` and the rest of the toolchain already live in this
project's `cpm_disk/`, alongside two pre-built examples
(`bdshello.com`/`bdsfib.com`) — see `cpm_disk/README.md`. Regenerate
them with `cpm/resources/bdsc/derive.sh`, which compiles and links
`cpm/resources/bdsc/examples/hello.c` and `fib.c` through the real
toolchain and checks the output — a real, reproducible build-and-test
cycle for the compiler itself, not just a single program.

This is the **first program validated in this project that's a
command-line toolchain rather than a menu-driven interactive one** —
see `cpm/docs/ROADMAP.md`'s Phase 3 entry and `CLAUDE.md`'s File I/O
section for the two real emulator gaps that surfaced (command-line
argument delivery via the default FCB, and a console `EOF`-vs-"key
waiting" ambiguity) and how they were fixed.

## The toolchain files

| File | Role |
|---|---|
| `CC.COM` | The compiler, phase 1 (the parser). Reads the `.C` source file, does most of the front-end work, and — unless told otherwise — auto-loads `CC2.COM` to finish the job. |
| `CC2.COM` | The compiler, phase 2 (the code generator). Normally invoked automatically by `CC.COM`; only needs to be run by hand in low-memory situations, per the User's Guide. Produces a `.CRL` (relocatable object) file. |
| `CLINK.COM` | The linker. Takes one or more `.CRL` files (the first must contain `main`), resolves references against them and then the standard library (`DEFF.CRL`, `DEFF2.CRL`), and writes a runnable `.COM` file. |
| `CLIB.COM` | The librarian — transfers, renames, deletes, and inspects individual functions within `.CRL` library files. Not yet exercised by anything in this project. |
| `C.CCC` | The run-time package: initialization code and helper subroutines, ~1.5K, always linked in at the very start of every compiled `.COM` file. `CLINK.COM` refuses to run at all without a copy of this in reach (`Can't find 0/A:C.CCC`) — found the hard way during this project's own testing. |
| `DEFF.CRL` | The standard library's C-coded functions (compiled from `STDLIB1.C`/`STDLIB2.C`/`STDLIB3.C` in the real distribution) — `printf`, `strcpy`, the buffered file I/O, etc. |
| `DEFF2.CRL` | The standard library's assembly-coded functions (from `DEFF2A.CSM`/`B.CSM`/`C.CSM` in the real distribution) — lower-level things like `bdos`, `bios`, block-memory operations. `CLINK` scans it automatically alongside `DEFF.CRL`, same as real BDS C. |

`CLINK` always searches, in order: the `.CRL` files named explicitly on
its command line, then `DEFF.CRL`, then `DEFF2.CRL` (and `DEFF3.CRL` if
present) automatically — a function defined in your own source takes
precedence over a same-named library function as long as `CLINK` finds
yours first.

## Compiling and linking

```
bin/z80 cpm_disk/CC.COM HELLO.C
bin/z80 cpm_disk/CLINK.COM HELLO
```

`CC` prints a two-part banner (`BD Software C Compiler v1.60 (part I)`
then `(part II)`) with a rough free-memory report after each phase —
`41K elbowroom` / `37K to spare` in this project's own testing. `CLINK`
prints a link-statistics summary (`Last code address`, `Externals start
at...`, `Top of memory`, `Stack space`, `... link space remaining`) —
`cpm/resources/bdsc/derive.sh` greps for `to spare` and `link space
remaining` in these banners as its own pass/fail signal, rather than
relying on exit codes (BDS C's own error reporting predates the
convention of a nonzero process exit status).

Real command-line options that matter most in practice (full list in
the User's Guide, chapter 1):

**`CC`:**
- `-r n` — reserve `n`K for the symbol table (default 10K); raise it on
  "Out of symbol table space", lower it on "Out of memory".
- `-o` — optimize for speed (inlines code the default build turns into
  run-time-package subroutine calls to save space).
- `-e xxxx` — put external data at a fixed hex address instead of
  right after the code, letting the compiler use direct `lhld`/`shld`
  instead of indirecting through a pointer — shorter, faster code, at
  the cost of needing to know in advance how big the program's code
  will end up being.
- `-p` — echo the preprocessed source with generated line numbers
  (useful for debugging `#define`/`#include` expansion).
- `-w` — write an error file (`PROGERRS.$$$`) for the bundled `RED`
  screen editor to jump straight to reported error lines.

**`CLINK`:**
- `-n` — quick-return to the CCP instead of performing a warm boot on
  exit (saves ~2K of run-time memory since the CCP itself is preserved,
  not overwritten by the stack).
- `-o newname` — name the output `newname.COM` instead of reusing the
  main `.CRL` file's own name.
- `-w` — write a `.SYM` symbol table file, usable with `SID`/`DDT` for
  debugging, or by `CDB` (see below).
- `-d ["args"]` — "debug mode": run the linked `.COM` immediately
  instead of writing it to disk, optionally passing a command-line
  string as if typed at the CCP.

**Important, from the User's Guide directly**: command lines for every
`.COM` file in the package — including ones the compiler itself
produces — must never start with a leading blank or tab; the real
CCP's own command-line parser mishandles it. Not something this
project's own emulator enforces or needs to work around (the parsing
happens entirely inside the loaded `.COM` program), just a real, dated
quirk worth knowing if a compiled program's own argument handling ever
looks wrong.

## Real language limitations (vs. standard/K&R C)

BDS C targets "a healthy subset" of pre-Standard K&R C, not the full
language — this is by design, not an emulator artifact, straight from
the User's Guide's own "Objectives and Limitations" section and its
detailed Appendix A comparison against Kernighan & Ritchie:

- **No `float`, `double`, `short int`, or `long int`.** Only `char`,
  `int`, `unsigned`, and `struct`/`union` exist as base types. (A
  separate, optional BCD floating-point package and a long-integer
  package are provided as add-on libraries, not built into the
  language — see below.)
- **`char` is always unsigned, never sign-extended.** A `char` variable
  can never hold a negative value in BDS C. This specifically bites
  code that checks a function's return value (e.g. `getc`, which
  returns `-1` on `EOF`) if that return value is stored in a `char`
  instead of an `int` — `-1` silently becomes `255`, and a test for
  equality with `-1` never succeeds.
- **Only two storage classes: `external` and `automatic` — no
  `static`, no `register` as a real modifier** (the keyword is accepted
  but treated as a synonym for `int`, or ignored as a no-op modifier).
  Which class an identifier gets is purely positional: declared outside
  any function body means external, declared inside means automatic.
- **No initializers at all** (`int x = 5;` is not legal syntax). The
  library functions `initw`/`initb`/`initptr` exist specifically to
  work around this for arrays.
- **A single run-time stack**, not separate stack/heap regions — all
  local variables, function parameters, and intermediate expression
  values share one stack that grows down from high memory. Function
  parameters are evaluated and pushed in *reverse* order (last argument
  first) specifically so a function like `printf` can process a
  variable argument count.
- **No `typedef`, no bit fields, no blocks** (a variable declared at
  the top of an inner `{ }` is really scoped to the whole enclosing
  function, not just that block).
- **Comments nest by default** (`/* outer /* inner */ still inside */`
  is one comment) — the opposite of standard C; `-c` to `CC` switches
  to non-nesting Unix-style behavior.
- **`&&`/`||` have *equal* precedence**, evaluated left to right —
  parenthesize explicitly if mixing them, since standard C's usual
  precedence between them doesn't hold here.
- **The `sizeof` operator can't measure an array** directly (wrap the
  array in a `struct` to take `sizeof` the whole thing, or `sizeof` one
  element and multiply by the element count by hand), and can't appear
  inside an array-declaration's dimension expression.
- **Only 63 function definitions per source file** (a full *program*
  can span any number of files, each with up to 63 functions) — real
  1980s programs routinely split across files partly because of this.
- **Explicit pointers-to-arrays can't be declared** (`char (*foo)[5];`
  silently means the same thing as `char *foo[5];`, not a real pointer
  to an array) — a known, documented quirk, not a compiler bug.
- **Constant-expression folding only happens in specific syntactic
  positions** (right after `[`, `case`, an assignment operator, `,`,
  `(`, or `return`) and is always done in *unsigned* arithmetic — a
  bare `-12/5` in an arbitrary expression position can print as `13104`
  because the constant folder never got a chance to run and the
  division happens on `-12` reinterpreted as unsigned `65524`.

## Console I/O and Ctrl-C handling

BDS C's own `getchar`/`putchar` poll the console for a Ctrl-C
*during every character of output, not just input* — real, documented
behavior (`iobreak(0)` disables it; the default is on) — that's
exactly what surfaced this project's `console_char_ready()` gap (see
`CLAUDE.md`'s File I/O section and `cpm/docs/CPM_REFERENCE.md`'s
Implementation status): a piped/redirected stdin that's genuinely at
`EOF` looks indistinguishable from "a key is waiting" to a plain
`select()` check, and BDS C's per-character poll turned that into a
`^Z` injected after every single character printed when run
non-interactively.

## Auxiliary tools mentioned in the manual, not yet used here

The full distribution (see `cpm/resources/bdsc/upstream/README.md` for
exactly what was and wasn't kept) also documents:

- **`RED`** — a screen editor bundled with BDS C, with a direct
  integration hook: `CC -w` writes an error file `RED` can jump
  straight to.
- **`CDB`** — a symbolic source-level debugger (breakpoints, single-
  stepping, symbolic variable dump/set), needing programs compiled with
  `CC -k` and linked with the alternate `L2` linker's `-d`/`-s` options.
- **`L2`** — an alternative linker (also public domain, originally
  Scott Layson/Mark of the Unicorn) that omits the per-function jump
  tables `CLINK` generates, producing smaller code and letting `SID`
  show real function names in call traces; required once a program
  exceeds `CLINK`'s 255-function ceiling.
- **`CASM`** — an assembly-language preprocessor for hand-writing
  `.CRL`-format functions in 8080 assembly (`.CSM` source), for mixing
  C and assembly in one program.
- **`CMODEM`** — a MODEM7-protocol file-transfer/terminal program.
- Optional **BCD floating-point** and **32-bit long integer** add-on
  libraries, since the base language has neither.

None of these have been validated against this project's emulator yet
— they'd be a reasonable next step if BDS C work continues.
