# ABC806 emulator roadmap

Status and known gaps for `bin/abc806`, the Luxor ABC806 machine target.
[`ABC806_SCOPING.md`](ABC806_SCOPING.md) is the feasibility review written
before any of this existed, and is still the plan being followed; this file
is what actually works.

There is no `ABC806_REFERENCE.md` yet. The hardware facts established so
far live in `emu/src/memory.c`'s header and in `resources/rom/README.md`;
a consolidated reference is worth writing once milestone 2 has produced
enough of them to consolidate.

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

## Known gaps

Everything below is expected: milestone 1 was the memory map, nothing else.

- **No video rendering.** The CRTC is a register file; nothing draws.
  Milestone 2.
- **No banner.** The ROM clears the screen and waits without writing any
  visible text, where the ABC802 shows its sign-on first. Whether that is
  a real difference, or something the machine wants before it will draw,
  is the open question milestone 2 starts from. It polls the keyboard, so
  milestone 3 may simply answer it.
- **No keyboard, no disk, no interactive mode.** Milestones 3 and 4. The
  ABC-bus is wired to the shared `abcbus/` card already, since the port
  decode came across with the rest, but nothing has been attached.
- **No high-resolution graphics.** Milestone 5. The video PROMs are
  committed and unused.
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
- **Not in `make test`.** There is nothing stable to assert on yet beyond
  the boot line. A check belongs here at milestone 2, when there is a
  screen to compare.
- **RAM is 32K directly addressable**, as on real hardware, with the rest
  reachable only through EME and the map. The 544K option is not modeled.
- **No RTC, no protection device.** Both hang off the same 74ALS259; the
  latch bits are decoded and dropped.
