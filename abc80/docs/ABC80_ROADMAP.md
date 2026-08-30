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

Milestones 1-13 are all done. Their full write-ups — including the dead
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
| 13 | An automated regression suite | `abc80/tests/run_tests.sh`, 17 checks, part of `make test` |

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

## Known gaps / not modeled

Only what is *absent* belongs here. Every milestone is complete, and the
narrative of what each one established lives in
[`ABC80_COMPLETED.md`](ABC80_COMPLETED.md) rather than being recited
again as a list of things that are no longer missing.

- **Cassette is a host-file bypass**, not analog tape.
  `--quickload`/`--quicksave` read and write BASIC's own program-storage
  pointers directly; no signal is modulated, and real `.wav` tape audio
  cannot be loaded. Adequate for moving programs in and out, and unlikely
  to be worth more unless something needs real tape timing.
- **One floppy drive.** The shared card supports eight units and ABC-DOS
  scans all of them at boot, but `--disk` takes a single image. See
  Planned next steps.
- **No printer or IEC-bus ROM card.** `0x8000`-`0xBFFF` floats; only the
  DOS card at `0x6000` is modeled, and only because `--disk` loads a real
  image into it. No ROM image for the others is committed.
- **ROM is writable.** `0x0000`-`0x3FFF` accepts stores, matching this
  repo's flat-memory-model precedent for the CP/M target rather than
  introducing an abstraction early. Revisit only if something concrete
  needs it.
- **The GTK app's pixel decode is only checked coarsely.**
  `bin/abc80-gtk --screenshot` now gives it two automated checks
  (`gtk-headless-boot`, `gtk-headless-type`), but they count lit pixels
  rather than compare an image — a committed reference PNG would be
  hostage to the host's Cairo version. That catches a decode that stops
  drawing or draws from the wrong place; it would not catch a subtly wrong
  glyph. The ABC802 and ABC806 windows do not need this because their
  decode is a pure function with its own ASCII-art fixture; this one
  carries its own, since the CLI renders Unicode block glyphs instead.
- **Non-interactive cursor blink is a fixed snapshot.** The end-of-run
  render hardcodes `blink_phase=1`; only `--interactive` computes it from
  elapsed time. A deliberate difference between the two modes' purposes.

### Performance

`bin/abc80` runs at roughly **1.7M instructions/sec** (`-O2`, Apple
silicon), several times slower than `bin/abc802` on the same shared core.
Nothing depends on it — the machine is a 3 MHz Z80 and `--interactive`
paces itself comfortably — but it sets the cost of the regression suite
(about 20 seconds, against the ABC802's 3) and would matter to anything
wanting to run long workloads. Not investigated; the per-instruction
video-timing work is the obvious first suspect.

## Testing

`make test-abc80` (also part of `make test`) runs
[`tests/run_tests.sh`](../tests/run_tests.sh): boot and BASIC, the
Swedish charset round trip, both memory-map configurations, the
floating bus, the ABC-bus/sound port decode boundary, a cassette
`--quicksave`/`--quickload` round trip across two processes, the video
timing PROMs, a chargen fixture diff, the SN76477 tone measured by zero
crossings, and that same register driven from BASIC through the CPU (the
only coverage `step.c`'s `OUT`-instruction decoding has). Five floppy checks — boot, the card's status byte read
from BASIC, the real `LIB` directory listing, a `SAVE`/`LOAD` round trip,
and UFD-DOS over the same card — need `ABC80_TEST_DISKS` pointed at a
directory holding `disk003.img`, and skip loudly without it.

## Planned next steps

None committed. Candidates, in rough order of how much they would add:

- **A second drive.** The card supports eight units and the ABC802 target
  already exposes them (`--disk` repeated, `N:FILE` to pin one). ABC-DOS
  scans all eight at boot — visible in `ABCBUS_TRACE=1` output, which
  walks units 0-7 reading directory sectors 16-23 on each — so this is
  mostly CLI plumbing. Testable: two distinct real images exist in the
  abc80.net archive set this project already uses (`disk001.img` and
  `disk003.img`; `disk002.img` is byte-identical to `disk001.img`).
- **A UFD-DOS-formatted disk image.** `--dos-rom UFD80V20.bin` drives the
  card correctly (Milestone 12) but has only ever been pointed at
  ABC-DOS media, which it reads fine and then correctly reports has no
  startup file on it. Real UFD-DOS media would turn that from "the bus
  works" into "the DOS works".
- **Emulator throughput**, if anything ever needs it — see Performance
  above.
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
