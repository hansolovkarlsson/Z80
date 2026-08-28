#!/usr/bin/env bash
# abc802/tests/run_tests.sh - regression checks for bin/abc802.
#
# Same shape and the same reasoning as abc80/tests/run_tests.sh: drive the
# real committed ROMs and assert on what the machine produced. This
# machine leans on the technique even harder, because ABC802 BASIC has
# `INP()` and `OUT` - so the chip models can be interrogated from inside
# the emulated machine, through the real port decode and the ROM's own
# implementation, rather than by a C-level shortcut around both. That is
# how the SIO was verified when it was written.
#
# Disk checks need real media, which this repo deliberately does not
# commit (third-party dumps - see ABC802_ROADMAP.md's sources). Point
# ABC802_TEST_DISKS at a directory holding them and they run; otherwise
# they SKIP loudly. A skip is never counted as a pass.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../scripts/testlib.sh
. "$ROOT/scripts/testlib.sh"

# bin/abc802 resolves its default ROM directory relative to the repo root.
cd "$ROOT"

ABC802="$ROOT/bin/abc802"
CHARGEN_DUMP="$ROOT/bin/abc802-chargen-dump"
FIXTURES="$ROOT/abc802/tests/fixtures"

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

for binary in "$ABC802" "$CHARGEN_DUMP"; do
    if [ ! -x "$binary" ]; then
        echo "abc802/tests/run_tests.sh: $binary is missing (run 'make test')" >&2
        exit 1
    fi
done

# Deterministic batch mode, so these caps only have to exceed what the ROM
# actually needs. Disk work needs the larger one because --type-at holds
# the keystrokes back until a booting program has stopped discarding them.
CAP=40000000
DISK_CAP=400000000
DISK_TYPE_AT=120000000

# run802 <typed text> [extra args...] - types text at the ROM once it is
# listening and prints everything the emulator produced, including the
# --screen render most checks assert on.
run802() {
    local typed="$1"
    shift
    "$ABC802" --columns 80 --screen --cycles "$CAP" --type "$typed" "$@" 2>&1
}

# basic_result <program> - the last number BASIC printed on its own line,
# for the INP() checks. The screen render frames each line with '|', so a
# result line is "| <digits>" and nothing else.
basic_result() {
    run802 "$1" | grep -oE '^\| +[0-9]+ +\|$' | tail -1 | tr -dc '0-9'
}

# --- Boot and BASIC ---------------------------------------------------

# The --screen render frames each row between '|', so the frame line is
# one '+', N dashes and another '+' - the simplest honest check that the
# machine really is in the width it was asked for.
FRAME40="+$(printf '%040d' 0 | tr 0 -)+"
FRAME80="+$(printf '%080d' 0 | tr 0 -)+"

out=$("$ABC802" --screen --cycles "$CAP" --type $'PRINT 6*7\n' 2>&1)
tl_begin "boot-40-columns"
tl_want "$out" "ABC802" "the ROM's own prompt"
tl_want "$out" " 42" "PRINT 6*7 evaluating to 42"
tl_want "$out" "$FRAME40" "a 40-column screen"
# Not a typo, and worth pinning: the CRTC counts 80 character cells per
# row in *both* modes. 40-column mode halves the character clock and
# doubles the glyph width instead of reprogramming R1, so a render that
# reported R1=40 would mean the mode had been modeled the wrong way.
tl_want "$out" "R1=80 cols" "the CRTC still counting 80 cells in 40-column mode"
tl_end "$out"

out=$(run802 $'PRINT 6*7\n')
tl_begin "boot-80-columns"
tl_want "$out" " 42" "PRINT 6*7 evaluating to 42"
tl_want "$out" "$FRAME80" "an 80-column screen"
tl_end "$out"

