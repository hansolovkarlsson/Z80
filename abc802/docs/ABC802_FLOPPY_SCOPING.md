# Scoping: ABC-bus floppy support for the ABC802

**Status**: **executed.** Option B was built and shipped as Milestone 5 —
see the outcome section immediately below, and
[`ABC802_COMPLETED.md`](ABC802_COMPLETED.md) for the full write-up. The
rest of this document is preserved as written *before* the work, because
its value now is as a record of what was predicted versus what happened.
**Date**: 2026-08-28
**Question**: what would it actually take to give `bin/abc802` real disk
storage, and is the ABC80 target's existing floppy work reusable?

---

## Outcome

Option B was built. `bin/abc802 --disk FILE` boots real ABC800-family
media and round-trips `SAVE`/`LOAD`. All four gates below were met.

**What this document got right:** the shape of the problem entirely. The
controller really is a second computer, MAME really does contain no
protocol logic, the ABC80 bypass really does not transfer, and the
abc80sim-derived protocol really did match this ROM — the two command
constants, the select mask and the select values all decoded as predicted,
first time.

**What it got wrong:** the estimate, in the cautious direction. This was
predicted as "comparable to Milestone 3, larger than Milestone 4." It came
in smaller than either — roughly 300 lines of new C — because the three
confirmations meant the protocol needed no reverse-engineering at all.
Gates 1 and 2 fell in the same session; a blank image was enough to see
the ROM issue real read commands.

**The one risk that mattered** was the one flagged as most likely: the
status byte. Not its bit *polarity*, as guessed, but a stricter constraint
missed here entirely — the ROM treats a status of `0x00` **or** `0xFF` as
"no device" and aborts. Reading the ROM's own poll loop settled it in
minutes.

**The interleave contradiction was resolved by experiment**, in favour of
this repository's own earlier finding: with interleave disabled and
nothing else changed, real media stops booting.

**One genuinely new gap surfaced** that no amount of scoping would have
predicted: `--type` was unusable for disk work, because the ROM reports
the keyboard ready long before a booting program is listening. `--type-at`
exists now because of it.

---

*Everything below this line is the document as written before the work.*

## Short answer

**Recommended: build a synthetic ABC-bus controller** — model the bus
ports and the controller's *command protocol* in C, serving sectors from a
disk image. Not a firmware-level emulation of the controller card, and not
the PC-trap bypass the ABC80 target uses.

Three things drive that conclusion, and each was surprising enough to be
worth stating before the detail:

1. **The controller is a complete second computer** — its own Z80, its own
   DMA controller, and an FD1793 FDC. "Emulate the controller" is a much
   larger project than it sounds.
2. **The ABC80 target's approach does not transfer.** Its bypass works
   because that ROM has a single sector-level routine to intercept. The
   ABC802's DOS ROM does not: one logical operation is a *sequence* of bus
   commands, so there is no equivalent place to stand.
3. **But the bus command protocol is fully documented** by an existing
   open-source implementation, and three independent details of it match
   this ROM's own code exactly. The middle option is real, and it is
   better architecture than what ABC80 has today.

Rough sizing: **comparable to Milestone 3 (pixel rendering), larger than
Milestone 4 (the GTK window)** — call it four verifiable sub-steps. The
main risk is not the protocol; it is the handshake details and the media
geometry, both of which are empirical and will need iteration against a
real disk image.

---

## What the hardware actually is

The ABC830/832/834 floppy system is not a dumb FDC on a card. MAME models
it as `luxor_55_21046`, and its machine configuration is:

| Component | Detail |
|---|---|
| CPU | **Z80 @ 4 MHz** (16 MHz XTAL / 4), with its own IM 2 daisy chain |
| DMA | **Z80 DMA** controller, with bus request/acknowledge to that CPU |
| FDC | **FD1793** |
| Firmware | 8-16 KB ROM, **five variants** (Luxor v1.05-v1.08, DiAB v2.07) |
| Glue | a **PAL16R4** — marked `NO_DUMP` in MAME, i.e. never dumped |

The host talks to it over the ABC-bus through six ports, and the card's
own CPU does all the real work: seeking, FDC programming, sector
buffering, error retry.

This is the single most important fact for scoping. It means "port MAME's
controller" is not a shortcut — MAME does not *know* the disk protocol, it
just runs the firmware that implements it.

### MAME contains no protocol logic at all

The card-side ABC-bus handlers in `lux21046.cpp` are pure plumbing:

```c
void luxor_55_21046_device::abcbus_cs(uint8_t data) {
    m_cs = (data == m_sw3->read());          // DIP-selected card address
}
uint8_t luxor_55_21046_device::abcbus_inp() {
    uint8_t data = 0xff;
    if (m_cs) { data = m_inp; m_busy = 1; }  // a byte latch
    return data;
}
void luxor_55_21046_device::abcbus_c1(uint8_t data) {
    if (m_cs) { /* pulse NMI on the card's own Z80 */ }
}
```

