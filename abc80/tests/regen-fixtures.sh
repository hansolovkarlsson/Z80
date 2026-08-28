#!/usr/bin/env bash
# Regenerates abc80/tests/fixtures/ from the current build.
#
# Only run this after a *deliberate* change to what the tools output, and
# read the diff before committing it - a fixture regenerated to match a
# regression silently converts a failing test into a passing one, which is
# worse than having no test at all.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT/abc80"
{
    echo "; Fixture: bin/abc80-chargen-dump resources/rom/chargen.bin"
    echo "; Regenerate with abc80/tests/regen-fixtures.sh after a deliberate change."
    "$ROOT/bin/abc80-chargen-dump" resources/rom/chargen.bin
} > tests/fixtures/chargen.txt
echo "wrote abc80/tests/fixtures/chargen.txt"
