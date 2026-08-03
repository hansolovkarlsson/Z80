#!/usr/bin/env bash
# Reproduces ccp_cpm.asm from the unmodified upstream/ccp.asm: first
# preprocess.py resolves the conditionals/directives z80asm can't handle
# (see that file's own comment for why), then derive.py translates the
# whole thing from 8080 to Z80 mnemonics (see its own comment for the
# general approach). Two separate scripts because only the mnemonic
# translation is generally reusable - the conditional/origin resolution
# is specific to this file.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"

STAGE1="$(mktemp)"
trap 'rm -f "$STAGE1"' EXIT

python3 preprocess.py upstream/ccp.asm "$STAGE1"

cat > ccp_cpm.asm <<'HEADER'
; Derived from upstream/ccp.asm (see upstream/README.md for provenance/
; license) via derive.sh (preprocess.py + derive.py) in this directory -
; NOT hand-edited. Regenerate with ./derive.sh rather than editing this
; file directly.
;
HEADER

python3 derive.py "$STAGE1" /dev/stdout >> ccp_cpm.asm

echo "Wrote ccp_cpm.asm ($(wc -l < ccp_cpm.asm) lines)"
