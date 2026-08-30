#!/usr/bin/env bash
# abc806/tests/run_tests.sh - regression checks for bin/abc806.
#
# Same shape and reasoning as the other two machine suites: drive the real
# committed ROMs and assert on what the machine produced.
#
# This target is younger than the others and the checks reflect that. There
# is no screen content to assert on yet - the ROM clears the display and
# writes no visible text (see ABC806_ROADMAP.md) - so the boot check asserts
# the machine's *configuration* instead, and the character decode is covered
# entirely by a synthetic-screen fixture. That division is not a compromise:
# per docs/postmortems/2026-08-28-boot-screen-cannot-validate.md a boot
# screen could not have validated the attribute decode anyway.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../scripts/testlib.sh
. "$ROOT/scripts/testlib.sh"
cd "$ROOT"

ABC806="$ROOT/bin/abc806"
CHARGEN_DUMP="$ROOT/bin/abc806-chargen-dump"
FIXTURES="$ROOT/abc806/tests/fixtures"

for binary in "$ABC806" "$CHARGEN_DUMP"; do
    if [ ! -x "$binary" ]; then
        echo "abc806/tests/run_tests.sh: $binary is missing (run 'make test')" >&2
        exit 1
    fi
done

# --- Boot ------------------------------------------------------------
#
# The machine has to get through initialisation and configure its own
# video. 80x25 is this machine's geometry, not the ABC802's 80x24, so a
# regression that quietly reverted to the older target's numbers would
# show here.
out=$("$ABC806" --cycles 20000000 --screen 2>&1)
tl_begin "boot-crtc-programmed"
tl_want "$out" "CRTC programmed: yes (R1=80 cols, R6=25 rows)" "the ROM configuring its own 80x25 display"
tl_want "$out" "Character RAM: 2000/2048 nonzero" "the ROM clearing exactly 80x25 cells"
tl_end "$out"

# Both committed DOS PROMs must boot. They are the same UFD-DOS v.19/v.20
# pair the ABC802 carries, and a memory-map regression would be unlikely
# to break both identically.
out=$("$ABC806" --cycles 20000000 --dos-rom ABC806-dos.66-21.bin 2>&1)
tl_begin "boot-alternate-dos-prom"
tl_want "$out" "CRTC programmed: yes" "UFD-DOS v.19 booting as well as v.20"
tl_end "$out"

# --- The character and attribute decode -------------------------------
#
# The only check on chargen.c. Covers colours, underline, flash, blank,
# keep-previous and double-width, none of which the boot screen touches.
tl_begin "chargen-attributes"
if diff -q "$FIXTURES/chargen.txt" <("$CHARGEN_DUMP" 2>&1) >/dev/null; then
    tl_end
else
    tl_note "chargen dump differs from the fixture; run abc806/tests/regen-fixtures.sh and read the diff"
    tl_end "$(diff "$FIXTURES/chargen.txt" <("$CHARGEN_DUMP" 2>&1) | head -20)"
fi

tl_summary "abc806"
