#!/usr/bin/env python3
"""Parse and analyse a PAL16L8 JEDEC fuse map.

Written for the ABC806's `ABC-P4-1.bin`, the PAL16L8 that decides that
machine's memory map. `abc806/emu/src/memory.c` currently follows MAME's
*behavioural* approximation of it, which is also where MAME's own
`abc806 30K banking` TODO sits, so evaluating the real array would settle
something the reference implementation leaves open.

The column-to-signal mapping **is** now established, from the PAL16L8
logic diagram in TI's SRPS016 datasheet, so terms are printed as readable
equations rather than column numbers. What is still missing is smaller and
different - see "What is not known".

## What is established

* The file parses cleanly: 2048 fuses, 64 product rows of 32 columns,
  exactly a 16L8's array.
* Fuse polarity: `1` is blown (disconnected), `0` intact (connected). A row
  with every fuse blown is a term that is always true; a row with every fuse
  intact connects each input *and* its complement and so is always false,
  which is the standard "unused term" encoding. Both appear here, and the
  unused rows land exactly where unused rows should.
* Rows group in eights, one group per output, and **the first group drives
  pin 19**, not pin 12 - stated outright by the datasheet's logic diagram,
  whose "first fuse numbers" run 0..224 against pin 19 and 1792..2016
  against pin 12. Two checks against the fuse data agree:
    - pin 15 (KDL) comes out permanently tri-stated with zero live terms,
      which is exactly right for a pin MAME's own pin list marks as an
      *input*;
    - the four signals MAME actually reads - ROMD, HRE, MUX, RAMD, the ones
      its comment marks with `>` - are precisely the always-enabled outputs
      with many live terms.
  Under the opposite ordering both facts land on the wrong pins.

* The column layout, from the same diagram: a PAL16L8 interleaves the eight
  dedicated inputs on pins 2-9 with the six output feedback lines, and
  puts pins 1 and 11 at either end. Even columns carry the true form of a
  signal, odd columns its complement.

  The layout is also **self-validating**, which matters more than the
  source: decoded with it, the terms come out as recognisable memory
  decode. Row 57 is `A15'.A14'.EME'` - the bottom 16K with EME off. Rows
  59 and 60 are `A14.B13.B12.B11`, which is exactly the `0x7800`-`0x7FFF`
  window. A wrong column mapping does not produce meaningful equations by
  accident, and two earlier guesses produced none.

* **The fetch-window rule is in the array, as a latch.** `HRAL` (pin 13)
  and `HRBL` (pin 14) each appear complemented in the other's terms: they
  are a cross-coupled SR latch built out of two of the PAL's outputs. It is
  set by

      A15' . I3' . A14 . B13 . B12 . B11 . M1L' . (ENL + EME') . XML

  which is address `0x7800`-`0x7FFF` **during an opcode fetch** - `M1L`
  low. `HRAL` then appears directly in `ROMD`'s and `HRE`'s own terms
  (`RKDL . KDL . EME' . HRAL`), so the latched state gates the memory
  decode.

  That is exactly the rule `abc806/emu/src/memory.c` implements, and it was
  derived there from watching the machine rather than from this file. The
  array confirms it independently, and explains the part that behavioural
  evidence could not: **why it is a latch** rather than a combinatorial
  test, and therefore why the diversion persists through an instruction's
  data cycles after the fetch that set it. MAME leaves this unimplemented -
  its `read_pal_p4()` carries the idea only as a commented-out TODO.

## What is not known

**The real levels of three inputs.** `I3` (pin 1), `XML` (pin 11) and
`RKDL` (pin 17) are supplied by board logic this tool knows nothing about,
and the outputs depend on them. With `RKDL` high the array selects ROM at
every address; with it low the behaviour becomes address-dependent but
still does not match the plain ROM-low/RAM-high split the machine
demonstrably has. `RKDL` is genuinely an input here whenever `KDL` is high,
since pin 17's own enable term is `KDL'`.

So the array is readable but not yet *evaluable* against real operation.
Establishing those three levels - from the ABC806 schematic, or by
instrumenting what makes the emulated machine behave - is what remains.

Usage:  scripts/palanalyse.py abc806/resources/rom/ABC-P4-1.bin
"""

import re
import sys

# Row group -> output pin. See "What is established" above.
OUT_PIN = [19, 18, 17, 16, 15, 14, 13, 12]

# Column pair -> pin, from the PAL16L8 logic diagram: dedicated inputs on
# pins 2-9 interleaved with the six feedback lines, pins 1 and 11 at the
# ends. Even column = true form, odd = complement.
PAIR_PIN = [2, 1, 3, 18, 4, 17, 5, 16, 6, 15, 7, 14, 8, 13, 9, 11]

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
    """A product term as a readable equation, or its constant value."""
    if all(b == 1 for b in row):
        return 'always true (no fuses intact)'
    if all(b == 0 for b in row):
        return None                       # unused: input AND its complement
    parts = []
    for c in range(len(row)):
        if row[c] == 0:
            name = PIN_NAME[PAIR_PIN[c // 2]].lstrip('>')
            parts.append(name if c % 2 == 0 else name + "'")
    return ' . '.join(parts)


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
