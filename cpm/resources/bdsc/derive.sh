#!/usr/bin/env bash
# Compiles and links examples/hello.c and examples/fib.c through the
# real CC.COM/CLINK.COM under this project's own emulator, then runs
# each resulting .COM and checks its output - a real, reproducible build
# *and* correctness check for the toolchain itself (unlike the other
# resources/*/derive.sh scripts, there's no single source file being
# translated/patched here; BDS C is a toolchain, and its own compiler/
# linker are what's under test). fib.c in particular exercises
# recursion, a for loop, and multi-argument printf, not just "does it
# boot" - hello.c alone wouldn't catch a broken function-call or
# argument-passing convention.
#
# This also depends on this project's own command-line-tail/default-FCB
# support in cpm/emu/src/main.c (bin/z80 <program.com> <args...>) - CC.COM
# and CLINK.COM both read their filename argument from the default FCB
# at 0x005C the way a real CCP would populate it, not from argv/getopt-
# style parsing. Found and fixed via this exact package: before that
# support existed, CC.COM either printed its usage message (no argument
# reached it at all) or "Cannot open" a filename made of NUL bytes (the
# still-zeroed FCB) - see CLAUDE.md's File I/O section.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
Z80="../../../bin/z80"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

mkdir -p "$WORK/cpm_disk"
cp upstream/*.COM upstream/*.CRL upstream/C.CCC "$WORK/cpm_disk/"

for prog in hello fib; do
    cp "examples/$prog.c" "$WORK/cpm_disk/$(echo "$prog" | tr a-z A-Z).C"
done

(
    cd "$WORK"
    for prog in HELLO FIB; do
        "$OLDPWD/$Z80" cpm_disk/CC.COM "$prog.C" > "cc_$prog.log" 2>&1
        if ! grep -q 'to spare' "cc_$prog.log"; then
            echo "$prog.C failed to compile - see cc_$prog.log:" >&2
            cat "cc_$prog.log" >&2
            exit 1
        fi
        "$OLDPWD/$Z80" cpm_disk/CLINK.COM "$prog" > "clink_$prog.log" 2>&1
        if ! grep -q 'link space remaining' "clink_$prog.log"; then
            echo "$prog failed to link - see clink_$prog.log:" >&2
            cat "clink_$prog.log" >&2
            exit 1
        fi
    done
)

RUN_HELLO="$("$Z80" "$WORK/cpm_disk/HELLO.COM")"
if ! grep -q 'Hello from BDS C!' <<<"$RUN_HELLO"; then
    echo "hello.com produced unexpected output:" >&2
    echo "$RUN_HELLO" >&2
    exit 1
fi

RUN_FIB="$("$Z80" "$WORK/cpm_disk/FIB.COM")"
if ! grep -q 'fib(9) = 34' <<<"$RUN_FIB"; then
    echo "fib.com produced unexpected output:" >&2
    echo "$RUN_FIB" >&2
    exit 1
fi

cp "$WORK/cpm_disk/HELLO.COM" hello.com
cp "$WORK/cpm_disk/FIB.COM" fib.com
echo "Wrote hello.com, fib.com - both compiled, linked, and verified via the real BDS C toolchain"
