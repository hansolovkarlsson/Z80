#!/usr/bin/env python3
"""Parse and analyse a PAL16L8 JEDEC fuse map.

Written for the ABC806's `ABC-P4-1.bin`, the PAL16L8 that decides that
machine's memory map. `abc806/emu/src/memory.c` currently follows MAME's
*behavioural* approximation of it, which is also where MAME's own
`abc806 30K banking` TODO sits, so evaluating the real array would settle
something the reference implementation leaves open.

**This tool does not evaluate the array yet, and the reason is recorded
rather than papered over: the column-to-signal mapping is not established.**
See "What is not known" below.

## What is established

* The file parses cleanly: 2048 fuses, 64 product rows of 32 columns,
  exactly a 16L8's array.
* Fuse polarity: `1` is blown (disconnected), `0` intact (connected). A row
  with every fuse blown is a term that is always true; a row with every fuse
  intact connects each input *and* its complement and so is always false,
  which is the standard "unused term" encoding. Both appear here, and the
  unused rows land exactly where unused rows should.
* Rows group in eights, one group per output, and **the first group drives
  pin 19**, not pin 12. Two independent checks agree:
    - pin 15 (KDL) comes out permanently tri-stated with zero live terms,
      which is exactly right for a pin MAME's own pin list marks as an
      *input*;
    - the four signals MAME actually reads - ROMD, HRE, MUX, RAMD, the ones
      its comment marks with `>` - are precisely the always-enabled outputs
      with many live terms.
  Under the opposite ordering both facts land on the wrong pins.

## What is not known

The 32 columns are 16 input lines by two polarities, but **which column pair
carries which pin is not established here.** A PAL16L8 interleaves dedicated
inputs with output feedback in an order given by the device datasheet's fuse
map, and this repository has no primary source for it.

Two candidate layouts were tried and both fail the simplest sanity check -
that with EME off and KEYDTR high the array must select ROM below 0x8000 and
RAM above it. Guessing a third would be the same mistake twice more, so the
tool stops at dumping the terms.

Settling it needs a PAL16L8 fuse-map column table from a datasheet. With
that, the array can be evaluated and compared against both MAME's
approximation and this emulator's own fetch-window rule - which was derived
from watching the machine and would then have an independent check.

Usage:  scripts/palanalyse.py abc806/resources/rom/ABC-P4-1.bin
"""

import re
import sys

# Row group -> output pin. See "What is established" above.
OUT_PIN = [19, 18, 17, 16, 15, 14, 13, 12]

# MAME's own pin list for this part, from src/mame/luxor/abc80x.cpp.
# '>' there marks an active-low output.
PIN_NAME = {
    1: 'I3', 2: 'A15', 3: 'A14', 4: 'B13', 5: 'B12', 6: 'B11', 7: 'M1L',
    8: 'EME', 9: 'ENL', 10: 'GND', 11: 'XML', 12: '>ROMD', 13: 'HRAL',
    14: 'HRBL', 15: 'KDL', 16: '>HRE', 17: 'RKDL', 18: '>MUX', 19: '>RAMD',
    20: 'Vcc',
}


def parse_jedec(path, rows=64, cols=32):
    text = open(path, 'rb').read().decode('latin-1')
    fuses = {}
    for entry in re.finditer(r'L0*(\d+)\s+([01\s]+?)\*', text):
        addr = int(entry.group(1))
        bits = re.sub(r'\s', '', entry.group(2))
        for offset, bit in enumerate(bits):
            fuses[addr + offset] = int(bit)
    return [[fuses.get(r * cols + c, 1) for c in range(cols)] for r in range(rows)]


def describe(row):
    """A product term as its connected columns, or its constant value."""
    if all(b == 1 for b in row):
        return 'always true (no fuses intact)'
    if all(b == 0 for b in row):
        return None                       # unused: input AND its complement
    return 'cols=%s' % [c for c in range(len(row)) if row[c] == 0]


def main(argv):
    if len(argv) != 2:
        print(__doc__, file=sys.stderr)
        return 1
    array = parse_jedec(argv[1])

    print('%s: %d rows x %d columns\n' % (argv[1], len(array), len(array[0])))
    for group, pin in enumerate(OUT_PIN):
        rows = array[group * 8:group * 8 + 8]
        enable = describe(rows[0])
        live = [describe(r) for r in rows[1:]]
        live = [t for t in live if t is not None]
        print('pin %2d %-6s  enable: %s' % (pin, PIN_NAME[pin],
                                            enable if enable else 'never (always false)'))
        if not live:
            print('    no live product terms - this pin is an input')
        for term in live:
            print('    term %s' % term)
        print()
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
