#!/usr/bin/env bash
# Automated regression check, wrapping the manual "eyeball the console
# output" verification described in CLAUDE.md/cpm/docs/ROADMAP.md. Three
# kinds of checks:
#   - The ZEXALL/ZEXDOC exercisers: fail if the output contains an ERROR
#     line, an unimplemented-opcode line, or never reaches "Tests complete".
#   - Every asm/examples/*.asm program (top-level asm/, not under cpm/ -
#     it's a generic Z80 tool, see CLAUDE.md): assemble it, run it, fail if the
#     output contains a FAIL line (the OK-n/FAIL-n convention used by
#     selftest.asm/gaps_test.asm) or an unimplemented-opcode line.
#   - bin/z80-test-interrupts: a direct C-level unit test (not a .asm
#     program - see cpm/tests/test_interrupts.c's own top comment for
#     why interrupt acceptance specifically can't be tested that way),
#     using the identical FAIL-line convention as the .asm checks above.
#   - z80dasm's own regression: disassembles asm/examples/hello.asm and
#     diffs the output against the committed disasm/examples/hello.dasm.txt
#     fixture - the only automated coverage this tool has (previously
#     spot-checked by eye only, see cpm/docs/ROADMAP.md's disassembler
#     section).
# Not a general framework - just enough to turn "did I break anything" into
# an exit code instead of a manual read of console output.

set -uo pipefail

# ROOT is the true repo root (bin/ lives there) - two levels up from
# this script's own cpm/tests/ location, not one, since this script
# itself lives a level deeper than the top-level layout might suggest.
# CWD is then set to cpm/ (not ROOT) so the emu/zexall/... relative path
# below keeps working unchanged relative to where the actual CP/M
# subproject files live. asm/examples/*.asm lives at the repo root
# instead (a generic Z80 tool, not CP/M-specific - see CLAUDE.md), so
# those references are built off $ROOT explicitly rather than relying
# on this cd.
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT/cpm"

Z80="$ROOT/bin/z80"
Z80ASM="$ROOT/bin/z80asm"
Z80DASM="$ROOT/bin/z80dasm"
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
    local src="$ROOT/asm/examples/term_test.asm" name="term_test.asm"
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

check_disasm_example() {
    local src="$ROOT/asm/examples/hello.asm" name="hello.asm"
    local fixture="$ROOT/disasm/examples/hello.dasm.txt"
    local com="$WORKDIR/$name.com"

    local asm_log
    if ! asm_log=$("$Z80ASM" "$src" -o "$com" 2>&1); then
        echo "FAIL: z80dasm/$name failed to assemble"
        echo "$asm_log" | sed 's/^/    /'
        overall_status=1
        return
    fi

    local actual expected
    actual=$("$Z80DASM" "$com" 2>&1)
    # The fixture's header is a `;`-prefixed provenance comment, same
    # convention as a real assembler source file's own comments - strip
    # it before comparing against the real tool's output.
    expected=$(grep -v '^;' "$fixture")

    if [ "$actual" == "$expected" ]; then
        echo "PASS: z80dasm/$name"
    else
        echo "FAIL: z80dasm/$name output doesn't match $fixture"
        diff <(echo "$expected") <(echo "$actual") | sed 's/^/    /'
        overall_status=1
    fi

    # Bare numbers are hex, as in the debugger. `-o 0100` used to be read
    # as octal and load the file at 0040h; `-l 34` is 0x34 bytes, which
    # ends on the JP 0000h at 0131.
    local org last reasons=""
    org=$("$Z80DASM" "$com" -o 0100 2>&1 | head -1)
    last=$("$Z80DASM" "$com" -l 34 2>&1 | tail -1)
    case "$org" in *"org 0100h"*) ;; *) reasons="${reasons}    -o 0100 gave '$org', expected org 0100h"$'\n' ;; esac
    case "$last" in *"; 0131:"*) ;; *) reasons="${reasons}    -l 34 ended on '$last', expected the instruction at 0131"$'\n' ;; esac
    if "$Z80DASM" "$com" -o 12G > /dev/null 2>&1; then
        reasons="${reasons}    -o 12G was accepted, expected an error"$'\n'
    fi
    if [ -z "$reasons" ]; then
        echo "PASS: z80dasm/hex-arguments"
    else
        echo "FAIL: z80dasm/hex-arguments"
        printf '%s' "$reasons"
        overall_status=1
    fi
}

