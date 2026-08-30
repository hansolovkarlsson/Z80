#!/usr/bin/env bash
# Regenerate the chargen fixture. Run after any deliberate change to the
# character/attribute decode, and *read the diff* before committing it -
# the fixture is the only check on that decode, so an unexamined
# regeneration silently blesses whatever broke.
set -euo pipefail
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
cd "$ROOT"
make abc806-chargen-dump >/dev/null
bin/abc806-chargen-dump > abc806/tests/fixtures/chargen.txt
echo "Regenerated abc806/tests/fixtures/chargen.txt"
