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
ABCDISK="$ROOT/bin/abcdisk"
FIXTURES="$ROOT/abc802/tests/fixtures"

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

for binary in "$ABC802" "$CHARGEN_DUMP" "$ABCDISK"; do
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

# --- Cassette on SIO channel B ----------------------------------------
#
# SAVE "CAS:name" records the byte stream the ROM transmits on SIO channel
# B. The modulation everybody means by "cassette interface" is hardware
# past the SIO, so the byte stream is the protocol boundary - see
# emu/src/cassette.h.
#
# The structure is asserted rather than just the length, because a length
# alone would pass on 590 bytes of anything: a 32-byte zero leader, the
# 16-bit sync pattern 16 02 the ROM programs into WR6/WR7, and then the
# header record naming the file.
CASSETTE_TAPE="$WORKDIR/tape.bin"
rm -f "$CASSETTE_TAPE"
out=$("$ABC802" --columns 80 --cassette "$CASSETTE_TAPE" --cycles 250000000 \
      --type $'10 PRINT "HEJ"\rSAVE "CAS:T"\r' 2>&1)
tl_begin "cassette-save-records-stream"
tl_want "$out" "Cassette: 590 bytes written" "the ROM transmitting a whole recording"
structure=$(python3 - "$CASSETTE_TAPE" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()
lead = all(b == 0 for b in d[:32])
sync = d[32:34] == b'\x16\x02'
# FF FF FF, then the 8-character name and 3-character type
name = d[34:37] == b'\xff\xff\xff' and d[37:48] == b'T          '[:8] + b'BAC'
print("leader=%d sync=%d header=%d" % (lead, sync, name))
PY
)
tl_want "$structure" "leader=1" "a 32-byte zero leader for the receiver to lock onto"
tl_want "$structure" "sync=1" "the 16-bit sync pattern the ROM programs into WR6/WR7"
tl_want "$structure" "header=1" "the header record naming T.BAC"
tl_end "$out$structure"

# The receive side: hunt-phase sync detection and a real SIO receive
# interrupt, which was the one slot in the IM 2 daisy chain that had
# nothing in it.
#
# This asserts an *incomplete* state on purpose, and should be replaced by
# a real SAVE/LOAD round trip when it stops being true. The ROM reads the
# recording back byte-exactly - 586 of the 590 bytes, the rest being the
# leader its hunt phase correctly skips - and then rejects it with Error
# 35, "CRC or address-mark error". That is honest: the ROM drives the
# SIO's *hardware* CRC generator (WR0 CRC commands) and this emulator does
# not implement it, so nothing ever wrote the CRC bytes the loader checks.
# What this check defends is everything up to that point: without the
# interrupt or the 16-bit hunt the ROM reads nothing at all and hangs.
out=$("$ABC802" --columns 80 --cassette "$CASSETTE_TAPE" --cycles 400000000 \
      --type $'LOAD "CAS:T"\r' --screen 2>&1)
tl_begin "cassette-load-reads-the-recording"
tl_want "$out" "586 read" "the ROM consuming the whole recording through the SIO"
tl_want "$out" "Error 35" "the ROM reaching its CRC check - see cassette.h for what is missing"
tl_end "$out"

# --- Row attributes in the terminal render ----------------------------
#
# The Row Graphic attribute switches the rest of a row to a teletext 2x3
# mosaic font. --screen used to know nothing about it and printed one
# alphanumeric glyph per character code, so an attribute-heavy screen read
# correctly as a PNG and misleadingly in a terminal. It now runs the same
# attribute walk the pixel renderer does and draws the mosaics as Unicode
# sextants.
#
# POKEd rather than printed, because BASIC's own PRINT never puts these
# codes into character RAM - CHR$(17) is consumed on the way. 80-column
# mode, because in 40-column mode the ROM lays text out in the even cells
# and consecutive POKEs would not correspond to what the video hardware
# draws.
#
# The codes: 17 turns Row Graphic on, 1 turns it off, and 33/35/127 are
# mosaics of one cell, two cells and all six. 65 after the off-switch must
# come back as a plain "A", which is what says the attribute is really
# scoped rather than latched for the rest of the screen.
out=$("$ABC802" --columns 80 --screen --cycles 500000000 \
      --type $'10 POKE 30720,17\r20 POKE 30721,33\r30 POKE 30722,35\r40 POKE 30723,127\r50 POKE 30724,1\r60 POKE 30725,65\rRUN\r' 2>&1)
