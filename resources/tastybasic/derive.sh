#!/usr/bin/env bash
# Reproduces tastybasic_cpm.asm from the unmodified upstream/ sources.
#
# tastybasic.asm targets uz80as (https://jorgicor.niobe.org/uz80as), whose
# preprocessor is a real C-preprocessor clone: #ifdef/#define/#include,
# selecting the CP/M variant via a `-dCPM` command-line define. z80asm
# (this project's assembler) has its own, much simpler preprocessor
# (IF/ELSE/ENDIF, MACRO/ENDM) and doesn't understand C-preprocessor
# directives at all, so getting this real, unmodified source to build
# needs an actual C preprocessor step first - the system `clang`/`cpp` is
# used here since its preprocessor is exactly what uz80as's own is modeled
# on, standing in for uz80as itself (which isn't readily available as a
# package and needs autotools to build from source).
#
# One real gotcha along the way (see docs/ROADMAP.md and the git history
# around this file for the full story): uz80as's dwa(addr) macro is
# defined as
#   #define dwa(addr) .db (addr >> 8) + 080h\ .db addr & 0ffh
# using a bare mid-line backslash as a "second statement" separator - a
# uz80as-specific preprocessor extension. A real C preprocessor only
# treats \<newline> as line continuation; a backslash NOT immediately
# followed by a newline (as here) is passed through as a literal
# character, so clang's output has BOTH .db statements crammed onto one
# line separated by a stray backslash. Left uncorrected, z80asm parses
# that whole line as a single DB's operand list and silently truncates at
# the backslash, emitting only the first byte of every jump-table entry -
# which is exactly the bug that broke this program's entire command
# dispatch table until it was diagnosed (see the commit that added
# JP (HL)/shift-operator/label-alone-on-a-line fixes). The `perl -pe`
# step below splits every such backslash back into a real newline.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

clang -E -DCPM -x c -P -I upstream upstream/tastybasic.asm -o /tmp/tastybasic_stage1.asm

perl -pe 's/\.db\b/DB/g; s/\.equ\b/EQU/g; s/\.ds\b/DS/g; s/\.org\b/ORG/g;' \
    /tmp/tastybasic_stage1.asm > /tmp/tastybasic_stage2.asm

perl -i -pe 's/\\\s*/\n    /g' /tmp/tastybasic_stage2.asm

cat > tastybasic_cpm.asm <<'HEADER'
; Derived from upstream/tastybasic.asm + upstream/cpmio.asm (GPLv3, see
; upstream/LICENSE) via derive.sh in this directory - NOT hand-edited.
; Regenerate with ./derive.sh rather than editing this file directly.
;
HEADER

cat /tmp/tastybasic_stage2.asm >> tastybasic_cpm.asm
rm -f /tmp/tastybasic_stage1.asm /tmp/tastybasic_stage2.asm

echo "Wrote tastybasic_cpm.asm ($(wc -l < tastybasic_cpm.asm) lines)"
