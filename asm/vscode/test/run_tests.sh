#!/usr/bin/env bash
# asm/vscode/test/run_tests.sh - runs test/run.js on VS Code's own Node and
# TextMate engine, so no separate Node install is needed and the grammar is
# tested by the code that will run it. Skips loudly, with exit 0, when VS
# Code is not installed. It looks where the macOS app, the Linux .deb/.rpm
# and the Linux snap put it; set VSCODE_APP to the directory holding the
# executable's folder and resources (the app's Contents on macOS, the
# folder holding `code` on Linux) if it is somewhere else.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
if [ -n "${VSCODE_APP:-}" ]; then
    CANDIDATES=("$VSCODE_APP")
else
    CANDIDATES=("/Applications/Visual Studio Code.app/Contents"
                "$HOME/Applications/Visual Studio Code.app/Contents"
                "/usr/share/code"
                "/snap/code/current/usr/share/code")
fi
# The two platforms lay the install out alike under different names:
# MacOS/Code and Resources/app on macOS, code and resources/app on Linux.
EXE="" RES=""
for dir in "${CANDIDATES[@]}"; do
    if [ -x "$dir/MacOS/Code" ] && [ -d "$dir/Resources/app" ]; then
        EXE="$dir/MacOS/Code" RES="$dir/Resources/app"; break
    elif [ -x "$dir/code" ] && [ -d "$dir/resources/app" ]; then
        EXE="$dir/code" RES="$dir/resources/app"; break
    fi
done
if [ -z "$EXE" ]; then
    echo "SKIP: vscode - VS Code not found in: ${CANDIDATES[*]} (set VSCODE_APP)"
    exit 0
fi
# Electron prints one harmless warning when run as Node; drop just that.
ELECTRON_RUN_AS_NODE=1 "$EXE" "$HERE/run.js" "$RES" 2>&1 \
    | grep -v 'node_main.cc'
exit "${PIPESTATUS[0]}"
