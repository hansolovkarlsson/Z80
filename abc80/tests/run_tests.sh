#!/usr/bin/env bash
# abc80/tests/run_tests.sh - regression checks for bin/abc80.
#
# Everything here drives the real, committed ROMs and asserts on what the
# machine actually produced - the screen it rendered, the file it wrote,
# the port it answered. There are no unit tests of internal functions,
# because almost every bug this target has had was a wrong belief about
# the hardware rather than a wrong line of C, and a unit test written
# from the same wrong belief passes.
#
# Where a check can be expressed as BASIC, it is: `OUT`/`INP`/`PEEK` make
# the emulated machine its own test harness, which exercises the real port
# decode and the ROM's own implementation rather than a C-level shortcut
# around both. That trick came from the ABC802's SIO work.
#
# Disk checks need real media, which this repo deliberately does not
# commit (third-party dumps - see ABC80_ROADMAP.md's sources). Point
# ABC80_TEST_DISKS at a directory holding them and they run; otherwise
# they SKIP loudly. A skip is never counted as a pass.

set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
# shellcheck source=../../scripts/testlib.sh
. "$ROOT/scripts/testlib.sh"

# bin/abc80 resolves its default ROM directory relative to the working
# directory, matching how it is meant to be run by hand.
cd "$ROOT/abc80"

ABC80="$ROOT/bin/abc80"
TIMING_DUMP="$ROOT/bin/abc80-video-timing-dump"
CHARGEN_DUMP="$ROOT/bin/abc80-chargen-dump"
SOUND_DEMO="$ROOT/bin/abc80-sound-demo"
ROMS="resources/rom"
FIXTURES="$ROOT/abc80/tests/fixtures"

WORKDIR="$(mktemp -d)"
trap 'rm -rf "$WORKDIR"' EXIT

for binary in "$ABC80" "$TIMING_DUMP" "$CHARGEN_DUMP" "$SOUND_DEMO"; do
    if [ ! -x "$binary" ]; then
        echo "abc80/tests/run_tests.sh: $binary is missing (run 'make test')" >&2
        exit 1
    fi
done

# run_basic <instruction cap> <typed text> [extra bin/abc80 args...]
# Types text into the machine one keystroke at a time (the keyboard reads
# stdin), runs to the cap, and echoes everything the emulator printed -
# including the final video-RAM render, which is what most checks assert
# on. \r is the ABC80's own Return.
run_basic() {
    local cap="$1" typed="$2"
    shift 2
    printf '%s' "$typed" | "$ABC80" "$ROMS" "$cap" "$@" 2>&1
}

# basic_numbers <output> - just the values BASIC printed, one per line, in
# order. Necessary rather than fastidious: the render includes the typed
# lines as the ROM echoed them, so a bare substring search for " 42"
# happily matches the "OUT 6,42" that *asked* for it. A mutation test
# caught exactly that, passing a check whose subject was entirely broken.
basic_numbers() {
    printf '%s' "$1" | sed -n 's/^ \([0-9][0-9]*\) *$/\1/p'
}

# --- Boot and BASIC ---------------------------------------------------

# Instruction caps are deliberately tight. Batch mode is fully
# deterministic - no wall-clock pacing - so a cap only has to exceed what
# the ROM actually needs, and every one here is roughly double the
# measured minimum. Being generous costs real seconds per check: this
# target runs at about 1.7M instructions/sec.
CAP=4000000
DISK_CAP=6000000

out=$(run_basic $CAP $'PRINT 6*7\r')
tl_begin "boot-to-basic"
tl_want "$out" "ABC80" "the ROM's own prompt"
tl_want_not "$out" "Unimplemented opcode"
tl_end "$out"

tl_begin "basic-arithmetic"
tl_want "$out" " 42" "PRINT 6*7 evaluating to 42"
tl_end "$out"

# Every run prints the detected RAM floor, so the base machine's memory
# map needs no run of its own.
tl_begin "memory-map-base-16k"
tl_want "$out" "0xC000 (base 16K RAM)" "the base machine's RAM floor"
tl_end "$out"

# The Swedish/Finnish national character set, both directions in one line:
# UTF-8 in on the keyboard, ABC80 charset bytes through the ROM's
# tokenizer and string evaluation, charset bytes back out to UTF-8 for the
# render. A postmortem exists for the class of bug this catches.
out=$(run_basic $CAP $'PRINT "\xc3\x85\xc3\x84\xc3\x96"\r')
tl_begin "charset-swedish-roundtrip"
tl_want "$out" 'PRINT "ÅÄÖ"' "the typed line echoed back"
tl_want "$out" $'\nÅÄÖ' "the string evaluated and printed"
tl_end "$out"