# UTF-8 in on the keyboard, ABC802 charset bytes through the ROM's
# tokenizer and string evaluation, and back out to UTF-8 for the render.
# --type used to feed its argument as raw UTF-8 bytes, which reached
# BASIC as an error; there is a postmortem for that class.
out=$(run802 $'PRINT "\xc3\x85\xc3\x84\xc3\x96"\n')
tl_begin "charset-swedish-roundtrip"
tl_want "$out" 'PRINT "ÅÄÖ"' "the typed line echoed back"
tl_want "$out" $'|ÅÄÖ' "the string evaluated and printed"
tl_want_not "$out" "Error" "a BASIC error"
tl_end "$out"

# --- Z80 SIO/2 --------------------------------------------------------
#
# Every one of these returned 4 before the register model replaced the
# stub, so they fail loudly if it is ever reverted to a constant.

tl_begin "sio-idle-status"
# RR0 = Tx buffer empty (0x04) + CTS asserted by configuration switch S2
# (0x20). Channel B's modem-status inputs are wired to two DIP switches,
# which is why a constant here would be asserting a machine configuration.
tl_want_eq "$(basic_result $'PRINT INP(67)\n')" "36" "channel B RR0 at idle (0x24)"
tl_end

tl_begin "sio-register-pointer"
tl_want_eq "$(basic_result $'OUT 67,1\nPRINT INP(67)\n')" "1" "RR1 after pointing WR0 at it"
tl_end

tl_begin "sio-interrupt-vector-roundtrip"
tl_want_eq "$(basic_result $'OUT 67,2\nOUT 67,88\nOUT 67,2\nPRINT INP(67)\n')" "88" "the vector written to WR2 read back from RR2"
tl_end

tl_begin "sio-rr2-is-channel-b-only"
# Deliberately wrong-looking and deliberately correct: the datasheet makes
# RR2 channel B only, so channel A must give an obviously wrong answer
# rather than a subtly right one.
tl_want_eq "$(basic_result $'OUT 65,2\nPRINT INP(65)\n')" "0" "channel A RR2 reading 0"
tl_end

tl_begin "sio-channel-reset"
tl_want_eq "$(basic_result $'OUT 67,1\nOUT 67,24\nPRINT INP(67)\n')" "36" "the register pointer back at RR0 after a channel reset"
tl_end

# --- Video ------------------------------------------------------------

# A real PNG of the real screen, through the same renderer bin/abc802-gtk
# draws with. Checks the file is a genuine PNG of the machine's real
# 480x240 pixel screen rather than merely non-empty.
"$ABC802" --columns 80 --cycles "$CAP" --type $'PRINT 6*7\n' \
    --screenshot "$WORKDIR/screen.png" > /dev/null 2>&1
tl_begin "screenshot-png"
if [ ! -s "$WORKDIR/screen.png" ]; then
    tl_note "no PNG was written"
else
    header=$(python3 - "$WORKDIR/screen.png" <<'PY'
import struct, sys
d = open(sys.argv[1], 'rb').read()
if d[:8] != b'\x89PNG\r\n\x1a\n':
    print("not-a-png"); sys.exit()
if d[12:16] != b'IHDR':
    print("no-ihdr"); sys.exit()
w, h = struct.unpack('>II', d[16:24])
print(f"{w}x{h}")
PY
)
    tl_want_eq "$header" "480x240" "the PNG's IHDR dimensions"
fi
tl_end

# The three row attributes are the part of the decode the ROM's own boot
# screen never exercises, so only this synthetic dump covers them.
tl_fixture "chargen-attributes" "$FIXTURES/chargen.txt" \
    "$("$CHARGEN_DUMP" 2>&1)"

# --- Floppy, on real media --------------------------------------------

MEDIA="${ABC802_TEST_DISKS:-}"
MO_IMAGE="$MEDIA/disk001.img"     # 160K ABC830, autoboots ORD 800
MF_IMAGE="$MEDIA/mf001.img"       # 640K ABC832, autoboots ADMINISTRATION 800
# The two-drive checks need media that reaches a BASIC prompt: the other
# two images autoboot an application, which swallows the typed commands.
MF_BASIC_IMAGE="$MEDIA/mf002.img"

