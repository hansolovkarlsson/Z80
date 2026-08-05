# Scripts

Standalone, reusable tooling — as opposed to the per-directory `derive.sh`
build recipes under `cpm/resources/*/` (e.g. `cpm/resources/sargon/derive.sh`),
which are one-off translations tightly coupled to their own upstream
source and stay colocated with it. Anything general enough to be useful
outside the one place it was first needed lives here instead.

- **`config.sh`** — `source scripts/config.sh` from the repo root puts
  `bin/` on `PATH` (and sets `$BASEDIR` to the repo root), so
  `z80`/`z80asm`/`z80dasm` work without the `./bin/` prefix. Optional —
  everything also works as `./bin/z80` etc.
- **`8080_to_z80.py`** — a general-purpose 8080-mnemonic → Z80-mnemonic
  assembly translator (register-pair renaming, `M`→`(HL)`, `PSW`→`AF`,
  condition-code jump/call/return forms, which ALU ops need an explicit
  `A,` operand vs. not). Built for `cpm/resources/ccp/`'s CCP source (CP/M
  predates the Z80, so Digital Research's own CCP is written entirely in
  8080 mnemonics), but not CCP-specific — usage:
  `python3 scripts/8080_to_z80.py <input.asm> <output.asm>`. See its own
  header comment for the full approach, and `cpm/resources/ccp/derive.sh` for
  a real example of driving it as part of a larger build pipeline.
