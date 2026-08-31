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

# --- High-resolution graphics ------------------------------------------
#
# The machine drawing into its own high-resolution plane, from BASIC. This
# is the only check on the fetch-window rule in memory.c: accesses below
# 0x7800 reach the plane when the executing instruction was fetched from
# 0x7800-0x7FFF, which is where both the ROM's own plane-clearing memset
# and FGLINE's plotter live.
#
# 91 is not a round number chosen to match: a line from (10,10) to
# (100,100) is 90 Bresenham steps plus its start point, one byte written
# per step at a 128-byte pitch, and every one lands in a distinct byte
# because the line is diagonal. Asserting the exact count is what makes
# this a geometry check rather than a "something happened" check.
out=$("$ABC806" --cycles 300000000 --type $'FGPOINT 10,10,7:FGLINE 100,100,7\r' 2>&1)
tl_begin "graphics-fgline-draws"
tl_want "$out" "High-resolution plane: 91/131072 bytes nonzero" \
        "FGLINE plotting exactly 91 pixels into the plane"
tl_end "$out"

# The plane must also come up *clear*. The ROM zeroes all 30,720 bytes at
# boot with LD (HL),0 followed by LDIR, and that propagate only works if
# its reads reach the plane too - with reads still answering from ROM it
# smears ROM bytes across the whole plane instead, which is exactly the
# symptom that led here.
out=$("$ABC806" --cycles 60000000 2>&1)
tl_begin "graphics-plane-clears"
tl_want "$out" "High-resolution plane: 0/131072 bytes nonzero" \
        "the ROM's own memset leaving the plane genuinely zeroed"
tl_end "$out"

# The pen encoding: a FG command's pen argument is masked to two bits and
# selects the plane nibble 0xC | (pen & 3), which FGCTL's palette then
# colours. Four pens, and pen 4 wraps back onto pen 0's nibble.
#
# Asserting on the byte *values* rather than a pixel count is the point.
# A count cannot tell one pen from another, and not being able to tell is
# what made a correct renderer look broken - three lines that were really
# red, green and yellow were read as "all white" off a small screenshot.
# Both values are asserted, not just the first: a horizontal line writes
# whole bytes along its body and a half byte at the ends, so "C0 CC" is the
# full signature. Matching only the leading value let a write path that
# masked off the low nibble slip through.
for pen_case in "0:C0 CC" "1:D0 DD" "2:E0 EE" "3:F0 FF" "4:C0 CC" "7:F0 FF"; do
    pen="${pen_case%%:*}"; want="${pen_case#*:}"
    out=$("$ABC806" --cycles 500000000 \
          --type "FGCTL 2"$'\r'"FGPOINT 20,20,$pen:FGLINE 200,20,$pen"$'\r' 2>&1)
    tl_begin "graphics-pen-$pen"
    tl_want "$out" "values: $want" "pen $pen writing plane nibble ${want%% *}"
    tl_end "$out"
done

# --- The graphics commands, as ABC806_BASIC_REFERENCE.md documents them --
#
# That document makes specific behavioural claims about FGPOINT, FGFILL and
# FGPAINT, established by running them. These are those claims, executable.
# Without them the reference is a set of assertions nothing re-checks.
#
# The exact byte counts are characterisation rather than derived constants,
# which is fine and is why each one names what it is counting. They are
# stable across runs; the emulator is deterministic here.
while IFS='|' read -r name cmd want desc; do
    [ -n "$name" ] || continue
    out=$("$ABC806" --cycles 250000000 \
          --type "FGCTL 2"$'\r'"$cmd"$'\r' 2>&1)
    tl_begin "$name"
    tl_want "$out" "High-resolution plane: $want/131072 bytes nonzero" "$desc"
    tl_end "$out"
done <<'GFX'
graphics-fgpoint-cursor-only|FGPOINT 100,100|0|two-argument FGPOINT moving the cursor and plotting nothing
graphics-fgpoint-plots-a-dot|FGPOINT 100,100,3|1|three-argument FGPOINT plotting exactly one dot
graphics-fgfill-rectangle|FGPOINT 50,50:FGFILL 150,150,2|5151|FGFILL filling the rectangle from the cursor
graphics-fgpaint-unbounded|FGPAINT 100,100,2|30720|FGPAINT flooding the whole plane when nothing bounds it
GFX

# The flood fill is bounded by drawn pixels, which is the claim that makes
# FGPAINT a flood fill rather than a screen clear. Drawing a box first must
# cut the filled area down from the whole plane to the box.
out=$("$ABC806" --cycles 900000000 --type "FGCTL 2"$'\r'\
"FGPOINT 40,40,3:FGLINE 160,40,3:FGLINE 160,140,3:FGLINE 40,140,3:FGLINE 40,40,3"$'\r'\
"FGPAINT 100,90,1"$'\r' 2>&1)
tl_begin "graphics-fgpaint-is-bounded"
tl_want "$out" "High-resolution plane: 6161/131072 bytes nonzero" \
        "FGPAINT filling only inside a drawn box, not the whole plane"
tl_end "$out"

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
