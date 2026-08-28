# ABC80 Roadmap

A second machine target sharing this repo's proven, ZEXALL/ZEXDOC-clean Z80
core (`z80core/z80.c`/`alu.c`) rather than a from-scratch CPU — see
`CLAUDE.md`'s top-level structure section for why this lives here instead of
as a separate repo (unlike the old `gameboy/` subproject, which deliberately
shared no code with this one).

The Luxor ABC80 (Dataindustrier AB / DIAB, manufactured by Luxor AB, Motala,
Sweden, 1978): Z80A @ ~2.9952 MHz, 16 KB BASIC ROM, 16–32 KB RAM, 40×24
monochrome text display, cassette storage standard, ABCbus expansion for
disk/RAM/other peripherals.

## Completed work

Milestones 1-12 are all done. Their full write-ups — including the dead
ends and the hardware facts each one had to establish — live in
[`ABC80_COMPLETED.md`](ABC80_COMPLETED.md).

| # | Milestone | Outcome |
|---|---|---|
| 1 | Boots the real ROM on the shared core | real BASIC ROM runs on `z80core/` |
| 2 | Video generation | real video RAM rendered, PROM-grounded timing |
| 3 | Z80 PIO + keyboard input | real scanned-matrix keyboard with debounce |
| 4 | Cassette storage | `--quickload`/`--quicksave` round trips |
| 5 | SN76477 sound (scoped) | full SLF/noise/one-shot/envelope model, `--wav` |
| 6 | ABCbus expansion | `--ram32k`, plus `--disk` doing real floppy `SAVE`/`LOAD` (via a DOS-ROM trap, later retired — see 12) |
| 7 | Periodic PIO interrupt | the real scanline-driven timer interrupt |
| 8 | `--interactive` | real keyboard input and a live, real-time-paced screen |
| 9 | Real Ctrl-C break | `Ctrl-C` reaches BASIC rather than killing the emulator |
| 10 | Left/right arrow keys | mapped from the ROM's own line editor, by disassembly |
| 11 | A real GTK window | `bin/abc80-gtk`, Cairo framebuffer with live SN76477 audio |
| 12 | Retiring the PC-address trap | a real ABC-bus card (`abcbus/disk.c`, shared with the ABC802) replaces the DOS-ROM trap |

## Memory map (grounded, not guessed)

Cross-checked between MAME's current mainline driver
(`src/mame/luxor/abc80.cpp`) and an independent Swedish ABC80 hobbyist-forum
mention of the screen memory address (31744–32767 decimal =
`0x7C00`–`0x7FFF`, matching MAME exactly) — this resolved a discrepancy
found against an older/outdated MESS driver fork, which is why both sources
are recorded here rather than trusting either alone.

| Range | Contents |
|---|---|
| `0x0000`-`0x3FFF` | ROM: four 4Kx8 chips — `3506_3.a5`/`3507_3.a3`/`3508_3.a4`/`3509_3.a2` at `0x0000`/`0x1000`/`0x2000`/`0x3000`. CRC32 `e2afbf48`/`d224412a`/`1502ba5b`/`bc8860b7`. |
| `0x4000`-`0xBFFF` | External ABCbus expansion, minus the video RAM carve-out below. `0x4000`-`0x7BFF`: floating (`0xFF`), except `0x6000`-`0x6FFF` when `--disk` loads a real DOS ROM there; no printer/IEC ROM card modeled. `0x8000`-`0xBFFF`: floating (`0xFF`) by default, or real RAM with `--ram32k` (the real 16KB expansion mod — see Milestone 6). |
| `0x7C00`-`0x7FFF` | Video RAM (1KB — 40×24 char cells). |
| `0xC000`-`0xFFFF` | Onboard RAM (16KB base configuration). |

I/O ports (global mask `0x17`, i.e. ports repeat every `0x18`):

| Port(s) | Function |
|---|---|
| `0x00`-`0x01` | ABCbus data/status |
| `0x02`-`0x05` | ABCbus control lines C1-C4 |
| `0x06` | SN76477 sound chip |
| `0x07` | ABCbus reset |
| `0x10`-`0x13` (mirrored `0x14`-`0x17`) | Z80 PIO |

The ABCbus ports are a real device, reached through the CPU core's
`io_in_hook`/`io_out_hook` (installed by `abc80/emu/src/abcbus.c`); every
other port is the core's flat `io_ports[]` array as before.

## Known gaps / near-term technical debt

