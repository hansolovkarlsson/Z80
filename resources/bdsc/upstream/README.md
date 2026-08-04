# BDS C v1.60 - upstream files

Leor Zolman's BDS C, an 8080/Z80 C compiler for CP/M-80, originally
released commercially in 1979. **Public domain** since September 20,
2002 - Zolman released all rights to the entire package (compiler,
linker, library sources, utilities, documentation) explicitly and in
writing; see <https://www.bdsoft.com/resources/bdsc.html>. Unlike every
other third-party program in this repo, there's no copyright/licensing
caveat needed here regardless of whether this repo is ever made public.

Fetched from the author's own site:
`https://www.bdsoft.com/dist/bdsc-all.zip` (the combined CP/M-80 +
ZCPR3 retail distributions, full 8080 assembly compiler/linker source,
and User's Guide PDF - 1.5MB). Only the files needed for a working
plain-CP/M-80 compile-and-link toolchain are kept here, from the
`bdsc160/` (v1.60, "vanilla" CP/M-80) subdirectory of that archive:

- `CC.COM`/`CC2.COM` - the two-pass compiler proper.
- `CLINK.COM` - the linker.
- `CLIB.COM` - the librarian (not exercised yet by anything in this repo).
- `DEFF.CRL`/`DEFF2.CRL` - compiled object code for the standard library
  (`printf` and the rest of the C-coded runtime), linked in by default.
- `C.CCC` - linker configuration (default library search path etc.) -
  `CLINK.COM` refuses to run without it (`Can't find 0/A:C.CCC`).

Not included: the ZCPR3-specific `bdsz20/` variant, the CDB debugger,
RED editor, and BCD (binary-coded decimal) packages the full archive
also contains, `EXAMPLES.LBR`/`SOURCES.LBR` (CP/M "library" archives
needing `LBREXT.COM` to unpack), and `bugs.doc`/the User's Guide PDF -
none of this project's own testing needed them, and they're easy to
re-fetch from the same URL above if that changes.

Confirmed genuine via `CC.COM`'s own embedded strings: `Copyright (c)
1982, 83, 84 by Leor Zolman`.
