#!/usr/bin/env bash
# Compiles upstream/hanoi-p.pas into hanoi.com via the real TURBO.COM
# (resources/turbopascal/TURBO.COM, already ANSI/Ctrl-H-configured by
# that directory's own derive.sh) running under this project's own
# emulator - there's no separate "assembler" step here the way
# asm/examples/*.asm or the other resources/*/derive.sh scripts have;
# TURBO.COM *is* the build tool, the same role TINST.COM plays in
# resources/turbopascal/derive.sh itself. Unlike sargon/tastybasic, the
# real upstream source needs no dialect translation - hanoi-p.pas is
# valid Turbo Pascal 3.01A as-is, so this script's only job is driving
# TURBO.COM's own menu-driven UI to compile it to a standalone .COM
# file (Options -> compile -> Com-file, then Work file + Compile),
# rather than the default in-memory compilation.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
Z80="../../../bin/z80"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/cpm_disk"
cp ../turbopascal/TURBO.COM ../turbopascal/TURBO.MSG ../turbopascal/TURBO.OVR "$WORK/cpm_disk/"
cp upstream/hanoi-p.pas "$WORK/cpm_disk/HANOI.PAS"

(
    cd "$WORK"
    # Same real-per-character-pause requirement as every other derive.sh
    # driving this project's own emulator interactively (see
    # resources/turbopascal/derive.sh's own comment) - piped input
    # arriving faster than TURBO.COM's own input polling gets read
    # ahead of the prompt asking for it.
    {
        sleep 1;   printf 'N\r'   # Include error messages? No
        sleep 1;   printf 'O'     # Options menu
        sleep 1;   printf 'C'     # compile -> Com-file (not the default Memory)
        sleep 1;   printf 'Q'     # back to main menu
        sleep 1;   printf 'W'     # Work file
        sleep 0.5; printf 'HANOI\r'
        sleep 3;   printf 'C'     # Compile (492 lines, ~10s of emulated time)
        sleep 12;  printf 'Q'     # Quit
        sleep 1;   printf 'Y'     # confirm quit without saving (already compiled to disk)
        sleep 1
    } | "$OLDPWD/$Z80" cpm_disk/TURBO.COM > compile.log 2>&1 || true
)

if ! grep -q '492 lines' "$WORK/compile.log" || grep -q 'Error' "$WORK/compile.log"; then
    echo "Compile failed - see:" >&2
    cat "$WORK/compile.log" >&2
    exit 1
fi

cp "$WORK/cpm_disk/HANOI.COM" hanoi.com
echo "Wrote hanoi.com ($(wc -c < hanoi.com) bytes) - compiled from upstream/hanoi-p.pas, 492 lines, 0 errors"