Every milestone is complete, so this section is now where the remaining
work actually lives — a quick-scan summary of what is *not* modeled, with
the finished write-ups it refers back to in
[`ABC80_COMPLETED.md`](ABC80_COMPLETED.md):

- Video generation (Milestone 2), keyboard input (Milestone 3), cassette
  quickload/quicksave (Milestone 4), a scoped SN76477 tone model
  (Milestone 5), RAM expansion / floating-bus fidelity (Milestone 6's first
  sub-step), the real periodic PIO interrupt (Milestone 7), real
  interactive keyboard input with a live, real-time-paced screen including
  a genuine Ctrl-C break to BASIC and real left/right arrow keys
  (Milestones 8-10, `bin/abc80 --interactive`) are done. Floppy/DOS
  support is done too, and **is now a real ABC-bus card rather than a
  bypass** (Milestone 12): `--disk` fits the shared synthetic controller
  in `abcbus/disk.c` and the DOS ROM's own protocol code executes for
  real, which retired the PC-address trap on `0x6068`/`0x60A1` that
  Milestone 6 built. `SAVE`/`LOAD` round trips are byte-identical to what
  the trap produced; the real `LIB` utility's directory listing, which
  failed under the trap, now works; and `UFD-DOS` (the alternate real DOS
  ROM, `UFD80V20.bin`, examined during Milestone 6 and left unwired
  because a trap cannot serve a second DOS) drives the same card
  correctly via `--dos-rom`. Disk-full behavior (both the
  directory-capacity and block-space-exhaustion cases) is confirmed
  correct and safe. The exact meaning of `B`'s unused bits was a
  trap-era question and no longer arises — the ROM's own code reads
  those registers now.
  Cassette storage is a host-file bypass of BASIC's own program-storage
  pointers, not real analog tape emulation. Sound was originally a single
  steady-tone case only, but Milestone 11's "full SN76477 emulation"
  sub-step closed that gap: SLF/noise/one-shot/envelope modes are all now
  synthesized, and live audio (via GTK/SDL, not just `--wav` rendering) is
  supported too — see that sub-step's own write-up.
- **Cursor blink is now live** (Milestone 8) in `--interactive` mode,
  computed from real elapsed time against the real 3.125Hz rate MAME's own
  `m_blink_timer` uses. Default (non-`--interactive`) mode is still a
  one-shot end-of-run snapshot with `blink_phase=1` hardcoded, unchanged -
  a deliberate difference between the two modes' purposes, not a gap.
- **Real Ctrl-C break now reaches BASIC** (Milestone 9, closing the gap
  Milestone 8 left open) - see that milestone's own write-up in
  `ABC80_COMPLETED.md`.
- **Memory-map fidelity for `0x4000`-`0xBFFF`**: fixed by Milestone 6's RAM
  expansion sub-step (see `ABC80_COMPLETED.md`) — `0x4000`-`0x7BFF` and, by default,
  `0x8000`-`0xBFFF` now correctly float (fixed `0xFF` reads, matching MAME's
  own no-card `abcbus_slot_device` behavior) instead of being ordinary flat
  RAM, except for `0x6000`-`0x6FFF` when `--disk` is active (the real DOS
  ROM, either of the two committed images). `0x8000`-`0xBFFF` still has no
  real printer/IEC ROM card content — out of scope, no milestone currently
  targets those cards.
