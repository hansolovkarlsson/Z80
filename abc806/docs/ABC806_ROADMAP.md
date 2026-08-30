# ABC806 emulator roadmap

Status and known gaps for `bin/abc806`, the Luxor ABC806 machine target.
[`ABC806_SCOPING.md`](ABC806_SCOPING.md) is the feasibility review written
before any of this existed, and is still the plan being followed; this file
is what actually works.

There is no `ABC806_REFERENCE.md` yet. The hardware facts established so
far live in `emu/src/memory.c`'s header and in `resources/rom/README.md`;
a consolidated reference is worth writing once the machine does something
visible, which it does not yet.

## Milestone 1: the memory map, and a boot — done

`make abc806` builds `bin/abc806`, which loads the real 32K firmware and
runs it.

```
$ bin/abc806
ABC806: loaded 32K ROM from 'abc806/resources/rom' (DOS PROM 'ABC806-dos.66-31.bin')
Ran 2132527 instructions / 20000003 T-states; PC=7625 (reached T-state cap)
CRTC programmed: yes (R1=80 cols, R6=25 rows)
Character RAM: 2000/2048 nonzero, 0 non-space; attribute plane: 0 nonzero
```

The scoping document's gate for this milestone was "the machine executes
past reset and programs the CRTC". It does, and three further signs say
the boot is real rather than a survival:

- **The CRTC is programmed for 80×25**, which is this machine's own
  geometry — not a value carried over from the ABC802's 80×24.
- **Character RAM holds exactly 2000 bytes of `0x20`.** 80 × 25 = 2000:
  the ROM cleared the screen it had just configured.
- **Execution settles into a six-address loop at `0x7621`-`0x762A`**
  polling port `0x23`, DART channel B — the keyboard. The machine
  finished initialising and is waiting for a key, the same end state the
  ABC802's own first milestone reached.

Both committed DOS PROMs (v.19 and v.20) boot identically.

### What milestone 1 actually needed

- **The memory decode**, following MAME's behavioural form of the PAL16L8
  rather than the fuse map (see `emu/src/memory.c`). ROM low, RAM high,
  2K character RAM at `0x7800` decoded by M1, and the EME/KEYDTR diversions
  to the high-resolution plane.
- **The 74ALS259 addressable latch** at port `0x36`, which is where EME
  and the 40-column line come from. One `OUT` sets one bit, named by the
  written value's own low three bits.
- **The attribute plane**, written as a side effect of a character-RAM
  write through a latch on port `0x35`. Not addressable directly.
- **The page map** at port `0x34`, indexed by the *high* address byte —
  which for `OUT (C),r` is register B, not the port number.

### Found the hard way

- **The page map entry is stored inverted.** MAME reads it as
  `m_map[page] ^ 0xff` before testing ENL in bit 7, so an entry of zero —
  what the map holds at reset — means "no diversion". Implemented with the
  polarity the other way round, enabling EME diverts *every* access to
  video RAM, and the machine dies thousands of instructions later on an
  illegal `ED C3` at `0x05D1`, nowhere near the cause. Bisected by
  disabling one port handler at a time.
- **`0x34`-`0x36` have no low-byte mirror.** MAME gives them
  `select(0xff00)`/`mirror(0xff00)`: the *high* byte varies, carrying a
  register index, and the low byte is exact. Decoding them as `port & 0x3F`
  also claims `0x74`, `0xB4` and `0xF4`, which are CTC mirrors. Only `0x37`
  mirrors, with `0x18`.
- **DTR-B means something different here.** On the ABC802 it is LRS,
  selecting ROM or RAM in the low 32K. On the ABC806 it is KEYDTR, which
  swaps the low 32K between ROM and the high-resolution plane. Same chip,
  same pin, different wiring — and it would have half-worked silently if
  carried across unexamined.

## Milestone 2: the text decode, and a renderer — done, with the gate changed

`--screen` dumps the text screen, `--screenshot` writes a real PNG in the
machine's eight colours, and `bin/abc806-chargen-dump` renders a synthetic
screen exercising every attribute path.

**The gate as written was not reachable, and was replaced rather than
quietly dropped.** `ABC806_SCOPING.md` said "renders the ROM's own sign-on
banner". This ROM draws no banner — see the open question below — so that
gate tests something the machine does not do. The replacement is stronger,
not weaker: the decode is verified against a *synthetic* screen that
exercises colours, underline, flash, blank, keep-previous and double width,
none of which a sign-on banner would have touched. That is exactly what
[the boot-screen postmortem](../../docs/postmortems/2026-08-28-boot-screen-cannot-validate.md)
already concluded on the ABC802, where the banner *did* render and still
proved nothing about attributes.

