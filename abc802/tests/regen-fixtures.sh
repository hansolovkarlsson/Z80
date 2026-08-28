#!/usr/bin/env bash
# Regenerates abc802/tests/fixtures/ from the current build.
#
# Only run this after a *deliberate* change to what the tools output, and
# read the diff before committing it - a fixture regenerated to match a
# regression silently converts a failing test into a passing one, which is
# worse than having no test at all.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
{
    echo "; Fixture: bin/abc802-chargen-dump"
    echo "; Regenerate with abc802/tests/regen-fixtures.sh after a deliberate change."
    "$ROOT/bin/abc802-chargen-dump"
} > abc802/tests/fixtures/chargen.txt
echo "wrote abc802/tests/fixtures/chargen.txt"
