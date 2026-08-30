# Scoping: an ABC806 machine target

**Status**: **not started.** Written before any work, on the same terms as
[`ABC802_FLOPPY_SCOPING.md`](../../abc802/docs/ABC802_FLOPPY_SCOPING.md) —
so that whatever happens next can be compared against what was predicted
here. Nothing in this document has been run; it is a review of the
available ROMs, of MAME's own driver, and of what this repository already
has. Every claim is sourced, and the ones that are inference rather than
observation say so.

**Date**: 2026-08-29
**Question**: is an ABC806 emulator possible with the information that
exists, and what would it actually cost?

---

## Short answer

**Yes, and it is better resourced than the ABC802 was at the equivalent
point.** Build it, with the memory management unit as milestone 1 and a
hard stop if that does not come out clean.

Three things drive that conclusion:

1. **The firmware is complete and identified.** Not just the BASIC and DOS
   ROMs but the four small video PROMs and both PALs — better than the
   ABC830 controller, whose PAL16R4 MAME marks `NO_DUMP` to this day.
2. **Roughly half the machine is already built here.** The ABC806 is an
   ABC800-family machine: same Z80 peripherals, same CRTC, same serial
   keyboard, same ABC bus, and a ROM layout the same 32K shape as the
   ABC802's. `abcbus/`, the CTC/SIO/DART models and the whole test
   apparatus transfer.
3. **The one hard part is identified up front.** The MMU is the risk, and
   it is also the thing MAME itself lists as unfinished. That is unusual
   and useful: the difficulty is known before starting rather than
   discovered in month two.

**Rough sizing**: comparable to the entire ABC802 target — ten milestones
— but with a large head start. Call it four genuinely new subsystems.

---

## What the machine is

An ABC806 (1983) is the top of the ABC800 family. Against the ABC802 this
repository already emulates:

| | ABC802 | ABC806 |
|---|---|---|
| CPU | Z80A @ 3 MHz | same |
| RAM | 64K flat | **160K behind an MMU** (544K option) |
| ROM | 32K: BASIC + DOS + option | same 32K shape, different contents |
| Video | 80×24 mono, attributes in the chargen | **80×24 colour, separate attribute RAM** |
| Graphics | none | **240×240, 4bpp, 16-bank video RAM** |
| Peripherals | CTC, SIO, DART | same |
| Extras | — | **E0516 RTC**, a protection device |
| Default disk | none fitted | ABC832 (640K) |

The ROM layout is worth stating precisely, because it is the same shape
the ABC802 loader already handles: six 4K BASIC PROMs at `0x0000`-`0x5FFF`,
a 4K DOS PROM at `0x6000`, and a 4K option PROM at `0x7000`. The DOS PROMs
are `66-21` (UFD-DOS v.19, 1984-03-02) and `66-31` (v.20, 1984-04-03) —
**the same two DOS versions, to the day, as the ABC802's `32-21`/`32-31`**.

---

## What transfers, and what does not

### Transfers largely unchanged

- **`abcbus/disk.c`** — same bus, same drives, and MAME's ABC806 config
  defaults the bus to an `abc832`, which is the drive type this repository
  has just acquired real system media for.
- **The Z80 peripherals.** CTC, SIO, DART, and the IM 2 daisy chain.
- **The MC6845 register model**, and the 40/80-column mechanism.
- **The serial keyboard** on the DART.
- **The Swedish/Finnish charset** and `abc802_utf8_to_chars()`.
- **The whole verification apparatus**: `--screenshot`, the PNG writer,
  `--type`/`--type-at`, the pty harness, `bin/abcdisk`, `scripts/testlib.sh`.

### Does *not* transfer

- **The character-generator attribute decode.** The ABC802's cleverest
  piece — attributes carried in the *font's own output byte* — is an
  ABC802 mechanism. The ABC806 has a separate attribute RAM plane, so
  Milestone 3's work informs nothing here beyond the general shape.
- **The flat memory map.** `abc802/emu/src/memory.c` reproduces one
  overlay decided by one line (LRS) plus an M1 quirk. The ABC806 needs a
  real page map.

---

## The four genuinely new pieces

### 1. The MMU — the risk, and milestone 1

Memory is a 16-entry page map (`m_map[offset >> 12]` in MAME) feeding a
**PAL16L8** that decides, per access, whether the CPU sees ROM, RAM, video
RAM or character RAM. MAME's `read_pal_p4()` implements it — and this is
the important detail — **with the actual PAL lookup commented out**,
replaced by a hand-written approximation, with the `abc806 30K banking`
TODO sitting immediately beside it.

That is simultaneously the largest risk and the clearest opportunity.
`ABC-P4-1.bin` **is dumped** and in the archive. Evaluating the real PAL
rather than approximating it would do something MAME has not, and it is
exactly the move this project already made with ABC80's `attr`, `hsync`
and `line` PROMs.

Whether a PAL16L8 dump in that file's format can be evaluated directly is
**not yet established** — it is 2,769 bytes, which is not a raw truth
table, so it is presumably JEDEC or a fuse map needing a small equation
evaluator. That question should be answered before anything else is built.

### 2. Colour text with an attribute plane

80×24 with a parallel attribute RAM carrying foreground, background,
flash, underline and double-height, plus a `RAD` PROM supplying the
character line address and an **undumped attribute-handler PAL** (see
Risks). Eight colours.

### 3. High-resolution graphics

240×240 at 4 bits per pixel, in video RAM banked 16 ways by the `hrs`
register, with a 16-entry colour lookup written through `hrc`. Rendering
is straightforward; the timing PROMs (`HRU I`, `V50`) place it on screen.

### 4. The RTC and the protection device