# --- Memory map -------------------------------------------------------

out=$(run_basic $CAP '' --ram32k)
tl_begin "memory-map-ram32k"
tl_want "$out" "0x8000 (32K RAM)" "the expansion mod's RAM floor"
tl_end "$out"

# --- Bus and I/O port decode ------------------------------------------

# One run, three related facts about the ABC bus. 0x4000 is expansion
# memory space with no card fitted, so it floats high rather than reading
# as ordinary RAM. Port 6 is the SN76477 and must fall through to the CPU
# core's flat io_ports[] array; port 0 is the bus and must reach the card,
# which with no image attached reports nothing there. Widening the bus
# port decode by one would silently kill sound; narrowing it would
# silently kill disks.
out=$(run_basic $CAP $'PRINT PEEK(16384)\rOUT 6,42\rPRINT INP(6)\rPRINT INP(0)\r')
results=$(basic_numbers "$out")
tl_begin "floating-bus-memory"
tl_want_eq "$(echo "$results" | sed -n 1p)" "255" "PEEK of an unpopulated bus address"
tl_end "$out"

tl_begin "io-port-decode"
tl_want_eq "$(echo "$results" | sed -n 2p)" "42" "INP(6), the sound port, round-tripping through io_ports[]"
tl_want_eq "$(echo "$results" | sed -n 3p)" "255" "INP(0), the ABC bus, floating with no card fitted"
tl_end "$out"

# --- Cassette ---------------------------------------------------------

# Across two processes, so this is a real file round trip rather than an
# in-memory one.
run_basic $CAP $'10 PRINT "QL"\r' --quicksave "$WORKDIR/ql.bac" > /dev/null
out=$(run_basic $CAP $'LIST\r' --quickload "$WORKDIR/ql.bac")
tl_begin "cassette-quicksave-quickload"
if [ ! -s "$WORKDIR/ql.bac" ]; then
    tl_note "--quicksave wrote no file"
fi
tl_want "$out" '10 PRINT "QL"' "the saved program listed back after reload"
tl_end "$out"

# --- Standalone decode tools ------------------------------------------

# Already speaks PASS/FAIL and sets an exit code of its own; it just was
# never run automatically.
out=$("$TIMING_DUMP" "$ROMS" 2>&1)
rc=$?
tl_begin "video-timing-proms"
[ "$rc" -eq 0 ] || tl_note "exited $rc"
tl_want "$out" "All checks passed."
tl_end "$out"

tl_fixture "chargen-decode" "$FIXTURES/chargen.txt" \
    "$("$CHARGEN_DUMP" "$ROMS/chargen.bin" 2>&1)"

# The SN76477 model, driven directly rather than through the CPU: the
# demo renders a real WAV from the same code the emulator and the GTK
# app use. Checks the tone is actually there and lands on the documented
# 640 Hz, by counting zero crossings - the same measurement the live
# audio was verified with by hand.
out=$("$SOUND_DEMO" "$WORKDIR/tone.wav" 2>&1)
tl_begin "sn76477-tone"
tl_want "$out" "640.00 Hz" "the computed VCO frequency"
if [ -s "$WORKDIR/tone.wav" ]; then
    # The demo's sequence is 0.5s silence, 1.0s of tone, 0.5s silence
    # (see sound_demo.c), so measure inside the burst - averaging over
    # the whole file halves the apparent frequency.
    measured=$(python3 - "$WORKDIR/tone.wav" <<'PY'
import array, sys
RATE = 44100
d = open(sys.argv[1], 'rb').read()
body = d[44:]
body = body[:len(body) - len(body) % 2]
a = array.array('h')
a.frombytes(body)
if len(a) < int(1.4 * RATE):
    print("short"); sys.exit()
if max(abs(x) for x in a[:int(0.4 * RATE)]):
    print("noisy-lead-in"); sys.exit()
seg = a[int(0.6 * RATE):int(1.4 * RATE)]
if not max(abs(x) for x in seg):
    print("silent"); sys.exit()
rises = sum(1 for i in range(1, len(seg)) if seg[i-1] <= 0 < seg[i])
print(int(rises / 0.8))
PY
)
    case "$measured" in
        short)         tl_note "the rendered WAV is shorter than the demo's own 2s sequence" ;;
        noisy-lead-in) tl_note "the disabled-register lead-in is not silent" ;;
        silent)        tl_note "the tone burst is silent" ;;
        # ~1.6% under the computed 640 Hz is the expected quantisation
        # error from rendering the VCO period at 44.1kHz.
        *) if [ "$measured" -lt 600 ] || [ "$measured" -gt 680 ]; then
               tl_note "measured tone is ${measured} Hz, expected ~640 Hz"
           fi ;;
    esac
