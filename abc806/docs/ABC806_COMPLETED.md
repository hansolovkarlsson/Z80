# ABC806: completed milestones

Finished work on `bin/abc806`, moved here out of `ABC806_ROADMAP.md` so
that file stays a short answer to "what works, what doesn't, and what's
next" rather than a growing archive — the same split
[`ABC802_COMPLETED.md`](../../abc802/docs/ABC802_COMPLETED.md) and
[`ABC80_COMPLETED.md`](../../abc80/docs/ABC80_COMPLETED.md) already made.
Nothing here has been rewritten: these are the write-ups as recorded when
the work was done, including the "found the hard way" notes, which are the
part most worth keeping.

This target is unusually dense in those notes, because very little about
the ABC806 is guessable from a memory map. For the facts themselves,
organised by subsystem rather than by when they were found, see
[`ABC806_REFERENCE.md`](ABC806_REFERENCE.md); for current status and open
gaps see [`ABC806_ROADMAP.md`](ABC806_ROADMAP.md); for the cross-cutting
lessons see [`../../docs/JOURNAL.md`](../../docs/JOURNAL.md) and
[`../../docs/postmortems/`](../../docs/postmortems/).

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

## Milestone 4: disk — done, and it was as free as predicted

`--disk` boots real ABC832 UFD-DOS media, `BYE` leaves BASIC for the DOS
command shell, and the shell loads and runs a real program off the disk:

```
** Disc operating system - Ver 6.20 **
   Ver 6.02, 1983-04-21
   Copyright 1982 Dataindustrier AB
-
```

then `LIB` gives a full directory listing — `DOSGEN.ABS`, `COPY.ABS`,
`ISAMDEMO.BAC` and the rest. That matters more than the banner does: `LIB`
is a *program on the disk*, not a shell built-in, so a listing means the
bus, the controller, the filesystem and the DOS's own loader all worked
end to end rather than a boot sector merely having run.

The scoping document predicted this milestone would be "nearly free — same
bus, same DOS", and it was: no ABC806-specific work was needed at all. The
shared `abcbus/` card and the RTC were between them the whole of it.

Covered by `dos-shell` and `dos-runs-lib`, both media-gated. They are two
runs rather than one because `LIB`'s listing scrolls the shell's banner off
the top, so each assertion is made against a screen that still holds it —
it failed that way first. Both were verified by breaking the ABC-bus card
select on purpose, which reds them along with the RTC check.

## Milestone 5: high-resolution graphics — done

**The ABC806 now draws into its high-resolution plane.** From BASIC:

```
FGPOINT 10,10,7:FGLINE 100,100,7
```

writes exactly 91 bytes into the plane, and replaying them gives a clean
45° diagonal: x from 108 down to 18 as y runs 139 to 229. That is
`(10,10)`→`(100,100)` in BASIC's coordinates, with y flipped (`239 − y`)
and an +8 viewport origin the ROM keeps at `0xFEF8`.

and `--screenshot` now draws it:

```
$ bin/abc806 --cycles 400000000 \
      --type $'FGCTL 1:FGPOINT 10,10,7:FGLINE 100,100,7\r' \
      --screenshot line.png
```

renders the ROM's text with a clean white diagonal beneath it, rising
left-to-right — which is what a `(10,10)`→`(100,100)` line looks like once
y is flipped.

**`FGCTL` is what turns the layer on.** It programs the `hrc` colour
lookup; without it every entry is zero, every dot is transparent pen 0,
and a perfectly drawn line is invisible. Any non-zero argument except 128
programs three or four entries.

### The mechanism: where the code runs from *is* the switch

**When the instruction currently executing was fetched from
`0x7800`-`0x7FFF`, accesses below `0x7800` go to the high-resolution plane
instead of ROM.** Nothing is switched. No port is written, no latch bit
changes, no bank register moves. That is why two sessions of looking for a
software trigger found nothing: there isn't one.

It explains every fact that had refused to fit:

- **The ROM's 30,720-byte memset** (`LD (HL),0` then `LDIR` at `0x7CB2`)
  is a clear of the plane — and it lives at `0x7CAC`, *inside* the window,
  so both its reads and its writes land there. The `LDIR` propagate only
  works because its reads reach the plane too. Earlier this smeared ROM
  bytes across the plane; now it leaves it genuinely zeroed.
- **`FGLINE`'s plotter at `0x7E31`** is in the window. Its masked
  read-modify-write reads and writes real pixels.
- **`FGPOINT`'s executor at `0x763B` is not** — consistent with the
  two-argument `FGPOINT x,y`, which only moves the graphics cursor. (The
  *three*-argument form `FGPOINT x,y,pen` does plot, through the drawing
  code that is in the window.)
- **The interpreter's data reads at `0x05xx`** run from code all over the
  low 32K, outside the window, so they still read ROM.

The placement of that clear routine at `0x7CAC`, immediately above the
30,720-byte region it clears, was noted as "unlikely to be a coincidence"
before the mechanism was known. It wasn't one.

### Found in MAME — as a TODO it does not implement

`abc806_state::read_pal_p4()` carries this, commented out:

```c
/*
    if (!m1l && (offset < 0x7800)
    {
        TODO 0..30k read from videoram if fetch opcode from 7800-7fff
        romd = 1;
        hre = 1;
        mux = 0;
    }
*/
```

Note that the sketch contradicts itself: the condition tests `!m1l` — *this
access is an opcode fetch* — while the comment describes the opposite,
diverting because the opcode *was* fetched from `0x7800`-`0x7FFF`. The
comment is the one that matches the hardware; implemented as written, the
condition does nothing useful. Presumably that is why it was left
unfinished.