### What the decode actually is

Nothing here transfers from the ABC802, whose attributes live in the
character generator's own output byte. On the ABC806:

- **An attribute byte whose foreground and background match is a command,
  not a colour.** `(attr & 7) == ((attr >> 3) & 7)` selects a command in
  bits 7:6 — keep previous, reserved, blank, double width. Black on black
  is therefore unreachable as an ordinary attribute, which is what makes
  the encoding work.
- **Underline, flash and double height are never drawn.** They index the
  RAD PROM, which answers with a *scanline address*, and the character
  generator is addressed with that instead of the real row. The cursor is
  the same trick: scanline `0x0F`, a solid bar in this font.
- **Double width is described by the cell before it.** A command-3
  attribute takes e5/e6 from its own low bits and the colours from the
  *next* cell's attribute byte.
- **The glyph is six bits from the top of the font byte after a two-place
  left shift.** The low two bits are not pixels.

### Verified

`bin/abc806-chargen-dump` renders legible text — "WHITE ON BLACK" in pen 7
on pen 0, "RED ON BLUE" in pen 1 on pen 4, the blank row genuinely blank —
and its output is committed as `tests/fixtures/chargen.txt`. Deliberately
changing the font shift by one bit turns `chargen-attributes` red, which
is the only evidence worth having that the fixture is doing its job.

`make test-abc806` runs three checks and is part of `make test`.

## Open: the ROM draws nothing

The machine boots, configures 80×25, clears the screen, and polls DART
channel B forever without writing a single visible character. Established
so far, all by tracing:

- **The keyboard reaches it.** A sent byte moves RR0 from `0x24` to `0x25`
  — receive-character-available — so the DART model and the wait loop
  agree about what a keypress looks like.
- **It is a real poll loop**, at `0x7621`-`0x762A` in the *option* PROM:
  `LD A,0x10 / OUT (C),A / IN B,(C)` on port `0x23`, resetting external
  status and reading RR0.
- **It is not waiting for a disk.** Attaching a real ABC832 UFD-DOS system
  image changes nothing.
- **It is not the DOS PROM.** Both v.19 and v.20 behave identically.

So the machine is waiting for something it has not been given, and the
keyboard alone is not it. Milestone 3 is where this gets solved; the
likeliest remaining candidates are the E0516 RTC and the protection
device, both of which hang off the 74ALS259 whose bits are currently
decoded and dropped.

## Known gaps

Everything below is expected at this point: two milestones in, of five.

- **No interactive mode.** `--type` delivers paced keystrokes and `--disk`
  attaches ABC-bus media, both because milestone 2 needed them to rule
  things out, but there is no live session yet. Milestone 3.
- **The high-resolution plane is not rendered.** `--screenshot` draws the
  text layer only.
- **Flash is static.** `flash_on` is passed to the decode but nothing
  drives it; there is no frame clock yet.
- **No high-resolution graphics.** Milestone 5. `HRU-I`, `HRU-II` and
  `V50` are committed and unused; `RAD` is now in use.
- **The PAL fuse map is not evaluated.** `ABC-P4-1.bin` is a well-formed
  JEDEC dump and the memory decode currently follows MAME's behavioural
  approximation instead — inheriting its `abc806 30K banking` gap. See
  `ABC806_SCOPING.md`.
- **`emu/src/ports.c` is a near-copy of the ABC802's.** The CTC, SIO, DART
  and CRTC are the same chips, so it was seeded from that file rather than
  rewritten. That is a deliberate duplication, not an oversight: extracting
  a shared Z80-peripheral module is the right move *once it is known what
  is genuinely common*, which needs this target further along than
  milestone 1. `abcbus/` reached that point the same way — built inside one
  target, moved out when a second consumer proved the shape.
- **The test suite asserts no screen content**, because there is none. It
  checks the machine's configuration and the decode instead.
- **RAM is 32K directly addressable**, as on real hardware, with the rest
  reachable only through EME and the map. The 544K option is not modeled.
- **No RTC, no protection device.** Both hang off the same 74ALS259; the
  latch bits are decoded and dropped.
