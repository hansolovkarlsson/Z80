#!/usr/bin/env bash
# Automated regression check, wrapping the manual "eyeball the console
# output" verification described in CLAUDE.md/cpm/docs/ROADMAP.md. Two
# kinds of checks:
#   - The ZEXALL/ZEXDOC exercisers: fail if the output contains an ERROR
#     line, an unimplemented-opcode line, or never reaches "Tests complete".
#   - Every asm/examples/*.asm program: assemble it, run it, fail if the
#     output contains a FAIL line (the OK-n/FAIL-n convention used by
#     selftest.asm/gaps_test.asm) or an unimplemented-opcode line.
# Not a general framework - just enough to turn "did I break anything" into
# an exit code instead of a manual read of console output.

set -uo pipefail

# ROOT is the true repo root (bin/ lives there, shared with gameboy/'s own
# build) - two levels up from this script's own cpm/tests/ location, not
# one, since this script itself lives a level deeper than the top-level
# layout might suggest. CWD is then set to cpm/ (not ROOT) so every
# relative path below (asm/examples/*.asm, emu/zexall/...) keeps working
# unchanged relative to where the actual CP/M subproject files live.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT/cpm"

Z80="$ROOT/bin/z80"
Z80ASM="$ROOT/bin/z80asm"
WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

overall_status=0

check_exerciser() {
    local name="$1" path="$2"
    local out
    out=$("$Z80" "$path" < /dev/null 2>&1)
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
    local src="$1" stdin_data="${2:-}"
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

    # Run from WORKDIR, not the repo root, so any file-I/O example (see
    # cpm.c's File I/O comment) creates its cpm_disk/ directory in a
    # throwaway location instead of the checkout itself.
    local out
    if [ -n "$stdin_data" ]; then
        out=$(cd "$WORKDIR" && printf '%s' "$stdin_data" | "$Z80" "$com" 2>&1)
    else
        out=$(cd "$WORKDIR" && "$Z80" "$com" < /dev/null 2>&1)
    fi
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

check_term_test() {
    # term_test.asm has no OK-n/FAIL-n self-check to grep for - unlike
    # file I/O, there's no CP/M-visible way for a program to read back its
    # own console output, so this checks the raw byte stream directly for
    # console_emit()'s legacy-terminal-protocol translation (ADM-3A/VT52,
    # see cpm.c's own comment) instead of delegating to check_asm_example.
    local src="asm/examples/term_test.asm" name="term_test.asm"
    local com="$WORKDIR/$name.com"

    local asm_log
    if ! asm_log=$("$Z80ASM" "$src" -o "$com" 2>&1); then
        echo "FAIL: $name failed to assemble"
        echo "$asm_log" | sed 's/^/    /'
        overall_status=1
        return
    fi

    local out
    out=$(cd "$WORKDIR" && "$Z80" "$com" < /dev/null 2>&1)
    local status=0

    echo "$out" | grep -qF $'\x1b[2;2H' || { echo "FAIL: $name - ADM-3A cursor addressing (ESC = row col) not translated to ANSI"; status=1; }
    echo "$out" | grep -qF $'\x1b[1;1H' || { echo "FAIL: $name - VT52 cursor addressing (ESC Y row col) not translated to ANSI"; status=1; }
    echo "$out" | grep -qF $'\x1b[K' || { echo "FAIL: $name - VT52 erase-to-EOL (ESC K) not translated to ANSI"; status=1; }
    echo "$out" | grep -qF $'\x1b[2K' || { echo "FAIL: $name - H19 erase-line (ESC l) not translated to ANSI"; status=1; }
    echo "$out" | grep -qF $'\x1b[1m' || { echo "FAIL: $name - real ANSI SGR passthrough (ESC [ 1 m) was altered"; status=1; }

    if [ "$status" -eq 0 ]; then
        echo "PASS: $name"
    else
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
    case "$(basename "$src")" in
        # Needs specific piped stdin to drive its BDOS console-input
        # checks (C_READ/C_RAWIO/C_READSTR) - see the .asm file's header
        # comment for exactly what each byte is for.
        console_test.asm) check_asm_example "$src" $'ABOK\r' ;;
        # Not an OK-n/FAIL-n self-check - see check_term_test() above.
        term_test.asm) ;;
        *) check_asm_example "$src" ;;
    esac
done

check_term_test

exit "$overall_status"
