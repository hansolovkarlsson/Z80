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
# available" before it'll even start - see BDOS_ENTRY in cpm/emu/src/cpm.h
# for the real emulator fix that was needed for that (not something this
# script works around).
#
# This script also runs TINST.COM's [C]ommand installation, as a second,
# separate TINST invocation layered on top of the Screen-installed
# TURBO.COM, to bind Ctrl-H to real delete-left-character. By default
# TURBO.COM only recognizes the distinct <DEL> (0x7F) byte for that -
# Ctrl-H/Backspace performs the same non-destructive cursor-left as
# Ctrl-S instead (real Turbo Pascal's own documented default, confirmed
# from the manual) - which stops a modern keyboard's Backspace/Delete key
# from deleting anything at all, since this project's own
# console_read_char() (cpm.c) always translates that key's 0x7F into
# 0x08 before TURBO.COM ever sees it (needed for Tasty Basic, which only
# recognizes 0x08). Command installation lets every one of the editor's
# 45 commands be given an additional key binding "on top of" the fixed
# WordStar-compatible keystrokes (see cpm/docs/TURBOPASCAL_REFERENCE.md); no
# partial-edit shortcut exists, so all 45 prompts must be answered - a
# bare <RETURN> leaves a binding unchanged. Item 27, "Delete left
# character", is the one binding actually changed, to Ctrl-H (0x08);
# items 1-26 and 28-45 all get a bare <RETURN> to keep their defaults
# untouched. This has to be a genuinely separate TINST run, not chained
# onto the Screen-installation run below with a plain 'C' at its own
# closing main menu: entering Q at that top-level "[S]creen | [C]ommand |
# [Q]uit" menu saves and exits TINST immediately (confirmed empirically -
# a chained single-session S-then-C attempt silently produced a
# Screen-only TURBO.COM, since by the time 'C' was sent TINST had already
# exited), so there's no way to answer both installation types in one
# TINST session's input stream.

set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")"
Z80="../../../bin/z80"

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
        sleep 0.5
    } | "$OLDPWD/$Z80" cpm_disk/TINST.COM > tinst_screen.log 2>&1 || true

    # Second, separate TINST run: Command installation, against the
    # Screen-installed TURBO.COM the run above just wrote in place.
    {
        sleep 0.5; printf 'C\r'
        for i in $(seq 1 45); do
            sleep 0.1
            if [ "$i" -eq 27 ]; then
                printf '\x08\r'    # bind Ctrl-H to "Delete left character"
            else
                printf '\r'        # keep every other binding unchanged
            fi
        done
        sleep 1;   printf 'Q\r'
        sleep 0.5
    } | "$OLDPWD/$Z80" cpm_disk/TINST.COM > tinst_command.log 2>&1 || true
)

cp "$WORK/cpm_disk/TURBO.COM" "$WORK/cpm_disk/TURBO.MSG" "$WORK/cpm_disk/TURBO.OVR" .

echo "Wrote TURBO.COM (+ TURBO.MSG/TURBO.OVR), reconfigured for ANSI, Ctrl-H bound to real delete"
