#!/usr/bin/env python3
"""CCP-specific preprocessing, run before the general 8080->Z80 pass
(scripts/8080_to_z80.py): resolves the conditionals z80asm can't handle (it only
understands IF/ELSE/ENDIF with a real expression, not IFDEF/IFNDEF) and
drops two directives (.cpu, title) it doesn't understand at all.

- The `ifdef origin` / `if testing` block picks this build's load
  address - resolved here to a fixed ORG (see cpm/docs/ROADMAP.md for why
  0E400h) instead of z80asm needing to support IFDEF.
- The three `ifndef noserial(ize)` blocks gate DRI's serialization check
  (comparing bytes at the nominal BDOS location against an embedded
  serial number, self-patching the CCP into a DI/HLT trap on mismatch -
  see badserial:) - the reformatted source's own comment says these
  flags exist specifically to omit that check, which is exactly what we
  need: this build has no resident BDOS bytes for it to compare against
  (BDOS is emulated entirely on the host side, not resident Z80 code),
  so left enabled it would always fail and brick the CCP the first time
  a program is run. Resolved here as if noserial/noserialize were
  defined (dropping all three blocks), the officially-provided escape
  hatch for exactly this situation - not a hack.
"""
import re
import sys

ORIGIN_BLOCK = """\tifdef\torigin
\torg\torigin
bdosl\tequ\t$+800h\t\t;bdos location
\telse
\tif\ttesting
\torg\t3400h
bdosl\tequ\t$+800h\t\t;bdos location
\telse
\torg\t000h
bdosl\tequ\t$+800h\t\t;bdos location
\tendif
\tendif"""

ORIGIN_REPLACEMENT = """\torg\t0e400h\t\t;below BIOS_BASE (0FC00h) - see cpm/docs/ROADMAP.md
bdosl\tequ\t$+800h\t\t;nominal bdos location (no resident bdos code in
\t\t\t\t;this build - bdos calls are intercepted by the
\t\t\t\t;emulator at address 5)"""


def strip_ifndef_blocks(text, tag):
    return re.sub(rf"\tifndef\t{tag}\n(?:.*\n)*?\tendif\n", "", text)


def main():
    src, dst = sys.argv[1], sys.argv[2]
    with open(src, "r", errors="replace") as f:
        text = f.read()

    if ORIGIN_BLOCK not in text:
        sys.exit("preprocess.py: origin block not found verbatim - upstream/ccp.asm changed?")
    text = text.replace(ORIGIN_BLOCK, ORIGIN_REPLACEMENT)

    text = strip_ifndef_blocks(text, "noserial")
    text = strip_ifndef_blocks(text, "noserialize")

    text = re.sub(r"\n\t\.cpu\t8080\n", "\n", text)
    text = re.sub(r'\n\ttitle\t"[^"]*"\n', "\n", text)

    with open(dst, "w") as f:
        f.write(text)
    print(f"Wrote {dst}")


if __name__ == "__main__":
    main()
