# Game Boy Hardware Reference

A reference for the DMG (original 1989 Game Boy) hardware this
project's emulator (`gameboy/src/`) targets: the memory map, cartridge/
MBC banking, the PPU (graphics), the APU (sound), the timer, the
joypad, and interrupts. Grounded against [Pan
Docs](https://gbdev.io/pandocs/) throughout, the same primary source
`gameboy/docs/GAMEBOY_ROADMAP.md`'s own phase-by-phase status cites — each
section below names the specific pandocs page(s) it's drawn from, the
same discipline `cpm/docs/CPM_REFERENCE.md` follows for the CP/M side.

Where this project's own implementation status matters — a
simplification, a deliberately deferred obscure quirk, a genuine gap —
it's called out explicitly in that section, not just lumped into one
end-of-document list; see also `gameboy/docs/GAMEBOY_ROADMAP.md`'s own
Status section for the fuller phase-by-phase story (bugs found, root
causes, exact test-ROM pass counts) behind each of these. Everything
else describes real DMG hardware behavior, independent of this
codebase. This document doesn't cover the CPU's own instruction set —
see `gameboy/docs/CPU_REFERENCE.md` for that.

## Memory map

| Range | Size | Contents |
|---|---|---|
| `0x0000`-`0x3FFF` | 16 KiB | ROM bank 0 (fixed — never banked) |
| `0x4000`-`0x7FFF` | 16 KiB | ROM bank 1-N (switchable via the cartridge's MBC — see [Cartridge/MBC](#cartridgembc)) |
| `0x8000`-`0x9FFF` | 8 KiB | VRAM (tile data + tile maps — see [PPU](#ppu-graphics)) |
| `0xA000`-`0xBFFF` | 8 KiB | External cartridge RAM, if present (switchable, or MBC3's RTC registers — see [Cartridge/MBC](#cartridgembc)) |
| `0xC000`-`0xDFFF` | 8 KiB | Work RAM (WRAM) |
| `0xE000`-`0xFDFF` | ~7.5 KiB | Echo RAM — a real hardware mirror of `0xC000`-`0xDDFF`, not separate storage |
| `0xFE00`-`0xFE9F` | 160 B | OAM (Object Attribute Memory — sprite table, 40 × 4 bytes) |
| `0xFEA0`-`0xFEFF` | 96 B | Unusable — real hardware's behavior here depends on PPU state/revision |
| `0xFF00`-`0xFF7F` | 128 B | I/O registers (joypad, timer, sound, PPU, etc. — memory-mapped, no separate I/O space) |
| `0xFF80`-`0xFFFE` | 127 B | HRAM (High RAM — fast, always-accessible even during OAM DMA) |
| `0xFFFF` | 1 B | `IE` (Interrupt Enable) |

`gameboy/src/mmu.c`'s `gb_read_byte()`/`gb_write_byte()` implement this
routing directly: `0x0000`-`0x7FFF` and `0xA000`-`0xBFFF` go to
`cart.c`; the named I/O sub-ranges below go to their own module
(joypad/timer/APU/PPU); everything else (WRAM, OAM, the unusable gap,
the general I/O register block, HRAM, `IE`) lives in one flat
`cpu->memory[65536]` array, indexed by the full 16-bit address even
though the cartridge-routed ranges never actually touch it. Echo RAM is
modeled by redirecting any address in `0xE000`-`0xFDFF` onto the
corresponding real WRAM byte before the array lookup, rather than
keeping two copies in sync.

### I/O register summary

The specific registers each subsystem owns (full detail in that
subsystem's own section below):

| Address | Register | Owner |
|---|---|---|
| `0xFF00` | `P1`/`JOYP` | [Joypad](#joypad) |
| `0xFF01`-`0xFF02` | `SB`/`SC` (serial) | not modeled as real hardware — see [Serial](#serial-stub) |
| `0xFF04`-`0xFF07` | `DIV`/`TIMA`/`TMA`/`TAC` | [Timer](#timer) |
| `0xFF0F` | `IF` (Interrupt Flag) | [Interrupts](#interrupts) |
| `0xFF10`-`0xFF26` | `NR10`-`NR52` | [APU](#apu-sound) |
| `0xFF30`-`0xFF3F` | Wave RAM | [APU](#apu-sound) |
| `0xFF40`-`0xFF4B` | `LCDC`/`STAT`/`SCY`/`SCX`/`LY`/`LYC`/`DMA`/`BGP`/`OBP0`/`OBP1`/`WY`/`WX` | [PPU](#ppu-graphics) |
| `0xFFFF` | `IE` (Interrupt Enable) | [Interrupts](#interrupts) |

`0xFF15`, `0xFF1F`, and `0xFF27`-`0xFF2F` are real, unconnected gaps
within the APU's own register span — they read back as `$FF` and
ignore writes, not accidentally exposed as plain RAM (a real bug this
project found and fixed via Blargg's `dmg_sound` `01-registers.gb` —
see `gameboy/docs/GAMEBOY_ROADMAP.md`'s Phase 5 status).

### Serial (stub)

`SB`/`SC` (`0xFF01`-`0xFF02`) are **not** modeled as real hardware —
there's no link-cable peer to actually transfer bytes to. Writing `SC`
with the "start transfer, internal clock" bits set (`(val & 0x81) ==
0x81`) invokes a host-side output hook (`gb_serial_output_hook` in
`mmu.c`) instead, printing the byte at `SB`. This is exactly the
mechanism Blargg's own `cpu_instrs`/`instr_timing` test ROMs use to
report `Passed`/`Failed` text without a real link cable attached — not
a simulation of the real serial protocol's bit-clocking/timing at all.

## Cartridge/MBC

`gameboy/src/cart.c` parses a real cartridge header and implements
bank switching for the overwhelming majority of real cartridges:
no-MBC, MBC1, MBC3 (with its real-time clock), and MBC5. Grounded
against pandocs' `The_Cartridge_Header.md`/`nombc.md`/`MBC1.md`/
`MBC3.md`/`MBC5.md`.

### Header

Fixed fields at `0x0100`-`0x014F`, the same in every cartridge
regardless of MBC type:

| Offset | Field | Notes |
|---|---|---|
| `0x0134`-`0x0143` | Title | ASCII, space-padded |
| `0x0147` | Cartridge type | Selects the MBC (see table below) |
| `0x0148` | ROM size code | Bank count = `2 << code` (16 KiB banks); codes above `0x08` are unofficial/unconfirmed and rejected rather than guessed |
| `0x0149` | RAM size code | `0x00`=none, `0x02`=1×8 KiB, `0x03`=4×8 KiB, `0x04`=16×8 KiB, `0x05`=8×8 KiB; `0x01` is a documented-unused code, rejected |
| `0x014D` | Header checksum | `checksum = checksum - rom[addr] - 1` over `0x0134`-`0x014C`; a real Game Boy refuses to boot at all if this doesn't match |

Cartridge-type byte → MBC mapping this project supports:

| Type byte(s) | MBC | RAM | Battery | RTC |
|---|---|---|---|---|
| `0x00` | none | — | — | — |
| `0x08`/`0x09` | none | yes | `0x09` only | — |
| `0x01`-`0x03` | MBC1 | `0x02`/`0x03` | `0x03` only | — |
| `0x0F`-`0x13` | MBC3 | `0x10`/`0x12`/`0x13` | `0x0F`/`0x10`/`0x13` | `0x0F`/`0x10` |
| `0x19`-`0x1E` | MBC5 | `0x1A`/`0x1B`/`0x1D`/`0x1E` | `0x1B`/`0x1E` | — |

Any other type byte is rejected at load time with an explicit error,
not silently guessed at. `+RUMBLE` MBC5 variants (`0x1C`-`0x1E`) load
and bank-switch identically to plain MBC5 — the rumble motor itself
isn't modeled (no host feedback channel to drive it), but that doesn't
affect memory behavior at all.

### No-MBC

The simplest case: a plain, unbanked 32 KiB ROM (exactly 2 banks) with
no control registers at all — writes to `0x0000`-`0x7FFF` are silently
ignored.

### MBC1

- `0x0000`-`0x1FFF` (write): RAM enable — enabled iff the low nibble of
  the written value is `0x0A`.
- `0x2000`-`0x3FFF` (write): ROM bank number, low 5 bits. **`0`→`1`
  quirk**: a value of 0 here is treated as 1 — real hardware "cannot
  duplicate bank 0" through this register, so bank 0 is only ever
  reachable at `0x0000`-`0x3FFF`, never through this window.
- `0x4000`-`0x5FFF` (write): a secondary 2-bit register, meaning
  depends on cartridge size — on ROMs larger than 512 KiB (33+ banks),
  it extends the ROM bank number (bits 5-6); on ROMs with more than one
  RAM bank, it selects the RAM bank instead. A cartridge is one or the
  other, never both.
- `0x6000`-`0x7FFF` (write): banking mode — `0`=simple (the secondary
  register above only ever affects RAM banking, if applicable), `1`=
  advanced (the secondary register *also* affects which bank
  `0x0000`-`0x3FFF` itself reads, letting banks `0x20`/`0x40`/`0x60` be
  reached there — the only way to reach them at all, since the primary
  register's own `0`→`1` quirk can never land exactly on one of those
  multiples of 0x20).

### MBC3

Same shape as MBC1's ROM/RAM-enable registers, with two real
differences: no simple/advanced mode split at all (banks `0x20`/`0x40`/
`0x60` are ordinary, directly reachable banks, no secondary-register
games needed), and a 7-bit (not 5-bit) primary ROM bank register — same
`0`→`1` quirk.

- `0x0000`-`0x1FFF` (write): RAM/RTC enable, same `0x0A`-in-low-nibble
  convention as MBC1.
- `0x2000`-`0x3FFF` (write): ROM bank number, 7 bits, `0`→`1` quirk.
- `0x4000`-`0x5FFF` (write): `0x00`-`0x07` selects a RAM bank;
  `0x08`-`0x0C` selects one of the 5 RTC registers (Seconds, Minutes,
  Hours, Day-low, Day-high) to appear at `0xA000`-`0xBFFF` instead.
- `0x6000`-`0x7FFF` (write): the RTC latch — writing `0x00` then `0x01`
  (as two separate writes) snapshots the live, ticking RTC registers
  into a separate latched copy, which is what `0xA000`-`0xBFFF`
  actually exposes when an RTC register is selected. This lets a
  program read a stable set of values while the clock keeps advancing
  underneath, rather than risking a read mid-tick.

**Implementation status**: the RTC registers exist and the latch
sequence works, but the clock isn't actually advanced by wall-clock
time yet — `rtc[]` only changes if a program writes to it directly.

### MBC5

The cleanest of the three: no `0`→`1` quirk at all (pandocs is explicit
that "bank 0 is actually bank 0" here, unlike MBC1/MBC3), and a full
9-bit ROM bank number (up to 512 banks = 8 MiB).

- `0x0000`-`0x1FFF` (write): RAM enable, same convention as above.
- `0x2000`-`0x2FFF` (write): ROM bank number, low 8 bits.
- `0x3000`-`0x3FFF` (write): ROM bank number, bit 8 (the 9th bit).
- `0x4000`-`0x5FFF` (write): 4-bit RAM bank number (bit 3 doubles as
  the rumble-motor line on `+RUMBLE` carts — not modeled, see above).
- `0x6000`-`0x7FFF`: no register here at all on MBC5.

## PPU (graphics)

`gameboy/src/ppu.c` implements the LCD controller: all twelve registers
(`0xFF40`-`0xFF4B`), the mode/timing state machine, and a
scanline-at-a-time renderer covering background, window, and objects
(sprites). Grounded against pandocs' `LCDC.md`/`STAT.md`/
`Tile_Data.md`/`Tile_Maps.md`/`OAM.md`/`Rendering.md`/`Palettes.md`/
`OAM_DMA_Transfer.md`.

### Registers

| Addr | Name | Bits | Meaning |
|---|---|---|---|
| `0xFF40` | `LCDC` | 7 | LCD/PPU enable |
| | | 6 | Window tile map: 0=`0x9800`, 1=`0x9C00` |
| | | 5 | Window enable |
| | | 4 | BG/window tile data addressing: 0=`0x8800` (signed), 1=`0x8000` (unsigned) |
| | | 3 | BG tile map: 0=`0x9800`, 1=`0x9C00` |
| | | 2 | Object size: 0=8×8, 1=8×16 |
| | | 1 | Object enable |
| | | 0 | BG/window enable (DMG: also gates the window entirely) |
| `0xFF41` | `STAT` | 6 | LYC=LY interrupt select |
| | | 5/4/3 | Mode 2/1/0 interrupt select |
| | | 2 | LYC=LY flag (read-only, PPU-owned) |
| | | 1-0 | Current mode (read-only, PPU-owned; reads 0 while the LCD is off) |
| `0xFF42`/`0xFF43` | `SCY`/`SCX` | | Background scroll position |
| `0xFF44` | `LY` | | Current scanline (read-only, 0-153) |
| `0xFF45` | `LYC` | | `LY` compare target |
| `0xFF46` | `DMA` | | OAM DMA source page (see below) |
| `0xFF47` | `BGP` | | BG/window palette (4×2-bit shade mapping) |
| `0xFF48`/`0xFF49` | `OBP0`/`OBP1` | | Object palettes (same 4×2-bit shape; color index 0 is always transparent for objects, regardless of the palette's own mapping for that slot) |
| `0xFF4A`/`0xFF4B` | `WY`/`WX` | | Window position (`WX` is offset by 7 — window pixel 0 is screen column `WX-7`) |

Only bits 3-6 of `STAT` are genuinely writable — bits 0-2 (mode,
LYC==LY flag) stay PPU-owned regardless of what the CPU writes to them.

### Mode timing

Each of the 154 scanlines (144 visible + 10 V-Blank) takes 456 dots
(T-states) at 4.194304 MHz:

| Mode | Name | Dots | When |
|---|---|---|---|
| 2 | OAM scan | 80 | start of every visible scanline (0-143) |
| 3 | Drawing | 172-289 | after OAM scan |
| 0 | H-Blank | 376 - Mode 3's duration | after Drawing |
| 1 | V-Blank | 456 × 10 scanlines | after scanline 143, before scanline 0 |

Mode 3's real, variable length (172-289 dots) is implemented (Phase 8),
using pandocs' `Rendering.md` "Mode 3 length" algorithm rather than the
project's earlier fixed-172-dots simplification: `SCX & 7` dots
(scroll penalty), a flat 6-dot penalty when the window activates that
scanline, and a 6-11-dot penalty per object overlapping the scanline
(pandocs' own "OBJ penalty algorithm", including the tile-sharing and
OAM-X=0 special cases). Computed once per scanline, at the Mode 2→3
transition, from a snapshot of `LCDC`/`SCX`/`WY`/`WX`/OAM at that
instant — see `gb_ppu_step()`/`compute_mode3_length()` in `ppu.c`.

Rendering itself still happens once, all at once, at the moment Mode 3
ends — not progressively pixel-by-pixel the way real hardware's pixel
FIFO works (pandocs' `pixel_fifo.md`'s fetcher/FIFO push-pop state
machine). This remains a deliberate simplification: pandocs'
`Rendering.md` algorithm gives Mode 3's exact *duration* without needing
a full FIFO simulation, and duration (not literal per-pixel FIFO
mixing) is what STAT/OAM-scan interrupt timing depends on. Genuinely
obscure *mid-Mode-3* effects (a register write timed to land between
two specific pixels within one scanline, or `pixel_fifo.md`'s own
documented "WX changed mid-scanline" bug) still aren't modeled. See
[Implementation status](#implementation-status-1) for what this
Phase 8 fix did and didn't change about `dmg-acid2`'s own measured
match rate.

### Tile data and addressing

A tile is 8×8 pixels, 2 bits per pixel (4 shades), stored as 16 bytes —
each row is two bytes (low bit plane, then high bit plane), combined
per-pixel to form a 2-bit color index. Two addressing modes select
where a tile ID's actual bytes live in VRAM, chosen by `LCDC` bit 4:

- **`$8000` method** (`LCDC.4`=1): tile ID is unsigned, address =
  `0x8000 + id×16`. Covers tiles 0-255.
- **`$8800` method** (`LCDC.4`=0): tile ID is **signed**, address =
  `0x9000 + (signed)id×16`. Covers the same physical VRAM range
  (`0x8800`-`0x97FF` for IDs 0-127 map to `0x9000`-`0x97FF`; IDs
  128-255, read as negative, map to `0x8800`-`0x8FFF`) from a different
  base and with different sign handling.

**Objects (sprites) always use the `$8000` method**, regardless of
`LCDC.4` — a real, easy-to-miss hardware detail (pandocs'
`Tile_Data.md`), not a simplification: `LCDC.4` only affects how the
*background and window* interpret tile IDs.

### Tile maps

A 32×32 grid of tile IDs (one byte each), two of which exist in VRAM at
fixed addresses `0x9800` and `0x9C00`; `LCDC` bits 3 (background) and 6
(window) independently select which map each layer uses — they can
differ.

### Background and window

Both are built from the tile map + tile data machinery above. The
background wraps at 256×256 pixels, scrolled by `SCY`/`SCX`; the window
is a fixed-position overlay (not scrollable within itself) positioned
by `WY`/`WX`, visible on a scanline when `WY <= LY` and drawn per-pixel
when `x + 7 >= WX`. The window has its own internal line counter,
independent of `LY` — it only advances on scanlines where the window
was actually drawn (per pandocs' "Window Internal Line Counter" tip),
which matters for a window that's toggled on/off mid-frame.

`LCDC` bit 0 (DMG) gates the background *and* window together — when
clear, both render as literal white, not "whatever `BGP}` maps color 0
to."

### Objects (sprites)

40 total, 4 bytes each, in OAM (`0xFE00`-`0xFE9F`):

| Offset | Field |
|---|---|
| 0 | Y position + 16 (so Y=16 means "top of screen") |
| 1 | X position + 8 |
| 2 | Tile ID (for 8×16 objects, bit 0 is forced to 0 — the top tile is `id & 0xFE`, the bottom is `id \| 0x01`) |
| 3 | Attributes: bit 7 = BG/window priority, bit 6 = Y flip, bit 5 = X flip, bit 4 = palette (`OBP0`/`OBP1`) |

Per-scanline selection and priority (pandocs' `OAM.md`, non-CGB mode):

1. Scan OAM in index order, keep the first (up to) 10 objects whose
   vertical span overlaps the current scanline.
2. Among those, draw smallest-X first, ties broken by OAM index order.
3. **Object-vs-object priority is resolved before BG priority is
   considered**: once a higher-priority object claims a pixel column
   (opaque, regardless of whether it then loses to the background via
   its own bit-7 priority flag), a lower-priority object can never be
   drawn there — a real, easy-to-get-backwards ordering rule.
4. Color index 0 is always transparent for an object, regardless of
   what that palette slot would otherwise map to.
5. Bit 7 (BG/window priority): if set, background/window pixels with
   color index 1-3 (not 0) are drawn *on top of* the object instead.

### Palettes

Each of `BGP`/`OBP0`/`OBP1` maps the 4 raw 2-bit color indices to 4
displayed shades, 2 bits per slot: `shade = (palette >> (index*2)) &
0x03`. The shade values themselves (0=lightest..3=darkest) aren't
color — real DMG hardware renders them as 4 fixed shades of green/gray;
this emulator's own PPM/frame-dump path (`main.c`'s `--ppm`) maps them
to evenly-spaced grayscale for pixel-comparison purposes, not an
attempt at the real green tint.

### OAM DMA

Writing `DMA` (`0xFF46`) with a value *n* triggers copying 160 bytes
from `n×0x100` to OAM (`0xFE00`-`0xFE9F`) — the standard way real
software populates OAM each frame, since the CPU can't address OAM
during Modes 2/3 on real hardware anyway.

**Deliberate simplification**: this emulator performs the copy
**instantly**, not over the real ~160 M-cycle (640 T-state) transfer
window. Real programs universally busy-wait in HRAM (the one region a
DMA transfer doesn't block CPU access to) until the transfer completes
before touching OAM again — any program following that convention
produces the identical end result either way.

## APU (sound)

`gameboy/src/apu.c` implements all four sound channels, the DIV-APU
frame sequencer, and `NR50`/`NR51`/`NR52` mixing. Grounded against
pandocs' `Audio.md`/`Audio_Registers.md`/`Audio_details.md`.

### Channels

| Channel | Registers | Generates |
|---|---|---|
| CH1 | `NR10`-`NR14` (`0xFF10`-`0xFF14`) | Pulse wave with frequency sweep |
| CH2 | `NR21`-`NR24` (`0xFF16`-`0xFF19`) | Pulse wave, no sweep |
| CH3 | `NR30`-`NR34` (`0xFF1A`-`0xFF1E`) + Wave RAM (`0xFF30`-`0xFF3F`) | Arbitrary 32-sample waveform |
| CH4 | `NR41`-`NR44` (`0xFF20`-`0xFF23`) | Pseudo-random noise (LFSR) |

Each channel's `NRx4` register shares a common shape: bit 7 = trigger
(restart the channel), bit 6 = length-enable, bits 0-2 (CH1-3) = the
high 3 bits of an 11-bit period value (CH1/2/3 only).

### Pulse channels (CH1, CH2)

A 4-value duty table (12.5%/25%/50%/75% high time) selected by `NRx1`
bits 6-7, stepped once per period-timer expiry. The period itself is an
11-bit value split across `NRx3` (low 8 bits) and `NRx4` bits 0-2 (high
3 bits); a smaller period value means a higher pitch. CH1 additionally
has a frequency sweep unit (`NR10`): every 1-7 sweep-timer ticks (128
Hz base rate), the period is recalculated by shifting the *sweep's own
shadow copy* of the period (not `NR13`/`NR14` directly) right by
`NR10`'s shift amount and adding or subtracting it depending on `NR10`
bit 3 (direction) — overflowing past `0x7FF` disables the channel.

### Wave channel (CH3)

Plays back 32 4-bit samples from Wave RAM (`0xFF30`-`0xFF3F`, packed
two nibbles per byte) at a rate set by the same 11-bit period
convention as the pulse channels, output volume selected by `NR32`
(mute, 100%, 50%, or 25% — a right-shift, not a separate volume table).

### Noise channel (CH4)

A 15-bit linear feedback shift register (LFSR) — modeled as a genuine
16-bit register with bit 15 used as scratch space for the newly
computed feedback bit before the whole thing shifts right, since that
bit also gets mirrored into bit 7 (in 7-bit/"short" mode, `NR43` bit 3)
*before* the shift. `NR43` selects both the clock divisor (bits 0-2,
`{8,16,32,48,64,80,96,112} << shift`) and the shift amount (bits 4-7);
a shift of 14 or 15 "stops the channel from being clocked entirely," a
real documented edge case, not an oversight.

### Envelope and length timer

Every channel with a DAC (see below) has an envelope (`NRx2`: initial
volume, direction, pace) that steps at 64 Hz while `envelope_going`
(pace was nonzero at trigger) — stopping automatically once volume
hits 0 or 15. Every channel also has a length timer (reloaded from
`NRx1`'s low bits — 6 bits for CH1/2/4, 8 for CH3 via `NR31`) that,
when enabled (`NRx4` bit 6), ticks down at 256 Hz and disables the
channel at 0.

### DIV-APU frame sequencer

An 8-step, 512 Hz sequence, **tied to `DIV` bit 4's falling edge** (the
same real-hardware-counter approach the timer module uses for
`DIV`/`TIMA` — not a naive independent counter, which matters because
it means a program that speeds `DIV` up by writing it directly also
speeds up the frame sequencer, a real hardware coupling):

| Step | Clocks |
|---|---|
| 0, 2, 4, 6 | Length (256 Hz) |
| 2, 6 | Sweep (128 Hz) |
| 7 | Envelope (64 Hz) |

### DACs and mixing

Each channel's digital 0-15 (or 0-3 for CH3's shifted wave output)
output passes through a DAC before mixing: `analog = 1.0 -
digital/7.5` — note the **negative slope** (digital 0 maps to the
loudest positive analog value), an easy detail to get backwards. A
channel's DAC is considered "off" (and the channel force-disabled) when
its volume/direction register's relevant bits are all zero — `NRx2 &
0xF8 == 0` for CH1/2/4, `NR30` bit 7 clear for CH3.

`NR50` sets left/right master volume (0-7 each, +1 internally so 0
still produces sound, not silence); `NR51` selects which of the 4
channels route to which stereo side; `NR52` bit 7 is the APU's own
power switch — bits 0-3 report each channel's *own* enabled state
(read-only). Powering off clears every register except Wave RAM
(explicitly exempted per pandocs) and, on DMG specifically, each
channel's internal length-timer *countdown* (not its raw register
byte) — the countdown value itself survives a power cycle unclocked,
even though the register bits that would let you inspect it directly
get cleared. `NRx1` writes still reach that internal countdown even
while the APU is powered off (a real, if obscure, DMG behavior), while
every *other* register write is ignored while off.

The final mix passes through pandocs' own cited DMG high-pass filter
algorithm (a one-pole filter with a charge factor derived from the
output sample rate) before reaching the output buffer.

### Implementation status

Two genuinely obscure "Obscure Behavior" quirks (pandocs' own section)
around the length timer's interaction with the frame sequencer's exact
phase **are** implemented, since Blargg's own test ROMs use them as
their actual measurement technique: writing `NRx4` with a 0→1
length-enable transition on a frame-sequencer step that wouldn't itself
have clocked length immediately clocks it once early, and triggering a
channel under the same condition, when length is being reloaded from
zero, reloads to one below max instead of max.

**Deliberately not modeled** (documented as such in `apu.h`'s own
comment, not silently missing): wave-RAM trigger-time/mid-playback
corruption (accessing Wave RAM while CH3 is actively reading it doesn't
behave like a normal RAM access), exiting CH1's sweep negate mode
disabling the channel, and LFSR width-switch lockup. See
`gameboy/docs/GAMEBOY_ROADMAP.md`'s Phase 5 status for the exact
`dmg_sound` sub-test pass/fail breakdown these gaps correspond to (7 of
12 passing as of that phase).

**"Zombie mode" volume writes** (writing `NRx2` while a channel is
active can, on real hardware, alter the currently-playing volume
without a retrigger) *are* now modeled, added Phase 7 - but only the
one narrow case pandocs' own `Audio_details.md` confirms as reliable
across every real hardware unit tested including DMG (the fuller
CGB-02/04 algorithm it also documents is explicitly "crazy"/unreliable
on DMG, so implementing that would be guessing rather than grounding):
writing `NRx2` with the envelope in increase mode and a period of zero,
repeatedly, while the channel is already playing, increments its live
volume by 1 each write (wrapping mod 16). Found necessary by a real
ROM - see `gameboy/test_roms/droneboy/README.md` - whose own live volume
faders rely on exactly this technique, and regression-tested directly
(`gameboy/tests/test_apu.c`, `make gameboy-test`) rather than only
through that one ROM.

## Timer

`gameboy/src/timer.c` implements `DIV`/`TIMA`/`TMA`/`TAC`
(`0xFF04`-`0xFF07`) as a genuine free-running 16-bit **system counter**
— `DIV` is just that counter's visible upper byte, not a separately
incrementing register — with `TIMA` incrementing on a **falling edge**
of one specific counter bit, not a naive independent periodic counter.
Grounded against pandocs' `Timer_and_Divider_Registers.md`/
`Timer_Obscure_Behaviour.md`.

| Addr | Register | Notes |
|---|---|---|
| `0xFF04` | `DIV` | Upper 8 bits of the 16-bit system counter; any write resets the whole counter to 0 |
| `0xFF05` | `TIMA` | Increments on the selected bit's falling edge |
| `0xFF06` | `TMA` | Reload value on overflow |
| `0xFF07` | `TAC` | Bit 2 = enable; bits 0-1 select which counter bit `TIMA` watches |

`TAC`'s 2-bit clock-select field picks system-counter bit `9, 3, 5, 7`
(for values `0, 1, 2, 3` respectively) — derived directly from
pandocs' documented Hz rates, not an arbitrary table (and cross-checked
against the same values every other independent GB emulator
implementation uses).

Modeling this as a real falling-edge-triggered bit off a genuine
counter — rather than a simpler independent periodic tick — is what
makes two obscure, easy-to-get-wrong behaviors fall out for free
instead of needing special-case code:

- **Writing `DIV`** (or executing `STOP`, which resets the same
  counter) resets the whole 16-bit counter to 0. If the watched bit
  happened to be `1` right before the reset, that's a falling edge —
  `TIMA` gets a **spurious extra tick** it wouldn't have gotten from a
  naive "just zero a separate counter" model.
- **`TIMA` overflow** doesn't reload from `TMA` and request an
  interrupt immediately — there's a real one-M-cycle (4 T-state) delay
  during which `TIMA` reads `$00`, not `TMA`'s value yet, before the
  reload and interrupt request both happen together.
- **Disabling the timer via `TAC`** while the watched bit is currently
  `1` *also* causes a spurious tick, the same falling-edge logic as a
  `DIV` write — a DMG-specific quirk (some later hardware revisions fix
  this) that real software has been observed to depend on.

All three are covered by `gameboy/tests/test_timer.c` directly, independent
of any ROM (`make gameboy-test`).

## Joypad

`gameboy/src/joypad.c` implements `P1`/`JOYP` (`0xFF00`) — grounded
against pandocs' `Joypad_Input.md`. Two 4-button groups (action:
A/B/Select/Start; direction: Right/Left/Up/Down) are multiplexed onto
the same 4 read bits, selected by two CPU-written bits, with the real
Game Boy's inverted **`0` = pressed** polarity throughout.

| Bit | Meaning (write) | Meaning (read) |
|---|---|---|
| 5 | Select action buttons (`0`=selected) | (same, read back) |
| 4 | Select direction buttons (`0`=selected) | (same, read back) |
| 3-0 | — (read-only) | Button state for whichever group(s) are selected, `0`=pressed; both groups selected ANDs their states together |

No real input source exists yet in this project — a GUI front end is
still a Phase 7 item (see `gameboy/docs/GAMEBOY_ROADMAP.md`) — but the
register logic and interrupt-request behavior are real and tested:
`gb_joypad_set_action()`/`gb_joypad_set_direction()` are the API a
future front end or test harness calls, and correctly request a joypad
interrupt only on a high-to-low (release-to-press) transition of a
button whose group is *currently selected* — i.e., only when the
game could actually have observed the change, not on every keypress
regardless of what the game last wrote to `P1`.

## Interrupts

Five sources, `IE` (`0xFFFF`) and `IF` (`0xFF0F`) sharing the identical
bit layout, priority by bit order (bit 0 highest):

| Bit | Source | Vector | Requested by |
|---|---|---|---|
| 0 | V-Blank | `0x0040` | PPU, entering scanline 144 |
| 1 | LCD STAT | `0x0048` | PPU, per `STAT`'s own interrupt-select bits (LYC=LY, or entering Mode 0/1/2) |
| 2 | Timer | `0x0050` | Timer, on a `TIMA` overflow (after the reload delay) |
| 3 | Serial | `0x0058` | Not modeled — no real serial transfer completion exists in this emulator (see [Serial](#serial-stub)) |
| 4 | Joypad | `0x0060` | Joypad, on a visible button's release→press transition |

Dispatch mechanics (delay, `IME` interaction, the HALT bug's
interaction with pending-but-undispatched interrupts) belong to the
CPU side of this story — see `gameboy/docs/CPU_REFERENCE.md`'s [Interrupt
handling](CPU_REFERENCE.md#interrupt-handling) section for that half.

## Implementation status

The subsystems above are all real, working implementations backed by
Blargg's `cpu_instrs`/`instr_timing` (12/12 passing), `dmg-acid2`
(98.04% pixel match), and `dmg_sound` (7/12 passing) — see
`gameboy/docs/GAMEBOY_ROADMAP.md`'s Status section for the exact numbers,
root causes behind every bug found along the way, and the full list of
what's deliberately deferred (instant OAM DMA; the several
named-and-cited APU quirks; MBC3's RTC not advancing by wall-clock
time; no real joypad input source; no low-power STOP mode). PPU Mode
3's real, variable length is implemented (Phase 8) - `dmg-acid2`'s
match rate is unchanged at 98.04% by that fix, since the specific rows
still mismatching (LY=0, LY=133-141) turn out to carry zero SCX/window/
object penalty either way; the remaining pixel-FIFO-shaped gap (full
per-dot simulation, not just accurate Mode 3 duration) is still
deliberately deferred. Nothing in this document describes aspirational
or planned behavior — everything above either works as described today
or is explicitly flagged as not yet modeled.