A latch, a busy flag, and an NMI pulse. Every byte of meaning lives in the
firmware ROM. So the choice is genuinely binary: **run real firmware, or
implement the protocol yourself.** There is no third path through MAME.

---

## Why the ABC80 target's floppy work does not transfer

`abc80/emu/src/disk.c` is a **PC-address trap**, not a device model.
(It no longer exists: this document's conclusion held, and the trap was
subsequently retired in favour of the very controller scoped here — see
ABC80 Milestone 12.)
`abc80_step()` watches for two specific addresses inside the ABC-DOS ROM:

```c
bool about_to_do_disk_io = abc80_disk_enabled() && !interrupt_will_intercept_this_step &&
                            (pc_before == 0x6068 || pc_before == 0x60A1);
```

`0x6068` is read-a-block, `0x60A1` is write-a-block. Each is a single
routine with a known calling convention, so the emulator can service the
whole operation in C and skip the bus protocol entirely. That was a sound
call for that ROM.

**The ABC802's DOS ROM is layered differently.** Disassembling the
committed `ABC802-dos.32-31.bin` (`bin/z80dasm ... -o 0x6000`) shows the
bus driver at `0x608F`-`0x616E` is *generic*: it takes a command byte in
`C`, sends a four-byte header, polls status, and block-transfers. The
callers above it issue **sequences** of those commands. From `0x66A2`:

```
66A2: CALL L6077     ; bus command 0x03
66A8: CALL L6BC0
66B0: CALL L6084     ; bus command 0x0C
66B8: CALL L6077     ; bus command 0x03 again
```

There is no single "read sector N" call to stand in front of. Trapping
here would mean intercepting a *conversation* and reconstructing intent
across several calls — strictly harder than implementing the protocol
properly, and it would have to be redone for every DOS ROM variant.

The ABC80 bypass does contribute one thing that transfers directly: the
**ABC830 sector interleave** (`ilfac = 7`, `ilmsk = 15`), which that
milestone established empirically against a real disk image. See the note
on interleave under Risks — it is not a settled question.

---

## The protocol is documented, and it matches this ROM

`sasq64/abc80sim` — already cited by ABC80's Milestone 6 — implements a
**synthetic controller** in C, about 330 lines covering two controller
types. Its command decoder:

| `k[0]` bit | Operation |
|---|---|
| `0x01` | READ SECTOR (media → controller buffer) |
| `0x02` | SECTOR TO HOST (buffer → host) |
| `0x04` | SECTOR FROM HOST (host → buffer) |
| `0x08` | WRITE SECTOR (buffer → media) |

with `k[1]` carrying the drive unit (bits 0-2) and which of four 256-byte
buffers (bits 6-7), and `k[2]`/`k[3]` the sector address.

**Three independent details of the ABC802's own ROM match this exactly**,
which is what turns "an implementation exists for a related machine" into
"this is the same protocol":

1. **The command constants decode perfectly.** The ROM has exactly two
   entry points, `L6080` (`LD C,03h`) and `L608D` (`LD C,0Ch`). Under the
   bitmask above, `0x03` = READ SECTOR + SECTOR TO HOST, and `0x0C` =
   SECTOR FROM HOST + WRITE SECTOR. Read and write, composed from
   primitives — not two arbitrary opcodes that happen to fit.
2. **The device-select mask matches.** abc80sim does
   `abcbus_select = value & 0x3f`; the ROM does `AND 3Fh` at `0x6172`,
   immediately before `OUT (01h),A`.
3. **The device-select values match.** abc80sim uses 36/44/45/46 for
   HD/MF/MO/SF; the ROM carries `2Dh`, `2Eh` and `2Ch` (45, 46, 44) in a
   table at `0x61DA`-`0x61FB`.

The ABC802 and ABC80 use the same bus and the same drives, so this is the
expected result — but it is now evidence rather than assumption.

---

## The three options

### A. PC-address trap, like ABC80's — **not recommended**

Intercept DOS ROM addresses and service disk operations in C.

- **For**: precedent in this repo; no protocol work.
- **Against**: as shown above, this ROM offers no sector-level routine to
  trap; addresses are ROM-variant-specific (there are at least two DOS ROM
  images, and the emulator already lets you choose); and it teaches the
  project nothing reusable. It would also have to be redone if a second
  ABC-bus device (printer, RTC) is ever wanted.

### B. Synthetic ABC-bus controller — **recommended**

Model ports `0x00`-`0x07` and the command state machine; serve sectors
from a raw image file.

- **For**: it is a genuine device model, so *any* software that talks to
  the bus correctly works, not just the DOS routines that were trapped.
  Protocol is documented and confirmed against this ROM. It is **shared
  infrastructure**: the ABC80 target uses the same bus and same drives, so
  this could eventually replace its PC-trap with something more honest —
  closing a known shortcut rather than adding a second one. Extends
  naturally to the other ABC-bus devices.
