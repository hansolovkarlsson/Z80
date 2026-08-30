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

## The ROM draws nothing — solved

It does now. The sign-on renders:

```
$ bin/abc806 --cycles 80000000 --screenshot abc806.png
```

gives **`ABC806`** in white on black with the cursor beneath it, which is
milestone 2's gate *as originally written* — reached after the fact.

### What it was waiting for

The routine at `0x7617` in the option PROM is a delay loop, and
disassembling it was the whole answer:

```
7619: LD C,23h          ; DART channel B, control
761B: LD A,10h
761D: OUT (C),A         ; WR0 = reset external status
761F: IN B,(C)          ; B = RR0, the baseline
7621: LD A,10h
7623: OUT (C),A
7625: IN A,(C)
7627: XOR B             ; what changed?
7628: AND 10h           ; bit 4 only
762A: JR Z,7621         ; spin until it does
```

It waits for **RR0 bit 4 to change state** — the DART's RI input. Not for
a keypress, which is why sending one changed nothing even though the
keystroke demonstrably arrived. With nothing driving RI the bit never
changes and the machine waits there forever, having booted perfectly.

`ports.c` now drives channel B's RI from a 50 Hz square wave. Stated as
inference in the source, because it is one: MAME drives channel A's RI and
channel B's CTS and leaves this alone, so it is not read out of another
implementation. What is *observed* is that the loop waits on a change to
this bit and that a periodic source satisfies it; the frequency is the
part that matters, and if the real machine sources it from a timer rather
than the frame the behaviour is indistinguishable.

### And a rendering bug the picture caught

With the ROM finally drawing, the text dump said `AABBCC880066` and the
PNG said `A B C 8 0 6` — thin letters with gaps. Two views of one screen
disagreeing is a bug by definition.

The banner is written as alternating attribute bytes `FF, 07`: `0xFF` has
foreground equal to background, so it is *command 3, double width*, with
e5/e6 set in its low bits and the colours taken from the `0x07` beside it.
The renderer got the skip right and the width wrong twice over — it drove
pixel doubling from the screen's 40-column flag rather than from e5/e6,
and then computed x as `column × width` rather than advancing a pen by
what was actually drawn. Both are fixed; the banner is solid.

**The fixture did not catch either.** Its double-width row used attribute
`0xC0`, which is command 3 with e5/e6 *clear* — so it exercised the
attribute-inheritance branch while never doubling a pixel. It now uses the
ROM's own alternating `FF`/`07` pattern and renders visibly doubled
glyphs. Worth stating plainly: a synthetic screen is only as good as the
sub-cases it picks, and this one picked the command without the operand.

**Still unproven:** reverting the x-position fix does not change the
fixture output, and I could not work out why within a reasonable time. So
that specific regression is currently covered by *looking at the real
ROM's banner*, not by the suite. Left as an open item rather than
described as covered.

## The real-time clock — done

Booted from a real 640K ABC832 UFD-DOS system disk, the DOS prints a date
line. Before this it read:

```
Datum och tid: 19é5-é5-é5  é5.é5.é5
```

and now it reads the host's own clock, to the second:

```
DOS är UFD-DOS ver. 20
DR_: motsvarar MF_:

Datum och tid: 2026-08-29 20.25.32
ABC806
```

That is the whole gate, and it is a good one: every digit is a BCD nibble
that travelled through a shift register a bit at a time, so a date that
reads correctly means the command encoding, the register order, the clock
edges and the bit order are all right at once.

### What the device is