tl_begin "chargen-row-graphic-in-terminal"
tl_want "$out" " 🬀🬂█ A" "the mosaic row rendered as sextants, matching the pixel render cell for cell"
# The negative half: those codes as alphanumerics are what the old
# renderer showed, and are exactly what a regression would print.
tl_want_not "$out" "!#" "the alphanumeric glyphs the codes would name outside Row Graphic"
tl_end "$out"

# The terminal walk reads scanline 0 of the character ROM to decide
# whether a code is an attribute command; the pixel renderer re-reads
# whichever scanline it is drawing. They agree only because this font
# encodes the same command on every row - which is a property of the ROM,
# not of the code, so it is checked rather than assumed. Over the ten
# scanned rows an attribute byte is identical; on the two substituted rows
# (blank 0x0E, cursor 0x0F) only bits the decode ignores may differ.
tl_begin "chargen-attribute-invariant"
invariant=$(python3 - "$ROOT/abc802/resources/rom/ABC802-char.6490191-01.bin" <<'PY'
import sys
rom = open(sys.argv[1], 'rb').read()
scanned_vary, command_vary = 0, 0
for code in range(0x80):
    for alt in (0, 0x800):
        a = ((code & 0x7F) << 4) | alt
        lines = [rom[a + l] for l in range(10)]
        if not any(b & 0x80 for b in lines):
            continue
        if len(set(lines)) != 1:
            scanned_vary += 1
        # ATE, ATD and the two attribute-select bits, on the substituted rows
        want = lines[0] & 0xC3
        for ra in (0x0E, 0x0F):
            if (rom[a + ra] & 0xC3) != want:
                command_vary += 1
print("scanned_vary=%d command_vary=%d" % (scanned_vary, command_vary))
PY
)
tl_want "$invariant" "scanned_vary=0" "attribute bytes identical across the ten scanned rows"
tl_want "$invariant" "command_vary=0" "the same attribute command on the blank and cursor rows"
tl_end "$invariant"

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

    # bin/abcdisk reading media it did not write. Without this, its
    # directory location is only ever checked against its own constant -
    # a writer and reader sharing one wrong value agree perfectly, which
    # is exactly what a deliberate sabotage of dir_first demonstrated.
    # Real media pins it independently.
    #
    # No --interleave equivalent is needed here: the directory sits on a
    # track boundary, and those sectors map to themselves under any
    # factor, so a raw read finds it under either dump convention.
    out=$("$ABCDISK" list "$MO_IMAGE" 2>&1)
    tl_begin "abcdisk-list-real-media"
    tl_want "$out" "mo, 640 sectors" "the format identified from the image size"
    tl_want "$out" "BASICINI SYS" "a real file listed out of media abcdisk did not write"
    tl_end "$out"
fi

# --- DOSGEN, the DOS's own disk generator -------------------------------
#
# DOSGEN is the program that makes a filesystem, and it is a file on a
# system disk rather than anything in ROM - which is why bin/abcdisk
# exists at all. Running it end to end exercises more of the bus model
# than any other single thing here: a program loaded off the media, an
# interactive dialogue, a full 2560-sector verify pass, and writes back.
#
# It needs sys832-ufd.img, which the other checks above do not, so it
# gates separately.
#
# Note the cycle budget. 700,000,000 is measured, not copied: the run
# completes by 450,000,000 and this leaves a comfortable margin. An
# earlier hand-run of this used 30,000,000,000 and took 41 seconds
# against the 2 it actually needs.
UFD_IMAGE="$MEDIA/sys832-ufd.img"
dosgen_skip_reason=""
if [ -z "$MEDIA" ]; then
    dosgen_skip_reason="set ABC802_TEST_DISKS to a directory holding sys832-ufd.img (a 640K ABC832 UFD-DOS system disk)"
