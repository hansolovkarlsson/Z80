#!/usr/bin/env bash
# Automated regression check, wrapping the manual "eyeball the console
# output" verification described in CLAUDE.md/ROADMAP.md. Two kinds of
# checks:
#   - The ZEXALL/ZEXDOC exercisers: fail if the output contains an ERROR
#     line, an unimplemented-opcode line, or never reaches "Tests complete".
#   - Every asm/examples/*.asm program: assemble it, run it, fail if the
#     output contains a FAIL line (the OK-n/FAIL-n convention used by
#     selftest.asm/gaps_test.asm) or an unimplemented-opcode line.
# Not a general framework - just enough to turn "did I break anything" into
# an exit code instead of a manual read of console output.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT"

Z80="bin/z80"
Z80ASM="bin/z80asm"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

overall_status=0

check_exerciser() {
    local name="$1" path="$2"
    local out
    out=$("$Z80" "$path" 2>&1)
    local status=0

    if echo "$out" | grep -q "ERROR"; then
        echo "FAIL: $name reported one or more ERROR lines"
        status=1
    fi
    if echo "$out" | grep -qi "Unimplemented opcode"; then
        echo "FAIL: $name hit an unimplemented opcode"
        status=1
    fi
    if ! echo "$out" | grep -q "Tests complete"; then
        echo "FAIL: $name never reached 'Tests complete'"
        status=1
    fi

    if [ "$status" -eq 0 ]; then
        echo "PASS: $name"
    else
        echo "$out" | grep -E "ERROR|Unimplemented" | sed 's/^/    /'
        overall_status=1
    fi
}

check_asm_example() {
    local src="$1"
    local name
    name="$(basename "$src")"
    local com="$WORKDIR/$name.com"

    local asm_log
    if ! asm_log=$("$Z80ASM" "$src" -o "$com" 2>&1); then
        echo "FAIL: $name failed to assemble"
        echo "$asm_log" | sed 's/^/    /'
        overall_status=1
        return
    fi

    local out
    out=$("$Z80" "$com" 2>&1)
    local status=0

    if echo "$out" | grep -qi "FAIL"; then
        echo "FAIL: $name reported one or more FAIL lines"
        status=1
    fi
    if echo "$out" | grep -qi "Unimplemented opcode"; then
        echo "FAIL: $name hit an unimplemented opcode"
        status=1
    fi

    if [ "$status" -eq 0 ]; then
        echo "PASS: $name"
    else
        echo "$out" | grep -Ei "FAIL|Unimplemented" | sed 's/^/    /'
        overall_status=1
    fi
}

if [ ! -x "$Z80" ] || [ ! -x "$Z80ASM" ]; then
    echo "tests/run_tests.sh: bin/z80 and bin/z80asm must be built first (run 'make')" >&2
    exit 1
fi

check_exerciser "ZEXALL" emu/zexall/ZEXALL-main/zexall.com
check_exerciser "ZEXDOC" emu/zexall/ZEXALL-main/zexdoc.com

for src in asm/examples/*.asm; do
    check_asm_example "$src"
done

exit "$overall_status"