So this is one of the places [`ABC806_SCOPING.md`](ABC806_SCOPING.md) hoped
for: behaviour the reference implementation documents but does not do. The
physical address is MAME's own `mux = 0` form,
`(m_hrs & 0xf0) << 11 | (offset & 0x7fff)` — the same expression the
KEYDTR path already used.

`abc806_note_instruction_fetch()` already supplied exactly the latched-M1
information this needs; it was added for the character-RAM window in
milestone 1 and needed no change.

### Found the hard way

**A one-sided bounds check broke the disk.** The first version tested only
`current_fetch_pc < 0x7800`, which admits every address in high RAM as
well. Two reads made by DOS code running at `0xC178` and `0xC32A` were
diverted into the plane, and the DOS's sign-on came out as
`** Disc operating system - Ver 6.00 **` with a truncated date instead of
`Ver 6.20` — a corruption in a completely unrelated subsystem, from a
missing upper bound. `dos-runs-lib` caught it.

### Verified

Two media-free checks. `graphics-fgline-draws` asserts **exactly 91**
bytes: 90 Bresenham steps plus the start point, one byte each because the
line is diagonal at a 128-byte pitch. An exact count makes it a geometry
check rather than a "something happened" check.
`graphics-plane-clears` asserts the plane comes up genuinely zero, which
only holds if the memset's *reads* reach the plane.

Three deliberate regressions were tried. Narrowing the window's address
range reds `graphics-fgline-draws`; widening the fetch-PC bound reds
`dos-runs-lib` and nothing else, so that check is its only cover; and
**changing the HRS bank shift is caught by nothing**, because every test
runs with `hrs = 0`. That last one is an honest gap, recorded below rather
than papered over.

### The renderer, and three things about it that are not guessable

Reimplemented from MAME's `abc806_state::hr_update()`:

1. **The displayed bank is HRS's *low* nibble** (VM15-VM18) while the bank
   the CPU writes through is the *high* nibble (F15-F18). They are
   independent on purpose — the machine can draw into one area while
   showing another — so reading the wrong nibble works right up until
   something double-buffers.
2. **One byte becomes four pixels, through two lookups.** Each nibble
   indexes `hrc[]`, and each `hrc` entry is *itself* two pixels of four
   bits: bit 3 opaque, bits 2:0 the pen. **The palette carries the
   horizontal resolution** — program both halves of an entry alike and the
   plane is 240 wide, differently and it is 480.
3. **The layer is not simply on top.** A dot is drawn where its opaque bit
   is set *or* where the text layer left black, so text punches through its
   own foreground and neither plane needs a mask.

The plane sits **16 pixels left of text column 0** (MAME draws text at
`hbp + (column + 4) * 6` and the plane at `hbp + 24 - 16`, so the shared
porch cancels and only the difference matters).

A zero `hrc` makes every dot transparent pen 0, so the layer disables
itself and needs no enable flag — which is exactly the state the machine
boots in.

### Verified

`bin/abc806-chargen-dump` now renders a synthetic plane alongside its
synthetic text, and the committed fixture covers the four things that can
break independently: the bank nibble (the plane is written in bank 1 with
`hrs = 0x01`, so a renderer reading the high nibble shows nothing), the
four-pixel expansion, the opaque rule (one row uses `hrc[2] = 0xA0` —
opaque pen 2 then *transparent* pen 0 — so alternate pixels let the text
show through), and the −16 offset (the 24-byte run ends at x=79, not 95).
Each of the three was broken on purpose and reds the check.

Six further checks pin the pen encoding, asserting the plane's *full* set
of byte values (`C0 CC` for pen 0, and so on — a horizontal line writes
whole bytes along its body and a half byte at each end). Matching only the
leading value let a write path that masked off the low nibble pass; with
both asserted, that sabotage reds seven checks instead of one.

### The pen encoding — solved, and it is a four-colour mode

**A `FG*` command's pen argument is masked to two bits and selects the
plane nibble `0xC | (pen & 3)`.** Pen 0 writes `C`, pen 1 `D`, pen 2 `E`,
pen 3 `F` — and pen 4 wraps back onto `C`, pen 7 onto `F`. `FGCTL` supplies
the palette for exactly those four entries: `FGCTL 1` sets `hrc[D..F]` to
`FF` (everything white), `FGCTL 2` sets `99`/`AA`/`BB` (pens 1, 2, 3).

Colour works. Three lines drawn under `FGCTL 2` render red, green and
yellow — confirmed by reading the PNG's pixels, `#FF0000`, `#00FF00`,
`#FFFF00`.

**This section previously said colour was broken. It was not**, and the two
observations behind that claim were both misread:

- *"Four lines came out white."* They were drawn under `FGCTL 1`, whose
  palette is white for every pen. Under `FGCTL 2` they are properly
  coloured. The screenshot was small and I judged it by eye rather than by
  sampling it.
- *"Two lines vanished."* One used pen 4, which wraps onto nibble `C` —
  unprogrammed under `FGCTL 1`, therefore transparent. The other was at a
  screen row still covered by text, and the high-resolution layer only
  shows through where text is black. Both are correct behaviour.

`bin/abc806`'s summary now prints the plane's distinct byte values, not
just a count, because a count cannot tell one pen from another — which is
precisely what allowed a working renderer to look broken.

### Still open

- **`HRU-I` and `V50`** remain unused. They place the plane on a real
  screen; the offsets here come from MAME's constants instead.
- **Only the four-colour mode is exercised.** `FGCTL 64` and `FGCTL 255`
  program `hrc[C]` as well and with different values, so other modes exist
  and none is tested.