elif [ ! -f "$UFD_IMAGE" ]; then
    dosgen_skip_reason="$UFD_IMAGE not found"
fi

if [ -n "$dosgen_skip_reason" ]; then
    for name in dosgen-completes dosgen-marks-beyond-media \
                dosgen-filesystem-is-usable; do
        tl_skip "$name" "$dosgen_skip_reason"
    done
else
    # The dialogue: BYE to leave BASIC for the DOS shell, DOSGEN, the
    # drive, "-" for filesystem-only (its F option is a low-level format,
    # which a synthetic controller has nothing to do), then three separate
    # confirmations - the program really does ask three times.
    #
    # The bare carriage returns are padding, not input. --type sends one
    # string at a fixed pace and there is no way to wait inside it, so
    # these fill the time while the DOS shell and then DOSGEN load. Keys
    # arriving while nothing is reading are discarded, and the ones that
    # do land at a prompt are answered harmlessly ("Felaktigt enhetsnamn")
    # and reprompted.
    dosgen_pad() { printf '\r%.0s' $(seq "$1"); }
    DOSGEN_KEYS="BYE"$'\r'"$(dosgen_pad 40)DOSGEN"$'\r'"$(dosgen_pad 5)MF0:"$'\r''-'$'\r''J'$'\r''J'$'\r''J'$'\r'

    cp "$UFD_IMAGE" "$WORKDIR/dosgen.img"
    out=$("$ABC802" --columns 80 --screen --cycles 700000000 \
          --type-at 200000000 --type "$DOSGEN_KEYS" \
          --disk "$WORKDIR/dosgen.img" 2>&1)
    tl_begin "dosgen-completes"
    # Only the summary line, deliberately. DOSGEN's own banner has long
    # scrolled off by the time it finishes - the screen is a 24-line
    # window and the run prints 1272 bad-sector lines - so asserting on it
    # fails for a reason that has nothing to do with the subject. That was
    # checked by doing it.
    #
    # The count is the whole point anyway: 632 usable clusters of 4
    # sectors on a 640-cluster drive. DOSGEN reaching a correct total is
    # what says the load, the verify pass and the writes all worked.
    tl_want "$out" "2528 användbara sektorer" "the correct usable-sector count for a 640K drive"
    tl_want_not "$out" "Fel " "a DOS error during the run"
    tl_end "$out"

    # The "Sektor NNNN är dålig" lines DOSGEN prints past the end of the
    # media are not an error, and this is what says so. DOSGEN initialises
    # a fixed 240-byte free-list bitmap covering 1920 clusters; this drive
    # has 640, so it marks clusters 640-1911 unusable and announces each
    # one. Reading the bitmap it wrote is the direct evidence - the screen
    # only shows the last few lines of the scroll.
    tl_begin "dosgen-marks-beyond-media"
    bitmap=$(python3 - "$WORKDIR/dosgen.img" <<'PY'
import sys
d = open(sys.argv[1], 'rb').read()[14*256:15*256]
bits = [(d[i >> 3] >> (7 - (i & 7))) & 1 for i in range(240 * 8)]
# The media is 640 clusters. Everything past it must be marked unusable,
# and the usable region must not be.
print("usable_clear=%d beyond_set=%d" % (
    all(b == 0 for b in bits[16:640]),
    all(b == 1 for b in bits[640:1912])))
PY
)
    tl_want "$bitmap" "usable_clear=1" "the on-media clusters left free in the bitmap"
    tl_want "$bitmap" "beyond_set=1" "clusters past the end of the media marked unusable"
    tl_end "$bitmap"

    # And the filesystem it built actually works, which no amount of
    # reading its summary establishes. The DOSGEN'd disk goes on drive 1
    # beside a pristine system disk, and BASIC saves to it.
    #
    # Across *two processes*, deliberately. Doing it in one - save, NEW,
    # load, LIST - looks like a round trip and is not: the program text is
    # on screen from the moment it was typed, so the assertion matches the
    # echo whether or not anything was ever written. That check passed
    # with the card's writes injected away, which is how this was found;
    # it is the same trap as the postmortem in
    # docs/postmortems/2026-08-29-test-matched-the-echoed-input.md. The
    # second process never types the program text, so any occurrence of it
    # came off the disk.
    cp "$UFD_IMAGE" "$WORKDIR/dgsys.img"
    "$ABC802" --columns 80 --cycles "$DISK_CAP" --type-at 200000000 \
        --type $'10 PRINT "DOSGEN OK"\rSAVE MF1:NYFIL\r' \
        --disk "$WORKDIR/dgsys.img" --disk "$WORKDIR/dosgen.img" > /dev/null 2>&1
    out=$("$ABC802" --columns 80 --screen --cycles "$DISK_CAP" --type-at 200000000 \
        --type $'LOAD MF1:NYFIL\rLIST\r' \
        --disk "$WORKDIR/dgsys.img" --disk "$WORKDIR/dosgen.img" 2>&1)
    tl_begin "dosgen-filesystem-is-usable"
    tl_want "$out" '10 PRINT "DOSGEN OK"' "a program read back off the generated filesystem in a second process"
    tl_end "$out"
