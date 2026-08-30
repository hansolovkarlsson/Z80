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

# --- The keyboard, and BASIC answering ---------------------------------
#
# Milestone 3's gate, and the strongest end-to-end check here that needs
# no media: a keystroke reaching the DART, the ROM's line editor, BASIC's
# parser and the screen. Deliberately given no --type-at, because the
# readiness gate is part of what is being tested - before it existed the
# first two characters were discarded and this arrived as `INT 6*7`.
out=$("$ABC806" --cycles 160000000 --type $'PRINT 6*7\r' --screen 2>&1)
tl_begin "keyboard-basic-answers"
tl_want "$out" "PRINT 6*7" "the ROM echoing the typed line"
# Assert on the answer, never only on the echo: an echo proves the
# keyboard works and says nothing about BASIC. That exact mistake was
# found in these suites once already.
tl_want "$out" " 42" "BASIC evaluating it"
tl_end "$out"

# The Swedish letters, through the keyboard and back out to the screen.
# This is the regression test for --type feeding its argument as raw UTF-8
# bytes, which is docs/postmortems/2026-08-28-type-raw-utf8-bytes.md's bug: it
# was fixed on the ABC802 and then reintroduced here by starting this
# target's main.c from a blank page. Round-tripping proves both directions
# of the one charset table.
out=$("$ABC806" --cycles 200000000 --type $'PRINT "ÅÄÖ"\r' --screen 2>&1)
tl_begin "keyboard-swedish-roundtrip"
tl_want "$out" 'PRINT "ÅÄÖ"' "the typed accented letters echoing as themselves"
tl_want "$out" "ÅÄÖ" "BASIC printing them back"
tl_end "$out"

# --- The character and attribute decode -------------------------------
#
# The only check on chargen.c *and* text.c. Covers colours, underline, flash, blank,
# keep-previous and double-width, none of which the boot screen touches.
tl_begin "chargen-attributes"
if diff -q "$FIXTURES/chargen.txt" <("$CHARGEN_DUMP" 2>&1) >/dev/null; then
    tl_end
else
    tl_note "chargen dump differs from the fixture; run abc806/tests/regen-fixtures.sh and read the diff"
    tl_end "$(diff "$FIXTURES/chargen.txt" <("$CHARGEN_DUMP" 2>&1) | head -20)"
fi

# --- The real-time clock, on real media ---------------------------------
#
# The only end-to-end check of the E0516, and it needs a disk: nothing in
# the ROM alone reads the clock. A 640K UFD-DOS system image prints
# `Datum och tid:` during boot, straight out of the chip, so the assertion
# is simply that the machine agrees with the host about what day it is.
#
# Point ABC806_TEST_DISKS at a directory holding sys832-ufd.img (an ABC832
# UFD-DOS system disk - see abc802/resources/disks/README.md for where to
# get one). Skips loudly without it; a skip is never counted as a pass.
RTC_IMAGE="${ABC806_TEST_DISKS:-}/sys832-ufd.img"
# Every check below needs the media, so each skips in its own right -
# reporting one skip for a block of three would undercount what is not
# being run.
DISK_CHECKS="disk-boot-and-rtc dos-shell dos-runs-lib"
if [ -z "${ABC806_TEST_DISKS:-}" ]; then
    for c in $DISK_CHECKS; do
        tl_skip "$c" "set ABC806_TEST_DISKS to a directory holding sys832-ufd.img (a 640K ABC832 UFD-DOS system disk)"
    done
elif [ ! -f "$RTC_IMAGE" ]; then
    for c in $DISK_CHECKS; do tl_skip "$c" "$RTC_IMAGE not found"; done
else
    # Copy first: the DOS writes to media, and a test must never mutate
    # the user's archive.
    WORK="$(mktemp -d)"
    trap 'rm -rf "$WORK"' EXIT
    cp "$RTC_IMAGE" "$WORK/sys.img"
    out=$("$ABC806" --cycles 200000000 --disk "$WORK/sys.img" --screen 2>&1)
    tl_begin "disk-boot-and-rtc"
    # --screen now collapses double-width cells, so these read as plain
    # text. That is a stronger assertion than the doubled form it replaced,
    # not a laxer one: collapsing correctly requires decoding the attribute
    # plane, so a broken attribute walk shows up here as doubled or missing
    # characters.
    tl_want "$out" "UFD-DOS" "the DOS booting off real ABC832 media"
    tl_want "$out" "$(date '+%Y-%m-%d')" \
            "the clock agreeing with the host about the date"
    tl_end "$out"

    # Milestone 4's gate: BYE leaves BASIC for the DOS command shell, and
    # the shell runs a real program off the disk. LIB is the DOS's own
    # directory utility, so a full listing means the bus, the controller,
    # the filesystem and the loader all work end to end - not just that a
    # boot sector ran.
    # Two runs, not one: LIB's listing is long enough to scroll the shell's
    # own banner off the top, so each assertion is made against a screen
    # that still holds it.
    cp "$RTC_IMAGE" "$WORK/bye.img"
    out=$("$ABC806" --cycles 900000000 --disk "$WORK/bye.img" --type $'BYE\r' --screen 2>&1)
    tl_begin "dos-shell"
    tl_want "$out" "Disc operating system" "BYE reaching the DOS command shell"
    tl_end "$out"

    cp "$RTC_IMAGE" "$WORK/lib.img"
    out=$("$ABC806" --cycles 2000000000 --disk "$WORK/lib.img" --type $'BYE\rLIB\r' --screen 2>&1)
    tl_begin "dos-runs-lib"
    # LIB is a program on the disk, not a shell built-in, so a listing
    # means the bus, the controller, the filesystem and the loader all
    # worked end to end - not merely that a boot sector ran.
    tl_want "$out" "DOSGEN" "LIB loading off the disk and listing its files"
    tl_end "$out"
fi

tl_summary "abc806"
