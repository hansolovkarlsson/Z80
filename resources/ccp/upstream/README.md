# CCP (upstream)

`ccp.asm` is Digital Research's original CP/M 2.2 Console Command
Processor (the "shell" that prints the `A>` prompt, implements built-in
commands like `DIR`/`TYPE`/`ERA`/`REN`/`SAVE`/`USER`, and loads/runs
`.COM` files by name), copyright 1976-1980 Digital Research. Downloaded
from [brouhaha/cpm22](https://github.com/brouhaha/cpm22/blob/main/ccp.asm)
(Eric Smith's reformatting of the original `os2ccp.asm` for cross-assembly
with Macro Assembler AS), itself sourced from
[cpm.z80.de](http://www.cpm.z80.de/download/cpm2-plm.zip) — genuine,
unmodified DRI source, not a rewrite.

**Written entirely in 8080 mnemonics** (CP/M predates the Z80 - it
originally targeted Intel's 8080), unlike SARGON or Tasty Basic which are
real Z80 source. `../derive.py` in this directory is a general-purpose
8080→Z80 mnemonic translator (register-pair renaming, `M`→`(HL)`,
`PSW`→`AF`, condition-code call/jump/return forms, etc.) written to get
this file building, but reusable for any other 8080-mnemoric CP/M-era
source.

**License note**: same situation as `resources/sargon/` - GitHub shows
no declared license for `brouhaha/cpm22` (`NOASSERTION`), and DRI's
original 1980 copyright notice is present verbatim in the source. Kept
here anyway per the same private-repo policy as SARGON/Adventure.