fi

# --- Formatted blank media, with no external images needed ------------
#
# These are the only floppy checks that never SKIP: bin/abcdisk builds the
# media, so the write path is covered on a machine with no third-party
# dumps at all. That matters beyond convenience - before this tool the
# whole SAVE path was untestable without media the repo cannot ship.
#
# Note the --interleave 0: abcdisk writes images in logical sector order,
# which is the convention the MO default does *not* assume.
#
# The free-cluster counts are asserted because a formatter can produce a
# perfectly working disk of the wrong size, and nothing else here would
# notice: the round trip below saves one small program. `--type mf` really
# did ship at half capacity once, from a `usable_clusters` derived from a
# single atypical image.
#
# Both numbers come from the machine, not from arithmetic. A 640K disk's
# own DOS reports "1960 av 2528 sektorer lediga", i.e. 2528/4 = 632
# clusters usable after the 8-cluster system area; and 616 is the 160K
# figure the ABC80 suite already pins from real media ("453 av 616
# sektorer kvar").
for spec in "mo:160K:--interleave 0:MO0:616" "mf:640K::MF0:632"; do
    IFS=: read -r type label flag dev freeclusters <<<"$spec"
    image="$WORKDIR/blank-$type.dsk"

    out=$("$ABCDISK" create "$image" --type "$type" 2>&1)
    tl_begin "abcdisk-create-$type"
    tl_want "$out" "Created" "the image being written"
    tl_want "$out" "$freeclusters free clusters" "the drive's full usable capacity"
    listing=$("$ABCDISK" list "$image" 2>&1)
    tl_want "$listing" "(empty)" "a fresh disk listing no files"
    tl_want "$listing" "$freeclusters free" "the same capacity read back"
    tl_end "$out"

    # shellcheck disable=SC2086
    "$ABC802" --columns 80 --cycles "$DISK_CAP" $flag --disk "$image" \
        --type-at "$DISK_TYPE_AT" \
        --type "10 PRINT \"$label BLANK OK\""$'\n'"SAVE \"$dev:BLANKT\""$'\n' \
        > /dev/null 2>&1
    # shellcheck disable=SC2086
    out=$("$ABC802" --columns 80 --screen --cycles "$DISK_CAP" $flag \
          --disk "$image" --type-at "$DISK_TYPE_AT" \
          --type "LOAD \"$dev:BLANKT\""$'\n'"RUN"$'\n' 2>&1)
    tl_begin "disk-blank-roundtrip-$type"
    tl_want "$out" "$label BLANK OK" "the program running back off a disk this repo formatted"
    tl_want "$("$ABCDISK" list "$image" 2>&1)" "BLANKT" "abcdisk reading back the entry the DOS wrote"
    tl_end "$out"
done

tl_summary "abc802"