disk_skip_reason=""
if [ -z "$MEDIA" ]; then
    disk_skip_reason="set ABC802_TEST_DISKS to a directory holding disk001.img (abc80.net's ABC800 160K archive, boots ORD 800), mf001.img (its 640K archive, boots ADMINISTRATION 800) and mf002.img (640K, reaches a BASIC prompt)"
else
    for image in "$MO_IMAGE" "$MF_IMAGE" "$MF_BASIC_IMAGE"; do
        [ -f "$image" ] || disk_skip_reason="$image not found"
    done
fi

if [ -n "$disk_skip_reason" ]; then
    for name in disk-mo-160k disk-mf-640k disk-drive-independence \
                disk-cross-drive-load; do
        tl_skip "$name" "$disk_skip_reason"
    done
else
    cp "$MO_IMAGE" "$WORKDIR/mo.img"
    out=$("$ABC802" --columns 80 --screen --cycles "$DISK_CAP" \
          --disk "$WORKDIR/mo.img" 2>&1)
    tl_begin "disk-mo-160k"
    tl_want "$out" "mo floppy controller" "the controller type chosen from the image size"
    tl_want "$out" "ORD 800" "the word processor booting off the disk"
    tl_end "$out"

    cp "$MF_IMAGE" "$WORKDIR/mf.img"
    out=$("$ABC802" --columns 80 --screen --cycles "$DISK_CAP" \
          --disk "$WORKDIR/mf.img" 2>&1)
    tl_begin "disk-mf-640k"
    tl_want "$out" "mf floppy controller" "the controller type chosen from the image size"
    tl_want "$out" "ADMINISTRATION 800" "the business application booting off the disk"
    tl_end "$out"

    # Two drives, one controller. Saving to MF1: must change drive 1 and
    # leave drive 0 byte-for-byte alone - checked against each image's own
    # before-value rather than a hardcoded digest, so this holds for any
    # 640K media.
    cp "$MF_BASIC_IMAGE" "$WORKDIR/d0.img"
    cp "$MF_BASIC_IMAGE" "$WORKDIR/d1.img"
    before0=$(cksum < "$WORKDIR/d0.img")
    before1=$(cksum < "$WORKDIR/d1.img")
    "$ABC802" --columns 80 --cycles "$DISK_CAP" \
        --disk "$WORKDIR/d0.img" --disk "$WORKDIR/d1.img" \
        --type-at "$DISK_TYPE_AT" \
        --type $'NEW\n10 PRINT "DRIVE ONE"\nSAVE "MF1:D1TEST"\n' > /dev/null 2>&1
    tl_begin "disk-drive-independence"
    tl_want_eq "$(cksum < "$WORKDIR/d0.img")" "$before0" "drive 0 unchanged by a save to drive 1"
    if [ "$(cksum < "$WORKDIR/d1.img")" = "$before1" ]; then
        tl_note "drive 1 was not written at all"
    fi
    tl_end

    # Load it back from the drive it went to, then fail to load it from
    # the drive it did not. The negative control is what rules out "both
    # drives are secretly the same file".
    out=$("$ABC802" --columns 80 --screen --cycles 500000000 \
          --disk "$WORKDIR/d0.img" --disk "$WORKDIR/d1.img" \
          --type-at "$DISK_TYPE_AT" \
          --type $'LOAD "MF1:D1TEST"\nLIST\nLOAD "MF0:D1TEST"\n' 2>&1)
    tl_begin "disk-cross-drive-load"
    tl_want "$out" 'PRINT "DRIVE ONE"' "the program listed back from drive 1"
    tl_want "$out" "Error 21" "the same name failing to load from drive 0"
    tl_end "$out"
fi

tl_summary "abc802"
