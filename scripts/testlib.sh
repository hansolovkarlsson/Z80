# scripts/testlib.sh - the shared reporting primitives for the machine
# targets' regression suites (abc80/tests/, abc802/tests/). Sourced, not
# executed.
#
# It exists because both suites do the same thing to different machines:
# run a real emulator over real ROMs, capture what it produced, and assert
# on it. Only the assertions differ. cpm/tests/run_tests.sh predates this
# and keeps its own helpers - its checks are shaped around .com programs
# that self-report OK-n/FAIL-n, which is a different problem, and
# retrofitting it would mean touching a working suite for no gain.
#
# Conventions, matching cpm/tests/run_tests.sh so all three read alike:
#   - one "PASS: name" or "FAIL: name" line per check
#   - failure detail indented four spaces beneath it
#   - a nonzero exit if anything failed
#
# A check is a tl_begin / one or more tl_want / tl_end sandwich, so a
# check with several assertions still reports one line and names every
# assertion that failed rather than only the first.

tl_status=0
tl_passed=0
tl_failed=0
tl_skipped=0
tl_current=""
tl_reasons=""

tl_begin() {
    tl_current="$1"
    tl_reasons=""
}

# tl_want <haystack> <needle> [description]
# Records a failure if <needle> is not a substring of <haystack>.
tl_want() {
    local haystack="$1" needle="$2" desc="${3:-}"
    case "$haystack" in
        *"$needle"*) ;;
        *) tl_reasons="${tl_reasons}    expected ${desc:-\"$needle\"} in the output"$'\n' ;;
    esac
}

# tl_want_not <haystack> <needle> [description]
tl_want_not() {
    local haystack="$1" needle="$2" desc="${3:-}"
    case "$haystack" in
        *"$needle"*) tl_reasons="${tl_reasons}    did not expect ${desc:-\"$needle\"} in the output"$'\n' ;;
    esac
}

# tl_want_eq <actual> <expected> <description>
tl_want_eq() {
    local actual="$1" expected="$2" desc="$3"
    if [ "$actual" != "$expected" ]; then
        tl_reasons="${tl_reasons}    $desc: expected '$expected', got '$actual'"$'\n'
    fi
}

# tl_note <text> - record a failure reason directly, for conditions that
# are not a substring test (a missing file, a nonzero exit).
tl_note() {
    tl_reasons="${tl_reasons}    $1"$'\n'
}

# tl_end [context]
# Emits the single PASS/FAIL line. On failure, prints the collected
# reasons and, if given, a context block (typically the captured output)
# indented beneath them.
#
# Only the tail of the context is shown. These emulators print an
# instruction trace before anything interesting, so dumping the whole
# capture buries the screen render - which is the part that actually
# explains the failure - under a hundred lines of opcodes.
TL_CONTEXT_LINES=${TL_CONTEXT_LINES:-32}

tl_end() {
    local context="${1:-}"
    if [ -z "$tl_reasons" ]; then
        echo "PASS: $tl_current"
        tl_passed=$((tl_passed + 1))
    else
        echo "FAIL: $tl_current"
        printf '%s' "$tl_reasons"
        if [ -n "$context" ]; then
            local total
            total=$(printf '%s\n' "$context" | wc -l | tr -d ' ')
            if [ "$total" -gt "$TL_CONTEXT_LINES" ]; then
                echo "      ... $((total - TL_CONTEXT_LINES)) earlier lines omitted; set TL_CONTEXT_LINES to see more"
            fi
            printf '%s\n' "$context" | tail -n "$TL_CONTEXT_LINES" | sed 's/^/      | /'
        fi
        tl_failed=$((tl_failed + 1))
        tl_status=1
    fi
    tl_current=""
    tl_reasons=""
}

# tl_skip <name> <why>
# A skip is not a pass. It prints loudly and is counted separately, so a
# suite that silently ran nothing cannot be mistaken for a green one.
tl_skip() {
    echo "SKIP: $1 - $2"
    tl_skipped=$((tl_skipped + 1))
}

# tl_fixture <name> <fixture_path> <actual>
# Diffs actual output against a committed fixture, the same pattern
# cpm/tests/run_tests.sh uses for z80dasm. Lines beginning with ';' in
# the fixture are provenance comments and are stripped before comparing.
tl_fixture() {
    local name="$1" fixture="$2" actual="$3"
    tl_begin "$name"
    if [ ! -f "$fixture" ]; then
        tl_note "fixture $fixture is missing"
        tl_end
        return
    fi
    local expected
    expected="$(grep -v '^;' "$fixture")"
    if [ "$actual" != "$expected" ]; then
        tl_note "output does not match $fixture"
        tl_reasons="${tl_reasons}$(diff <(printf '%s\n' "$expected") <(printf '%s\n' "$actual") | head -40 | sed 's/^/      /')"$'\n'
    fi
    tl_end
}

# tl_summary <suite name> - final tally plus the exit status to use.
tl_summary() {
    echo
    if [ "$tl_skipped" -gt 0 ]; then
        echo "$1: $tl_passed passed, $tl_failed failed, $tl_skipped skipped"
    else
        echo "$1: $tl_passed passed, $tl_failed failed"
    fi
    return "$tl_status"
}