else
    tl_note "the demo wrote no WAV"
fi
tl_end "$out"

# The same sound model reached the way real software reaches it: BASIC's
# compiled OUT is the ED-prefixed register-indirect form, which step.c has
# to recognize by decoding the instruction ahead of executing it. Nothing
# else here exercises that path, and the demo above bypasses the CPU
# entirely. 0x40 is the register value the demo itself uses - enabled,
# mixer=VCO, envelope=Mixer Only.
#
# Note for anyone extending this: most register values are legitimately
# silent. 0xFF disables the chip and inhibits the mixer; 0x00 selects
# envelope mode VCO, which produces nothing without a trigger. Silence
# here means the test picked a bad value far more often than it means the
# emulator broke.
run_basic $CAP $'OUT 6,64\r' --wav "$WORKDIR/basic-tone.wav" > /dev/null
tl_begin "sound-register-from-basic"
if [ ! -s "$WORKDIR/basic-tone.wav" ]; then
    tl_note "no WAV was rendered"
else
    peak=$(python3 - "$WORKDIR/basic-tone.wav" <<'PY'
import array, sys
d = open(sys.argv[1], 'rb').read()
body = d[44:]
body = body[:len(body) - len(body) % 2]
a = array.array('h')
a.frombytes(body)
print(max(abs(x) for x in a) if a else 0)
PY
)
    if [ "$peak" -eq 0 ]; then
        tl_note "the sound register written by BASIC produced silence; step.c's OUT detection is the likely suspect"
    fi
fi
tl_end

# --- Floppy, on real media --------------------------------------------

DISK003="${ABC80_TEST_DISKS:-}/disk003.img"

disk_skip_reason=""
if [ -z "${ABC80_TEST_DISKS:-}" ]; then
    disk_skip_reason="set ABC80_TEST_DISKS to a directory holding disk003.img (the abc80.net 160K archive's 'System.diskett ABC80 Ver. 2.1')"
elif [ ! -f "$DISK003" ]; then
    disk_skip_reason="$DISK003 not found"
fi

# Each check gets its own copy: the DOS writes to the media, and a test
# must never mutate the user's archive.
fresh_disk() {
    cp "$DISK003" "$WORKDIR/$1.img" && echo "$WORKDIR/$1.img"
}

if [ -n "$disk_skip_reason" ]; then
    for name in disk-boot disk-abcbus-status disk-lib-directory \
                disk-save-load disk-alternate-dos-rom; do
        tl_skip "$name" "$disk_skip_reason"
    done
else
    # One run covering the attach, a clean DOS boot, and the whole
    # ABC-bus path driven from BASIC: select the ABC830 (0x2D = 45) on the
    # CS port, then read the status port. 137 is 0x89 - ready, idle, and
    # "this command has not failed". Bit 3 of that byte was modeled
    # backwards until the ABC80's ROM exposed it (see the postmortem);
    # this is what pins it, and a regression reads 129.
    out=$(ABCBUS_TRACE=1 run_basic $DISK_CAP $'OUT 1,45\rPRINT INP(1)\r' \
          --disk "$(fresh_disk boot)")
    tl_begin "disk-boot"
    tl_want "$out" "mo floppy controller" "the controller type reported at attach"
    tl_want_not "$out" "ERR " "a DOS error on the boot screen"
    # The card's own trace, so this asserts the bus really carried
    # commands rather than that the screen merely looked untroubled. The
    # DOS reads the directory and the free-space bitmap at boot.
    tl_want "$out" "[abcbus] cmd 03 00 02 00" "the boot-time directory read"
    tl_end "$out"

    tl_begin "disk-abcbus-status"
    tl_want_eq "$(basic_numbers "$out" | tail -1)" "137" "INP(1) on a selected, idle controller (0x89)"
    tl_end "$out"

    # The real LIB utility, loaded off the disk and run: a multi-sector
    # program load, then a directory read and a free-space computation.
    # This failed under the PC-address trap that preceded the real card.
    out=$(run_basic $DISK_CAP $'RUN LIB\r\r' --disk "$(fresh_disk lib)")
    tl_begin "disk-lib-directory"
    tl_want "$out" "BASICERR.SYS" "a real directory entry"
    tl_want "$out" "MARKDISK.BAC" "the last directory entry"
    # Specific to this exact dump, and worth asserting: it is computed
    # from the free-space bitmap, which nothing else here reads.
    tl_want "$out" "453 av 616 sektorer kvar" "the free-sector count"
    tl_end "$out"

    out=$(run_basic $DISK_CAP \
        $'10 PRINT "HEJ"\r20 PRINT 6*7\rSAVE RTTEST\rNEW\rLOAD RTTEST\rLIST\r' \
        --disk "$(fresh_disk rt)")
    tl_begin "disk-save-load"
    tl_want "$out" '10 PRINT "HEJ"' "the first line read back off the disk"
    tl_want "$out" "20 PRINT 6*7" "the second line read back off the disk"
    tl_want_not "$out" "ERR " "a DOS error during the round trip"
    tl_end "$out"

    # A different real DOS ROM over the same card. It has its own bus
    # driver and its own device-select scheme, and nothing in this
    # emulator was written for it; reading ABC-DOS media and correctly
    # reporting no UFD-DOS startup file on it is the point.
    out=$(ABCBUS_TRACE=1 run_basic $DISK_CAP '' --dos-rom UFD80V20.bin \
          --disk "$(fresh_disk ufd)")
    tl_begin "disk-alternate-dos-rom"
    tl_want "$out" "HITTAR EJ FILEN" "UFD-DOS finding no startup file on ABC-DOS media"
    # On its own the message above is a weak assertion - a completely dead
    # card produces the same "file not found", which a mutation test
    # demonstrated. What makes it mean something is that UFD-DOS got there
    # by really reading the disk, so count the bus commands it issued.
    commands=$(printf '%s' "$out" | grep -c "\[abcbus\] cmd")
    if [ "$commands" -lt 20 ]; then
        tl_note "UFD-DOS issued only $commands bus commands; expected a real directory walk (20+)"
    fi
    tl_end "$out"