check_c_unit_test() {
    local name="$1" binary="$2"
    local out
    out=$("$binary" 2>&1)
    local status=0

    if echo "$out" | grep -qi "^FAIL"; then
        echo "FAIL: $name reported one or more FAIL lines"
        status=1
    fi

    if [ "$status" -eq 0 ]; then
        echo "PASS: $name"
    else
        echo "$out" | grep -i "^FAIL" | sed 's/^/    /'
        overall_status=1
    fi
}

# The debugger (docs/DEBUGGER.md), driven by a script the way a person
# would type it. Each check asserts on what the debugger printed *and* on
# what the program then did, since a debugger that stops correctly but
# leaves the machine broken is worse than none.
debugger_run() {
    local com="$1" script="$2"
    printf '%s' "$script" > "$WORKDIR/dbg.txt"
    (cd "$WORKDIR" && "$Z80" --debug --debug-script "$WORKDIR/dbg.txt" "$com" < /dev/null 2>&1)
}

report() {
    local name="$1" reasons="$2" out="$3"
    if [ -z "$reasons" ]; then
        echo "PASS: $name"
    else
        echo "FAIL: $name"
        printf '%s\n' "$reasons"
        echo "$out" | tail -20 | sed 's/^/      | /'
        overall_status=1
    fi
}

want() {
    case "$1" in *"$2"*) ;; *) printf '    expected %s\n' "$3" ;; esac
}

check_debugger() {
    local hello="$WORKDIR/dbg-hello.com" strutil="$WORKDIR/dbg-strutil.com"
    "$Z80ASM" "$ROOT/asm/examples/hello.asm" -o "$hello" > /dev/null 2>&1
    "$Z80ASM" "$ROOT/asm/examples/strutil.asm" -o "$strutil" > /dev/null 2>&1

    # Breakpoint, disassembly, and `n` over a BDOS call. CP/M's z80_step()
    # runs an intercepted BDOS call and the instruction after it in one
    # step, which is exactly what `n` has to survive.
    local out reasons
    out=$(debugger_run "$hello" $'b 0105\nc\nn\nc\n')
    reasons="$(want "$out" "[breakpoint] 0105  CD 05 00     CALL 0005h" "the stop at 0105, with its instruction")
$(want "$out" "[returned] 010A" "n stopping right after the BDOS call")
$(want "$out" "Hello from z80asm!" "the program's own output")
$(want "$out" "terminated normally" "the program running to its end afterwards")"
    report "debugger-break-and-next" "$(printf '%s' "$reasons" | grep .)" "$out"

    # Setting a register changes the machine: hello.asm checks its own loop
    # counter, so corrupting HL mid-loop makes it report the failure.
    out=$(debugger_run "$hello" $'b 010D\nc\nr hl=1234\nd all\nc\n')
    reasons="$(want "$out" "HL=1234" "the register shown with its new value")
$(want "$out" "loop counter wrong" "the program noticing the changed register")"
    report "debugger-register-set" "$(printf '%s' "$reasons" | grep .)" "$out"

    # An empty line repeats the last step *with its count*: `s 2` then an
    # empty line is four instructions, so DJNZ has taken B from 5 to 3.
    # Repeating a bare `s` instead stops after three, with B still at 4.
    out=$(debugger_run "$hello" $'b 010D\nc\ns 2\n\nq\n')
    reasons="$(want "$out" "BC=0309" "the fourth step, from an empty line repeating s 2")"
    report "debugger-step-repeat" "$(printf '%s' "$reasons" | grep .)" "$out"

    # Symbols: z80asm -s writes them, the debugger reads them. Addresses are
    # taken from the symbol file, so the debugger is checked against the
    # assembler rather than against itself.
    local sym="$WORKDIR/dbg-hello.sym" fail_addr
    "$Z80ASM" "$ROOT/asm/examples/hello.asm" -o "$WORKDIR/dbg-hello-sym.com" -s "$sym" > /dev/null 2>&1
    fail_addr=$(awk '$1 == "fail" { sub(/h$/, "", $3); print $3 }' "$sym")
    reasons="$(want "$(cat "$sym")" "equ 0005h    ; equ" "BDOS recorded as an equ, not a label")
$(want "$(cat "$sym")" "count_loop               equ 010Dh    ; label" "count_loop recorded as a label")"
    printf 'u count_loop 2\nu start 3\nb fail\nm msg 5\nu count_loop+3 1\nd count_loop\nr hl=4\nc\nq\n' > "$WORKDIR/dbg.txt"
    out=$(cd "$WORKDIR" && "$Z80" --symbols "$sym" --break count_loop --debug-script "$WORKDIR/dbg.txt" "$WORKDIR/dbg-hello-sym.com" < /dev/null 2>&1)
    reasons="$reasons
$(want "$out" "[breakpoint] count_loop:" "--break by name, and the stop named")
$(want "$out" "DJNZ count_loop" "a jump target named in the instruction")
$(want "$out" "breakpoint $fail_addr fail" "b by name, at the address z80asm gave fail")
$(want "$out" "Hello" "m msg dumping the message the symbol points at")
$(want "$out" "0110  7D" "count_loop+3 resolving to 0110")
$(want "$out" "CALL 0005h" "an equ (BDOS) never used to name an address")
$(want "$out" "[breakpoint] fail:" "the run stopping at fail after the counter is corrupted")"
    report "debugger-symbols" "$(printf '%s' "$reasons" | grep .)" "$out"

    # `e` writes memory. Data: the greeting's H becomes J, under a watch
    # that must not report the debugger's own write. Code: `cp 5` becomes
    # `cp 6` (its operand is at 0112), so the program's check fails. And a
    # line with one bad byte must write none of them.
    out=$(debugger_run "$hello" $'w 0134 1\ne 0134 4A\ne 0135 58 zz\nc\n')
    reasons="$(want "$out" "Jello from z80asm!" "the data write reaching the program")
$(want "$out" "e: usage" "a bad byte rejected")"
    case "$out" in *"[watch]"*) reasons="$reasons
    the watch fired on the debugger's own write" ;; esac
    case "$out" in *"JXllo"*) reasons="$reasons
    a line with a bad byte still wrote the good one" ;; esac
    local code_out
    code_out=$(debugger_run "$hello" $'e 0112 06\nc\n')
    reasons="$reasons