- **Against**: the handshake details (STAT bit polarity, busy semantics,
  the ROM's exact poll masks) are empirical and will need iteration.
- **Size**: four sub-steps, each independently verifiable. See below.

### C. Full controller emulation — **not now**

A second Z80 instance running real controller firmware, plus Z80 DMA, plus
FD1793, plus track-level disk images.

- **For**: maximum fidelity; the only option that would handle
  copy-protected media or software driving the controller below the DOS
  layer.
- **Against**: five firmware ROM variants not in this repo and separately
  licensed; an undumped PAL16R4; a whole FDC and floppy-format layer;
  and a second CPU instance in a codebase whose core is currently
  single-instance by assumption. Nothing on the roadmap needs it.
- **Worth noting**: option B does not block option C. A synthetic
  controller sits behind the same bus interface a real one would.

---

## Recommended plan

Four sub-steps, each with its own observable verification — the same shape
the ABC802 milestones have used so far, and specifically designed so that
a wrong assumption surfaces at the earliest possible step.

| # | Step | Verification gate |
|---|---|---|
| 1 | Bus ports + device select + STAT handshake | The ROM's boot-time controller scan **finds a device** where it currently finds nothing. Observable today: it writes `0x24` to CS and reads STAT. |
| 2 | Command state machine + read path | DOS can read a directory off a real image — the ABC802 equivalent of `LIB`/`DIR` listing real filenames. |
| 3 | Write path | A `SAVE` then `LOAD` round trip across separate process runs, byte-for-byte, as ABC80's Milestone 6 verified. |
| 4 | Geometry and interleave for real ABC802 media | Every directory entry on a real image resolves to a consistent file header — the same empirical check that caught ABC80's interleave bug. |

Step 1 is the important one to do first and to *stop at* if it misbehaves:
it is cheap, and it proves the STAT/busy handshake — the part most likely
to be subtly wrong — before any command work is built on top of it.

---

## Risks and open questions

Listed because each one could change the estimate, not to pad it.

- **STAT bit polarity and semantics.** MAME notes an LS240 inverting the
  status byte (`return data ^ 0xff`), and abc80sim has its own convention.
  These must be reconciled against what this ROM's poll loop at `0x6196`
  actually tests (`AND L` / `XOR H` against caller-supplied masks). Most
  likely source of a frustrating first session.
- **Media geometry for the ABC802 is not yet established.** The archive
  has both `160k/` and `640k/` ABC800-family images. Which the DOS ROM
  expects — and whether it probes — is unknown. The `0x24` (HD) select in
  the boot scan suggests it looks for a hard disk controller too.
- **Interleave is not settled.** ABC80's Milestone 6 found interleave
  (`ilfac 7`) genuinely necessary and verified it empirically; abc80sim
  ships with `INTERLEAVE 0`, i.e. disabled. Both cannot be right for the
  same media. Likely a difference in how the image files themselves were
  dumped, and it must be re-established for whatever ABC802 image is used
  rather than assumed from either source.
- **Command coverage may exceed the four known bits.** The ROM uses only
  `0x03` and `0x0C`, but initialization, geometry query, or format may use
  others. Unknown until step 2 runs against real media.
- **No ABC802 disk image is committed yet**, and ABC80's `disk003.img`
  was itself never committed (a decision recorded in that milestone). The
  same licensing/size question applies here and should be settled before
  step 2, not after.

---

## Assets already in hand

- The **ABC802 DOS ROM is committed** and verified against MAME's CRC32
  and SHA1, and its bus driver is fully disassembled by this project's own
  `bin/z80dasm` (`0x608F`-`0x61B0`).
- **Real media exists** in the same archive already used as ground truth
  for ABC80: `abc80.net/archive/luxor/sw/disk_images/ABC800/`, with
  `160k/`, `640k/`, `cpm/` and several software collections.
- The **ABC80 target's interleave knowledge** and its disk-image handling
  are directly relevant even though its trap mechanism is not.
- `bin/abc802 --screenshot` and `--type` make each verification gate
  above checkable without a human at a keyboard.

---

## Sources

- MAME `src/devices/bus/abcbus/lux21046.cpp` and `abcbus.h` — the
  controller's machine configuration, ROM variants, and the card-side bus
  handlers quoted above.
- `sasq64/abc80sim`, `src/disk.c` and `src/abcio.c` — the synthetic
  controller, its command bitmask, device-select values, and interleave
  parameters.
- This repo's own `abc802/resources/rom/ABC802-dos.32-31.bin`,
  disassembled with `bin/z80dasm` — every claim above about what the
  ABC802 ROM does was read out of that disassembly, not inferred from the
  other two sources.
- `abc80/emu/src/disk.c` (as it stood then) and ABC80's Milestone 6 write-up in
  `../../abc80/docs/ABC80_COMPLETED.md` — the existing bypass and the
  interleave derivation.