An E0516 real-time clock, bit-banged through a 74ALS259 latch. Small. The
protection device (`prot_ini`/`prot_din` on the same latch) is not
identified in MAME's source beyond those two lines and would need
investigation — or stubbing, if nothing on a boot path reads it.

---

## Options

### A. Don't. Point people at MAME — **worth stating, not chosen**

MAME emulates this machine more completely than a first cut here would.
The honest case for building anyway is the same one that justified the
ABC802: this repository is a place where the *reasoning* is written down,
and where a PROM gets decoded for real rather than approximated. If the
MMU cannot be done properly, this option becomes the right one.

### B. Full target, MMU first — **recommended**

Bring up the machine in the milestone shape the ABC802 used, with the MMU
as milestone 1 and an explicit stop-if-wrong gate.

- **For**: the head start is real; the DOS layer should work almost
  immediately; the PAL question is genuinely interesting and
  MAME-exceeding if it lands.
- **Against**: it is a large project, and the two undumped parts (below)
  may cap how faithful the video can get.

### C. Text-mode-only target, no high-res — not recommended

Tempting, and it would boot BASIC quickly. But the high-res unit is the
*reason* an ABC806 is different from an ABC802, and a target that omits it
is an ABC802 with more RAM. If the graphics are out of reach, option A is
the more honest answer.

---

## Recommended plan

Five gates, each independently checkable, ordered so a wrong assumption
surfaces as early as possible.

| # | Step | Gate |
|---|---|---|
| 1 | **The MMU.** Decode `ABC-P4-1.bin`, or reproduce MAME's approximation, and reach the ROM's first instruction | The machine executes past reset and programs the CRTC — the same "CRTC programmed: yes" signal that gated ABC802 Milestone 1 |
| 2 | **Text video** | `--screenshot` renders the ROM's own sign-on banner, in colour, against the attribute plane |
| 3 | **Keyboard and a live session** | `--type "PRINT 6*7"` answers `42`; `--interactive` works, on the terms both existing targets established |
| 4 | **Disk** | `--disk` with the ABC832 system media already here boots, and `BYE` reaches the DOS. Should be nearly free — same bus, same DOS |
| 5 | **High-resolution graphics** | A BASIC program using the GRAF806 option-PROM commands draws something that matches a real screenshot |

**Stop at gate 1 if it misbehaves.** It is cheap, it is the only part that
is genuinely unknown, and everything else rests on it.

---

## Risks and open questions

Listed because each could change the answer, not to pad it.

- **Can the PAL dump actually be evaluated?** `ABC-P4-1.bin` is 2,769
  bytes — not a raw truth table. If it is a fuse map, a small evaluator is
  needed; if it is an unusable format, the MMU falls back to MAME's
  approximation and inherits its `30K banking` gap. **This is the single
  question worth answering before committing to the project.**
- **Two parts are not dumped anywhere.** MAME's source carries a
  commented-out region for the **attribute-handler PAL** — with only a
  pin list, no contents — and `V60`, the 60 Hz vertical timing PROM, is
  marked `NO_DUMP`. The first may limit attribute fidelity; the second
  only matters for 60 Hz operation.
- **The protection device is unidentified.** Two callbacks in MAME and
  nothing else. Unknown whether any boot path depends on it.
- **160K of RAM breaks an assumption.** `Z80.memory` is a flat 64K array
  and `fetch_byte()` indexes it directly, deliberately bypassing
  `bus_read_hook`. The ABC802 worked around this by keeping the selected
  32K physically resident. A 16-page MMU over 160K may not fit that trick,
  and **may be the first thing to genuinely require changing the shared
  core's instruction-fetch path** — which two working targets depend on.
  Assess this at gate 1, not later.
- **Scope discipline.** The ABC802 took ten milestones with a simpler
  machine and no colour. Anyone starting this should expect it to be the
  largest single piece of work in the repository.

---

## Assets in hand

- **A complete, identified ROM set** at
  `abc80.net/archive/luxor/Prom/fw/ABC806/`, with a `00index.txt` naming
  every part: six BASIC PROMs, five DOS variants, two option PROMs, two
  character generators, `RAD`, `HRU I`, `HRU II`, `V50`, and both PALs.
  Every one has a CRC32 and SHA1 in MAME to verify against, exactly as
  `abc802/resources/rom/README.md` documents doing.
- **Real ABC832 system media**, already downloaded and in
  `abc802/resources/disks/` — and MAME defaults the ABC806's bus to an
  `abc832`, so gate 4 has its test material before it starts.
- **MAME's own implementation** (`src/mame/luxor/abc80x.cpp` and
  `abc80x_v.cpp`, BSD-3-Clause, Curt Coder) — near-complete, with a
  three-line TODO list for the whole family.
- **Documentation**: `ABC806-dator-manual-BASIC-II.pdf`, a service manual,
  the schematic, and `graf806-option-prom-manual.pdf` for the graphics
  commands gate 5 needs.
- **This repository**: the shared core, `abcbus/`, `bin/abcdisk`, the pty
  harness, and two worked examples of bringing up a machine of this family.

---

## Sources

- `abc80.net/archive/luxor/Prom/fw/ABC806/00index.txt` — the ROM
  inventory, read directly.
- MAME `src/mame/luxor/abc80x.cpp` and `abc80x_v.cpp` — the memory decode,
  the PAL simulation and its commented-out lookup, the ROM layout, the
  machine configuration, the row renderer and the high-res update. Every
  statement above about what the hardware does was read out of these two
  files, not inferred from the machine's marketing.
- `abc80.net/archive/luxor/ABC80x/` — the manuals named above.
- This repository's own `ABC802_FLOPPY_SCOPING.md`, for the form.
