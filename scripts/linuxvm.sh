#!/bin/sh
# Build and test this tree on Linux, in a Parallels VM on the same Mac,
# without SSH or a shared folder: `prlctl exec` runs commands in the guest
# and passes stdin through, which is enough to copy the tree in and drive
# `make` from here.
#
#   scripts/linuxvm.sh sync          copy every tracked file (as it is in the
#                                    working tree, uncommitted edits included)
#                                    into the guest
#   scripts/linuxvm.sh disks         also copy the uncommitted disk images the
#                                    floppy checks use (see abc-test-media in
#                                    the ABC roadmaps' Testing sections)
#   scripts/linuxvm.sh run 'CMDS'    run shell commands in the guest's copy,
#                                    e.g. scripts/linuxvm.sh run 'make test'
#
# The copy is separate from the Mac's on purpose: object files sit beside
# their sources here, so building both platforms in one shared directory
# would mix them. Settings, with the defaults this was first used with:
#
#   LINUX_VM    the VM's name as `prlctl list` shows it
#   LINUX_USER  the guest account that owns the copy and runs the build
#   LINUX_DIR   where the copy lives in the guest
#
# The guest needs build-essential and pkg-config; libgtk-4-dev and
# libsdl2-dev add the ABC GTK apps, and libvte-2.91-gtk4-dev adds
# bin/z80-gtk (make test builds each app only when its packages exist).
# xvfb adds bin/z80-gtk's check, and VS Code (Microsoft's arm64 .deb)
# the extension's; each skips loudly without them.

set -e
VM="${LINUX_VM:-Ubuntu 24.04.3 ARM64}"
GUSER="${LINUX_USER:-parallels}"
GDIR="${LINUX_DIR:-/home/$GUSER/Z80}"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"

# bsdtar's macOS metadata would only produce warnings in the guest.
# Extracted with -m, so every file sent is stamped with the time it
# arrived rather than when it was edited on the Mac: with the Mac's time
# kept, a source edited before the guest's last build looked older than
# its object file, make skipped it, and an old binary ran (2026-09-26).
send() {
    COPYFILE_DISABLE=1 tar --no-xattrs -czf - "$@" |
        prlctl exec "$VM" runuser -u "$GUSER" -- tar -xzmf - -C "$GDIR" 2>&1 |
        grep -v "Ignoring unknown extended header" || true
}

case "$1" in
    sync)
        # Through bash on stdin: prlctl takes some dash options for itself
        # (a bare `mkdir -p` loses its -p).
        printf 'mkdir -p %s\n' "$GDIR" |
            prlctl exec "$VM" runuser -u "$GUSER" -- /bin/bash -s
        cd "$ROOT"
        git ls-files -z | send --null -T -
        ;;
    disks)
        cd "$ROOT"
        # Whatever images are present locally; the suites skip loudly
        # for any that are missing.
        send $(ls abc80/resources/disks/*.img abc802/resources/disks/*.img 2>/dev/null)
        ;;
    run)
        [ -n "$2" ] || { echo "usage: $0 run 'commands'" >&2; exit 2; }
        # A command line would be split into words by prlctl, and some of
        # its dash options taken, so the commands go to bash on stdin.
        printf 'cd %s || exit 1\n%s\n' "$GDIR" "$2" |
            prlctl exec "$VM" runuser -u "$GUSER" -- /bin/bash -s
        ;;
    *)
        sed -n '2,27p' "$0" | sed 's/^# \{0,1\}//'
        exit 2
        ;;
esac