- **ROM write-protection**: `0x0000`-`0x3FFF` is writable in this model,
  matching this repo's existing flat-memory-model precedent for the CP/M
  target (`CLAUDE.md`'s Architecture section) rather than a new abstraction
  introduced early. No milestone yet — revisit only if something concrete
  needs it, same standard `cpm/docs/ROADMAP.md` applies elsewhere.

## Planned next steps

None committed. Candidates, in rough order of how much they would add:

- **A second drive.** The card supports eight units and the ABC802 target
  already exposes them (`--disk` repeated, `N:FILE` to pin one). ABC-DOS
  scans all eight at boot, so `DR1:` should work with only the CLI
  plumbing. Untested — no second real ABC80 image is in hand.
- **A UFD-DOS-formatted disk image.** `--dos-rom UFD80V20.bin` drives the
  card correctly (Milestone 12) but has only ever been pointed at
  ABC-DOS media, which it reads fine and then correctly reports has no
  startup file on it. Real UFD-DOS media would turn that from "the bus
  works" into "the DOS works".
- **Printer/IEC ROM cards** in the rest of the expansion range — no image
  committed, no milestone.

## Sources consulted

- MAME mainline driver: `src/mame/luxor/abc80.cpp` (memory map, I/O map,
  ROM filenames/CRC32 checksums, machine configuration), fetched from
  <https://raw.githubusercontent.com/mamedev/mame/master/src/mame/luxor/abc80.cpp>.
- MAME's `SN76477` sound device: `src/devices/sound/sn76477.cpp`/`.h`
  (per-sample RC-integrator formulas for the VCO/SLF/noise/one-shot/
  attack-decay subsystems, and the measured voltage-threshold constants
  they're built on), fetched from
  <https://raw.githubusercontent.com/mamedev/mame/master/src/devices/sound/sn76477.cpp>
  and the equivalent `.h` path — used to derive the real SLF/noise/
  one-shot/envelope timing values in `ABC80_REFERENCE.md`'s Sound
  section beyond the VCO's own (previously the only one grounded).
- *Mikrodatorns ABC* (Gunnar Markesjö) — block diagrams and partial circuit
  schematics covering most of the machine; full text at
  <https://archive.org/stream/microdatorns_abc/microdatorns_abc_djvu.txt>.
- ABC80 service manual, PC/M Personal Computer Museum archive:
  <https://www.abc80.net/archive/luxor/ABC80/ABC80-servicemanual.pdf>.
- ROM images: <https://www.abc80.net/archive/luxor/Prom/fw/ABC80/> — see
  `abc80/resources/rom/README.md` for the exact files and checksum
  cross-check against MAME.
- Christer Ekman, "Bygg ut din ABC 80 till 32K RAM," *Mikrodatorn* nr 7,
  1982 — <https://www.abc80.net/archive/luxor/ABC80/ABC80-32K-mod-Mikrodatorn.pdf>
  (Milestone 6's RAM-expansion sub-step).
- *Bruksanvisning Minneskort ABC* (Luxor) —
  <https://www.abc80.net/archive/luxor/ABC80/ABC80-minneskort-bruksanvisning.pdf>
  (the separate ROM/DOS expansion card, Milestone 6).
- Scandia Metric AB, *Kort beskrivning av ABC-80 BASIC* —
  <https://www.abc80.net/archive/luxor/ABC80/Kort-beskrivning-av-abc80-basic.pdf>
  (the primary source for `abc80/docs/ABC80_BASIC_REFERENCE.md`).
- Real ABC80 floppy disk images: <https://www.abc80.net/archive/luxor/sw/disk_images/ABC80/160k/>
  (14 real, dumped 160KB disk images with scanned labels/descriptions in
  that directory's own `index.txt` — corrected from this project's own
  earlier, mistaken count of 49; `disk003.img`, "System.diskett ABC80
  Ver. 2.1," used as real ground truth for Milestone 6's floppy/DOS
  bypass sub-step, not yet committed into this repo).
- sasq64/abc80sim, a real, independent open-source ABC80 emulator with
  working floppy support — <https://github.com/sasq64/abc80sim> (`src/
  disk.c`, `src/abcio.c`): source for the real ABC830/"mo" controller
  sector-addressing formula this project's own bypass had wrong (see
  Milestone 6's own write-up) — pointed at by the user rather than found
  independently.
- andersrcarlsson-stack/abc80-pico-public, a real ABC80 emulator running
  on Raspberry Pi Pico hardware — <https://github.com/andersrcarlsson-stack/abc80-pico-public>
  (`src/disk_controller.cpp`): source for the real sector-interleave
  mapping (`phys_sector()`, interleave factor 7) that turned out to be
  Milestone 6's actual remaining root cause — pointed at by the user
  rather than found independently.
- Torfinn Ingolfsen's `abc80sim` notes page —
  <https://tingo.homedns.org/emulators/abc80sim/> (not reachable
  directly from this environment; read via its Wayback Machine archive,
  <http://web.archive.org/web/20250622044221/http://tingo.homedns.org/emulators/abc80sim/>)
  — checked but turned out to contain only build-environment
  troubleshooting logs, no protocol detail; a dead end, noted here for
  completeness rather than silently omitted.

See `abc80/docs/ABC80_REFERENCE.md` for a consolidated hardware reference
(memory map, I/O ports, ROM/PROM inventory, per-subsystem register layouts)
pulled from all of the above plus this project's own code comments — this
section only lists sources, not the facts derived from them.
