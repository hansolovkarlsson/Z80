#!/usr/bin/env bash
# Reproduces TURBO.COM (+ its unmodified companions TURBO.MSG/TURBO.OVR)
# from the unmodified upstream/ files. Unlike the other resources/*/
# derive.sh scripts, there's no source to translate here (TURBO.COM is a
# prebuilt binary, no source available) - what needs reproducing instead
# is a one-time *binary patch*: upstream/TURBO.COM ships pre-configured
# for a "Microbee VDU" terminal (see upstream/READ.ME), which doesn't
# speak anything a modern terminal understands. Real Turbo Pascal ships
# with TINST.COM specifically to rewrite TURBO.COM's own terminal-control
# byte tables in place for a different terminal - option 6 in its own
# built-in list is a genuine "ANSI" profile, so running TINST.COM through
# this project's own emulator with that selection is the reproducible
# "build" step, the same role clang/sed play in the other derive.sh
# scripts.
#
# TINST.COM also needs a plausible answer to "how much memory is
# available" before it'll even start - see BDOS_ENTRY in emu/src/cpm.h
# for the real emulator fix that was needed for that (not something this
# script works around).

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
Z80="../../bin/z80"

WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

# bin/z80's own file I/O maps every drive/user onto a cpm_disk/ directory
# relative to wherever it's invoked from (see CLAUDE.md's File I/O
# section) - TINST.COM needs to find TURBO.COM there, not in $WORK
# itself.
mkdir -p "$WORK/cpm_disk"
cp upstream/TURBO.COM upstream/TURBO.MSG upstream/TURBO.OVR \
   upstream/TINST.COM upstream/TINST.DTA upstream/TINST.MSG "$WORK/cpm_disk/"

(
    cd "$WORK"
    # Each character needs a real pause behind it - TINST.COM (like every
    # other real CP/M program tested in this project) polls for input
    # rather than blocking indefinitely, so piped input arriving faster
    # than a human could type gets read ahead of the prompt that's
    # actually asking for it.
    {
        sleep 0.5; printf 'S\r'
        sleep 1;   printf '6\r'
        sleep 1;   printf 'N\r'
        sleep 1;   printf '\r'   # accept the default CPU frequency
        sleep 1;   printf 'Q\r'
        sleep 0.5; printf 'Q\r'
        sleep 0.5
    } | "$OLDPWD/$Z80" cpm_disk/TINST.COM > tinst.log 2>&1 || true
)

cp "$WORK/cpm_disk/TURBO.COM" "$WORK/cpm_disk/TURBO.MSG" "$WORK/cpm_disk/TURBO.OVR" .

echo "Wrote TURBO.COM (+ TURBO.MSG/TURBO.OVR), reconfigured for ANSI"