fi

# --- bin/abc80-gtk, headlessly -----------------------------------------
#
# The GTK window is opt-in (`make abc80-gtk`) and is not built by
# `make test`, so these skip loudly when it is absent rather than failing.
#
# They exist because that app had no automated coverage at all, and it is
# the one target here carrying its *own* pixel decode - the CLI renders
# Unicode block glyphs instead, so unlike the ABC802's and ABC806's
# windows there is no shared, fixture-verified decode underneath it.
#
# `--screenshot` opens no window and claims no audio device, which is what
# makes this runnable at all: automating a capture against the user's real
# desktop steals focus and switches Spaces (see abc80/gtk/README.md).
#
# The assertion is a count of non-background pixels rather than an image
# comparison. A committed reference PNG would be hostage to the host's
# Cairo version; a pixel count still fails loudly if the decode breaks,
# and the *relative* check below is the stronger half.
GTK_BIN="$ROOT/bin/abc80-gtk"
if [ ! -x "$GTK_BIN" ]; then
    tl_skip "gtk-headless-boot" "bin/abc80-gtk not built (run 'make abc80-gtk')"
    tl_skip "gtk-headless-type" "bin/abc80-gtk not built (run 'make abc80-gtk')"
else
    GTK_TMP="$(mktemp -d)"
    trap 'rm -rf "$GTK_TMP"' EXIT

    out=$("$GTK_BIN" resources/rom --screenshot "$GTK_TMP/boot.png" 2>&1)
    boot_lit=$(python3 "$ROOT/abc80/tests/litpix.py" "$GTK_TMP/boot.png" 2>/dev/null || echo 0)
    tl_begin "gtk-headless-boot"
    tl_want "$out" "Wrote" "the headless render completing"
    lit_ok=$([ "$boot_lit" -gt 300 ] && echo yes || echo no)
    tl_want_eq "$lit_ok" "yes" \
        "the sign-on lighting more than 300 pixels (got $boot_lit)"
    tl_end "$out"

    # Typing must add pixels. This is the half that cannot pass by
    # accident: a render that drew nothing, or a keyboard path that
    # delivered nothing, leaves the count at the boot value.
    out=$("$GTK_BIN" resources/rom --screenshot "$GTK_TMP/typed.png" --type 'PRINT 6*7' 2>&1)
    typed_lit=$(python3 "$ROOT/abc80/tests/litpix.py" "$GTK_TMP/typed.png" 2>/dev/null || echo 0)
    tl_begin "gtk-headless-type"
    more_ok=$([ "$typed_lit" -gt "$boot_lit" ] && echo yes || echo no)
    tl_want_eq "$more_ok" "yes" \
        "typed text adding pixels (got $typed_lit against the boot screen's $boot_lit)"
    tl_end "$out"
fi

tl_summary "abc80"