$(want "$code_out" "loop counter wrong" "the patched cp 6 failing the program's own check")"
    report "debugger-memory-write" "$(printf '%s' "$reasons" | grep .)" "$out$code_out"

    # A watchpoint names the instruction that wrote. The expected addresses
    # come from z80dasm, not from the debugger, so the two cannot agree by
    # sharing a mistake.
    local store var line
    line=$("$Z80DASM" "$strutil" 2>/dev/null | grep -m1 -E 'LD \(D[0-9A-F]{4}\),HL')
    var=$(printf '%s' "$line" | sed -E 's/.*LD \(D([0-9A-F]{4})\).*/\1/')
    store=$(printf '%s' "$line" | sed -E 's/.*; ([0-9A-F]{4}):.*/\1/')
    out=$(debugger_run "$strutil" "w $var 2"$'\nc\nc\n')
    reasons="$(want "$out" "[watch] $var:" "the watch on $var firing")
$(want "$out" "written by the instruction at $store" "the store at $store named as the writer")"
    report "debugger-watch" "$(printf '%s' "$reasons" | grep .)" "$out"
}

TEST_INTERRUPTS="$ROOT/bin/z80-test-interrupts"

if [ ! -x "$Z80" ] || [ ! -x "$Z80ASM" ] || [ ! -x "$Z80DASM" ] || [ ! -x "$TEST_INTERRUPTS" ]; then
    echo "tests/run_tests.sh: bin/z80, bin/z80asm, bin/z80dasm, and bin/z80-test-interrupts must be built first (run 'make test')" >&2
    exit 1
fi

check_exerciser "ZEXALL" emu/zexall/ZEXALL-main/zexall.com
check_exerciser "ZEXDOC" emu/zexall/ZEXALL-main/zexdoc.com
check_c_unit_test "test_interrupts" "$TEST_INTERRUPTS"
check_disasm_example

for src in "$ROOT"/asm/examples/*.asm; do
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
check_debugger

exit "$overall_status"
