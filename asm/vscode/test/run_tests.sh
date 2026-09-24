#!/usr/bin/env bash
# asm/vscode/test/run_tests.sh - runs test/run.js on VS Code's own Node and
# TextMate engine, so no separate Node install is needed and the grammar is
# tested by the code that will run it. Skips loudly, with exit 0, when VS
# Code is not installed; set VSCODE_APP to its Contents directory if it is
# somewhere else.
set -uo pipefail
HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
APP="${VSCODE_APP:-/Applications/Visual Studio Code.app/Contents}"
if [ ! -x "$APP/MacOS/Code" ] || [ ! -d "$APP/Resources/app" ]; then
    echo "SKIP: vscode - VS Code not found at $APP (set VSCODE_APP)"
    exit 0
fi
# Electron prints one harmless warning when run as Node; drop just that.
ELECTRON_RUN_AS_NODE=1 "$APP/MacOS/Code" "$HERE/run.js" "$APP/Resources/app" 2>&1 \
    | grep -v 'node_main.cc'
exit "${PIPESTATUS[0]}"
