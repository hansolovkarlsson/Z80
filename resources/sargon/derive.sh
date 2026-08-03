#!/usr/bin/env bash
# Reproduces sargon_cpm.asm from the unmodified upstream/sargon-cpm.asm.
#
# The source is otherwise a clean, real Z80 program (JP used correctly in
# 70+ places) but has 3 stray 8080-style mnemonics left over in its
# hand-written CP/M console-I/O wrapper (con_in/con_outs, near the end of
# the file) - JMP where the rest of the file uses JP, and CMP (the 8080
# accumulator-compare mnemonic) where Z80 uses CP. Not systematic 8080
# dialect (this project's assembler deliberately doesn't speak 8080 - see
# resources/user_prompt.txt's manual translation for that case), just 3
# isolated typos/inconsistencies in an otherwise-real-Z80 file, confirmed
# by grepping the whole source for both mnemonics before writing this.
# whole-word substitution (perl, not sed -E, since BSD/macOS sed doesn't
# support \b) is safe here since both are 100% unambiguous in this file
# (3 exact matches, nothing else looks like either mnemonic).

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

cat > sargon_cpm.asm <<'HEADER'
; Derived from upstream/sargon-cpm.asm (see upstream/README.md for
; provenance/license) via derive.sh in this directory - NOT hand-edited.
; Regenerate with ./derive.sh rather than editing this file directly.
;
HEADER

perl -pe 's/\bjmp\b/jp /g; s/\bcmp\b/cp /g' upstream/sargon-cpm.asm >> sargon_cpm.asm

echo "Wrote sargon_cpm.asm ($(wc -l < sargon_cpm.asm) lines)"