The E050-16 (`emu/src/rtc.c`, reimplemented from MAME's `e0516_device`)
has no bus at all. Three bits of the 74ALS259 at port `0x36` drive chip
select, clock and data, and the data line is read back as **bit 7 of port
`0x37`** — which it shares with the HRU II palette PROM's low nibble. So
one register read is thirty-odd `OUT`s and `IN`s of bit-banging, and the
protocol is a shift register: CS low arms a four-bit command (address in
bits 3:1, read/write in bit 0), then data moves a bit per clock edge.

Address 7 is not a register but a mode — continuous read-out of all seven
registers as one 56-bit transfer — and it is the one the DOS uses.

### Found the hard way

- **Reads move on the falling clock edge and writes on the rising one.**
  Not a symmetry to tidy up: get it wrong and the value is plausibly
  shaped and off by one bit position, which is exactly the kind of wrong
  that reads as a real date on some seconds and not others.
- **The port `0x37` read had to become two devices at once.** It returned
  a constant `0xFF` before, which is a data line stuck high — the clock
  appeared to answer, with all bits set. The HRU II PROM's low nibble and
  the clock's bit 7 now come from the same read.
- **CS is inverted between the latch and the chip**, so a *set* latch bit
  deselects the clock.
- **The ABC806 ties OUTSEL high**, which removes the chip's high-impedance
  read state entirely. That simplification belongs to this board, not to
  the chip, and is flagged in the source for anyone porting it.

### Verified

`make test-abc806` gains `disk-boot-and-rtc`, which boots real media and
asserts the DOS agrees with the host about the date. It needs a disk image
this repo does not commit, so it **skips loudly** without
`ABC806_TEST_DISKS` — and a skip is counted separately from a pass.

## Milestone 3: the keyboard, and a live session — done

`--interactive` is a genuine session: real 3 MHz pacing, a screen redrawn
at 30fps **in colour**, a live keyboard, and Ctrl-\ to quit. Both halves of
the scoping document's gate are met.

```
$ bin/abc806 --type $'PRINT 6*7\r' --screen
+--------------------------------------------------------------------------------+
|ABC806                                                                          |
|PRINT 6*7                                                                       |
| 42                                                                             |
|ABC806                                                                          |
```

and driven through a pty, a live session takes `PRINT 6*7` to `42` and
`PRINT "ÅÄÖ"` back out as `ÅÄÖ` — the Swedish letters round-tripping
through the keyboard, the ROM and the screen. With `--disk`, the same live
session boots real UFD-DOS and shows the date from the clock.

### `--screen` now shows what the machine shows

It used to print `AABBCC880066` for a screen reading `ABC806`, because it
dumped raw character cells and this ROM writes its banner double-width. It
now walks the attribute plane and prints one character per *drawn* cell.

That is a stronger assertion for the suite, not a laxer one: collapsing
correctly **requires** decoding the attribute plane, so a broken attribute
walk now shows up in the text dump as doubled or missing characters. The
RTC check's assertions were rewritten from the doubled form to the plain
one on those grounds.

### One decode, not two

`abc806_decode_row()` (chargen.c) was extracted so the pixel renderer and
the text renderer share the attribute state machine rather than each
carrying a copy. They had one copy each for about a day, they disagreed
over double width, and the disagreement is what found
[the two width bugs](#and-a-rendering-bug-the-picture-caught). One walk
means they cannot disagree again.

### The colour path needed a fixture, for the reason the postmortem gives

The eight colours are the thing that makes an ABC806 an ABC806, and the
boot screen is **white on black using one attribute** — so a live session
renders it perfectly with the colour mapping completely broken. That is
[the boot-screen postmortem](../../docs/postmortems/2026-08-28-boot-screen-cannot-validate.md)'s
finding, arriving a third time.

So the drawing was split out of `render.c` into a pure `text.c` — screen
in, characters out, no live machine — and `bin/abc806-chargen-dump` now
emits the text dump and the ANSI frame beside its pixel art, with `ESC`
printed as `\e` so the fixture stays diffable. The committed fixture
carries the real escape codes: `37;40` for white on black, `31;44` for red
on blue, `[4m` for underline, `30;40` where flash has dropped the pen to
the background. Swapping foreground and background in the mapping turns
the check red.

`render.c` keeps only the half that reaches for the machine — assembling
an `Abc806Screen` from the CRTC, the 74ALS259 and the two RAM planes — and
that snapshot is now built in exactly one place, so `--screen`,
`--screenshot` and the live frame cannot disagree about what the screen is.

### Found the hard way

- **`--type` fed its argument to the keyboard as raw bytes.** That is
  precisely [the postmortem's own bug](../../docs/postmortems/2026-08-28-type-raw-utf8-bytes.md),
  fixed on the ABC802 and reintroduced here by starting this target's
  `main.c` from a blank page instead of from the file that already solved
  it. `PRINT "ÅÄÖ"` reached BASIC as UTF-8 bytes and errored while an
  interactive session typing the same letters worked.
- **The ROM reports the keyboard ready long before it is listening.**
  Typing at T-state 0 arrived as `INT 6*7` — the first two characters
  silently discarded. `--type` now waits for the machine to have *drawn*
  something, which is a real readiness signal rather than a tuned delay:
  the sign-on is written at the very end of boot, immediately before the
  keyboard poll loop. `--type-at` remains, for the different problem of a
  program booting off disk that starts listening much later.
- **The palette is already in ANSI's order.** Black, red, green, yellow,
  blue, magenta, cyan, white — so a pen index is `30 + index` with no
  mapping table. A coincidence, and one worth writing down before someone
  "fixes" it.

### Verified

Two new media-free checks: `keyboard-basic-answers` (asserting on BASIC's
answer, never only on the echo —
[that exact mistake](../../docs/postmortems/2026-08-29-test-matched-the-echoed-input.md)
was caught in these suites once already) and `keyboard-swedish-roundtrip`. Both were broken on
purpose before being trusted: bypassing the UTF-8 conversion reds the
round-trip, and forcing the readiness gate open reds both.

## Known gaps

Everything below is expected at this point: two milestones in, of five.

- **No GTK front-end.** `bin/abc80-gtk` and `bin/abc802-gtk` have no
  ABC806 equivalent yet. `abc806_step()` is already extracted for one.
- **Right arrow is dropped, on inference rather than evidence.** The
  ABC802's line editor was swept byte by byte and turned out to have no
  cursor movement at all; this ROM is from the same family and the same
  year and is *assumed* to match. The sweep has not been done here.
- **The flash rate is assumed.** 2 Hz, the conventional rate. No source
  consulted gives the ABC806's own divider, and the ROM does not blink its
  cursor in software the way the ABC802's does, so nothing in the machine
  supplies the phase.
- **The high-resolution plane is not rendered.** `--screenshot` draws the
  text layer only.
- **No high-resolution graphics.** Milestone 5. `HRU-I` and `V50` are
  committed and unused; `RAD` and `HRU-II` are now in use — though HRU II
  is only being read back through port `0x37`, not used to colour a
  high-resolution plane.
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
- **RAM is 32K directly addressable**, as on real hardware, with the rest
  reachable only through EME and the map. The 544K option is not modeled.
- **No protection device.** It hangs off the same 74ALS259 as the clock;
  its latch bit (3, PROT INI) is decoded and dropped.
- **The clock is read-only in practice.** Writes are implemented — the DOS
  can set the time — but nothing persists them, so a new run reads the
  host clock again rather than whatever was last written.
