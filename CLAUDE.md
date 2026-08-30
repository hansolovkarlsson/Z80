# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project

A Z80 CPU emulator written in C, built to run CP/M-80 programs. Phase 1 (a
complete Z80 core passing ZEXALL/ZEXDOC cleanly, including I/O ports and
interrupt delivery) is done; Phase 2 (a Z80 assembler, including
macros/`include`, plus a disassembler) is done too; Phase 3 (CP/M
BDOS/BIOS) is far along — see `cpm/docs/ROADMAP.md` for exact status and
its Known Gaps section for what's left. This directory is a git
repository (initialized after the first working ZEXALL/ZEXDOC pass).
The shared Z80 core itself (`z80.c`/`alu.c`/their headers) lives in
`z80core/` at the true repo root, not under either machine target's own
directory — it's genuinely machine-agnostic (`z80_execute()`, not the
CP/M-specific `z80_step()` covered below) and used by two independent
targets, so it isn't owned by either one. `abcbus/` (`disk.c`/`disk.h`,
the synthetic ABC-bus floppy controller, plus `mkdisk.c` → `bin/abcdisk`,
which *creates* formatted blank images and lists what is on one — the
on-disk format was derived by inspecting real Luxor media and confirmed
by a `SAVE`/`LOAD` round trip through the real ROM on both drive types,
and it matters because the machine's own formatter, `DOSGEN`, is a program
on a Luxor system disk rather than anything in either ROM — so on real
hardware you need a working disk to make a disk, and before this tool the
only writable media here was media you already had) is at the repo root on
exactly those terms and for exactly that reason: the ABC bus is a bus, not a
machine, and both `abc80/` and `abc802/` drive the same card with the same
four-byte command header and the same status bits. It started life under
`abc802/emu/src/`, and was moved out the moment the ABC80 target began
using it rather than letting `abc802/` become a de facto shared library —
the same mistake moving `z80core/` out of `cpm/` had already corrected
once. `asm/` (the `z80asm`
assembler) and `disasm/` (the `z80dasm` disassembler) live at the true
repo root for the identical reason: neither has any real CP/M
dependency (confirmed by inspection, not just asserted — their `src/`
has no BDOS/CP/M-specific code), and `disasm/` in particular is already
in active, independent use disassembling `abc80/`'s own ROM images (see
`abc80/docs/ABC80_ROADMAP.md`), the same second-consumer fact that
justified `z80core/`'s own move below. Their `examples/`/`test/`
subdirectories do stay CP/M-flavored in *content* (real `.asm` programs
using `BDOS: equ 5`/`CALL 5`/`org 100h`, run through `bin/z80`) since
that's this repo's only available Z80 execution environment — that's
not a reason to relocate the tool, just a fact about how its regression
suite happens to be exercised. `cpm/` holds the CP/M-specific emulator
layer, GTK launcher, sample disk, and third-party reference
software/docs; `bin/` (the build output) and `scripts/` (general-purpose
tooling) stay at the true repo root too. This repo previously also held
a standalone Game Boy emulator as a
separate, code-sharing-free subproject under `gameboy/`; it was split
out into its own repository (via `git subtree split`, preserving its
real commit history) once real end-to-end functionality made clear the
two would never actually share code - see that project's own
`docs/GAMEBOY_ROADMAP.md` (in its new repo) for the full history if
relevant. `abc80/` is a second, newer machine target - the Luxor ABC80,
a real Z80-based Swedish home computer from 1978 - and unlike `gameboy/`
it *does* share code: its `abc80/emu/src/main.c` links directly against
`z80core/z80.o`/`alu.o`, the same proven core `bin/z80` uses, so it
stays in this repo rather than being a future split candidate. (Before
this sharing was made explicit, `z80core/`'s files lived under
`cpm/emu/src/` and `abc80/`'s own build reached into that directory
directly - moved out to a shared, top-level location instead, so the
two-consumer relationship doesn't require `cpm/`'s own directory to be
treated as a de facto shared-library location.) See
`abc80/docs/ABC80_ROADMAP.md` for its status and known gaps,
`abc80/docs/ABC80_REFERENCE.md` for a consolidated hardware reference
(memory map, I/O ports, ROM/PROM inventory, per-subsystem register
layouts), and `abc80/docs/ABC80_BASIC_REFERENCE.md` for the BASIC
language itself (commands, statements, functions, operators — the
language `bin/abc80` actually runs, not the `z80asm` syntax
`docs/ASSEMBLER.md` covers) — as of Milestone 8, it boots the real BASIC ROM images
(committed under `abc80/resources/rom/`) with working video, keyboard
input (including genuine real-time interactive keyboard input and a live,
real-time-paced screen via `bin/abc80 --interactive`, not just scripted/
piped input), cassette quickload/quicksave, a scoped SN76477 sound model,
a real periodic PIO interrupt, a correctly-modeled (floating-bus-by-
default, optional `--ram32k`) memory map, and `--disk` support giving
genuine floppy `SAVE`/`LOAD` round trips against real ABC80 disk images
through a real, unmodified DOS ROM. That last one was a *PC-address trap*
on two DOS ROM routines from Milestone 6 until **Milestone 12 retired it**
in favour of the shared `abcbus/` card, so the ROM's own bus protocol code
now executes for real; `abc80/emu/src/abcbus.c` keeps only what is
machine-specific (loading the DOS ROM into the expansion window at
`0x6000`, and this machine's own `0x17`-masked port decode, installed via
the core's `io_in_hook`/`io_out_hook`). Two things that buys: the real
`LIB` utility's directory listing works, where the trap gave `Diskfel`,
and `--dos-rom UFD80V20.bin` — a *different* real DOS, never trapped and
never analysed routine-by-routine — drives the same card correctly.

`abc802/` is a *third* machine target - the Luxor ABC802 (1983), a later
member of the ABC800 family - added on exactly the terms `abc80/`
established: it links the same `z80core/z80.o`/`alu.o` rather than
duplicating them. It is not an ABC80 variant but a genuinely different
machine: a programmable MC6845 CRTC instead of fixed video timing, three
Z80 peripheral chips (CTC/SIO/DART) on an IM 2 daisy chain instead of a
single periodic PIO interrupt, a 32K ROM that *overlays* the low half of
a full 64K of RAM, and a serial keyboard rather than a scanned matrix.
See `abc802/docs/ABC802_ROADMAP.md` for status and known gaps,
`abc802/docs/ABC802_REFERENCE.md` for the hardware reference, and
`abc802/docs/ABC802_BASIC_REFERENCE.md` for the BASIC II language itself
(commands, statements, functions, the disk devices and how a disk image
is laid out) — the ABC800-family dialect, a substantially richer language
than ABC80's, whose keyword tables that document reads out of the ROM
images directly rather than transcribing a manual. As of
Milestone 1 it boots the real, unmodified BASIC II ROM images (committed
under `abc802/resources/rom/`, every one verified byte-for-byte against
MAME's published CRC32 *and* SHA1) to a working prompt: `bin/abc802
--columns 80 --type "PRINT 6*7"` renders the ROM's own sign-on banner,
its echo of the typed line, and its answer, `42`. Milestone 2 adds
`--interactive`: a genuine live session (raw-terminal keyboard, real
3 MHz pacing, a screen redrawn at 30fps with inverse video and a real
cursor), on the same terms `bin/abc80 --interactive` established. Two
things there are worth knowing. Live input is deliberately paced with the
*same* ~0.1s inter-key gap `--type` uses, because the DART holds one
receive byte and a pipe or paste would otherwise overwrite the whole line
away — but that gap is skipped mid-sequence, since a UTF-8 lead byte's
continuation must arrive inside a much shorter real-time timeout. And the
cursor blink needs no constant like ABC80's `ABC80_BLINK_HZ`: this ROM
blinks in *software*, toggling MC6845 R10 between `0x09` and `0x29` from
its own 93.75 Hz clock interrupt, so honoring R10 and pacing execution is
the entire implementation.

Milestone 3 adds real pixel rendering (`abc802/emu/src/chargen.c`, plus a
hand-written PNG writer in `png.c` using DEFLATE stored blocks, on the
same "this build has no third-party libraries" grounds as `abc80`'s own
WAV writer): `bin/abc802 --screenshot FILE` writes the 480x240 screen in
the machine's real amber phosphor. The decode is worth reading before
touching anything video-related, because it is not guessable — **the
character generator ROM's own output byte decides whether a cell is a
character or an attribute command.** Bit 7 (ATE) set means the byte is not
pixel data but an instruction: bit 6 (ATD) is the value, bits 1:0 pick
Row Graphic / Row Flash / Row Clear. So the *font* defines which character
codes are attribute codes (here `0x01`-`0x09` and `0x11`-`0x18`), and all
three attributes work by substituting the scanline address rather than
post-processing pixels — Row Graphic ORs `0x800` into the ROM address to
select a mosaic font, flash/clear force scanline `0x0E` (blank), and the
cursor forces `0x0F`, which is a solid bar, so the real cursor *replaces*
the glyph rather than inverting it. `abc802_render_pixels()` is
deliberately pure (everything arrives in an `Abc802Screen` struct), which
is what lets `bin/abc802-chargen-dump` verify it with no CPU core at all —
and that tool is not optional cover: the ROM's boot screen uses none of
the three attributes, so `--screenshot` would render it perfectly with the
attribute state machine completely broken.

Milestone 5 adds **real disk storage**: `--disk FILE` (on both
`bin/abc802` and `bin/abc802-gtk`) attaches a 160KB ABC830 floppy image,
boots real ABC800-family software off it, and round-trips BASIC
`SAVE`/`LOAD`. This is a real device model (`abcbus/disk.c`, since ABC80
Milestone 12 shared with that target): the six ABC-bus ports and the
controller's own command state machine, serving 256-byte sectors. Building
it was forced rather than chosen — this DOS ROM's bus driver is generic
and its callers issue *sequences* of bus commands per logical operation,
so there is no sector-level routine to trap the way the ABC80's ROM
allowed. Three facts to know before touching it.
**A status byte of `0x00` or `0xFF` means "no device"** — the ROM's poll
loop at `0x6196` aborts on either (`INC A / JR Z`, `DEC A / JR Z`), which
is why the previous "every ABC-bus read returns `0xFF`" behavior read
correctly as no card fitted. **Status bit 3 means "this command has not
failed", not "error"** — its exact complement, so an idle, healthy
controller is `0x89` and a failed one `0x81`, with the failure itself
reported through the auxiliary status byte on the INP port. This machine's
ROM never reads bit 3 at all, so it was modeled backwards on no evidence
until the ABC80's ROM — which reads it twice, and refuses to transfer a
byte or accept a completed write without it — exposed it. And **the
sector interleave (factor 7) is required**, now proven by experiment on
this machine as well as ABC80: with it disabled, real media stops booting
entirely. That looked like it settled a contradiction with abc80sim, which
ships with interleave compiled out — but the real answer is that **the
factor belongs to the dump, not the drive**: abc80.net's `.img` archive
stores sectors physically and needs factor 7, while `.dsk` images of the
same media are in logical order and need none, and nothing inside an image
says which. Both are right for their own dumps. `--interleave N` (on
`bin/abc802` and `bin/abc80`, overriding the drive default via
`abcbus_disk_set_interleave()`) exists for that; the wrong choice is easy
to misread, because the card is found and the directory lists correctly
while every real file read gives `Error 37`. Milestone 6 adds the 640KB ABC832/834 (`MF`) drive alongside the 160KB
ABC830 (`MO`), with the controller type chosen from the image's size
rather than a flag. **The two drives interleave in opposite directions** —
`MO` needs factor 7 and `MF` needs none, each established by booting real
media both ways — so interleave cannot be inferred for the `SF`/`HD` types
if those are ever added. Note the trap: both formats keep their directory
at sector 16, which reads correctly under *either* mapping because
track-boundary sectors map to themselves, so a readable hex dump proves
nothing and only booting settles it.

Milestone 7 makes `--disk` repeatable (drives 0, 1, …, or `N:FILE` to pin
one), so `MO1:`/`MF1:` work; all drives must share one type, since one
controller is modeled.

Milestone 8 settled the line editor by sweeping every control code rather
than disassembling it (that routine is only entered indirectly, so
`bin/z80dasm` renders it as `DB` bytes). Its entire vocabulary is
backspace `0x08`, discard-line `0x18`, clear-screen `0x0C`, and the
terminators `0x03`/`0x0A`/`0x0D` — **no cursor movement at all**, unlike
the ABC80's editor, which has a non-destructive cursor-right at `0x09`.
Left arrow therefore maps to `0x08` and Right is deliberately dropped;
that is hardware, not a missing feature.

Milestone 9 replaced the SIO stub with a real register model. The reason
it mattered is not the RS-232 port: **two configuration DIP switches reach
the ROM through SIO channel B's modem-status inputs** (S1 on DCD, S2 on
CTS), so a stub returning a constant was asserting a machine configuration
rather than staying neutral. An idle channel B now reads `0x24`. Channel A
is the second RS-232 port and channel B is the cassette; neither has a
device attached, so the SIO still raises no interrupts. Note that ABC802
BASIC has `INP()`/`OUT`, which makes the emulated machine its own best
test harness for port-level work — that is how the SIO was verified.

`--type-at N` exists for disk work specifically:
the ROM reports the keyboard ready long before a booting program is
listening, and discards anything typed meanwhile.

Milestone 4 adds `bin/abc802-gtk` (`abc802/gtk/`, opt-in via `make
abc802-gtk`) — a real Cairo pixel framebuffer like `bin/abc80-gtk`, not a
VTE launcher like `cpm/gtk/`, and needing only `gtk4` (no SDL2 and no
threads at all, since this machine's only sound is a strobe the emulator
doesn't sound). It is far shorter than ABC80's equivalent because that
decode is already shared and verified; `abc802_step()`
(`abc802/emu/src/step.h`) was extracted at the same time so the CLI's
`--interactive` loop and the window share the per-instruction logic, the
same move ABC80's Milestone 11 made. The thing worth copying from it is
`--screenshot`: it opens no window and never creates a `GtkApplication`,
but renders through the *identical* `draw_screen()` the live window uses,
against an offscreen surface. That exists because automating
`screencapture` against the user's real desktop steals focus and switches
Spaces (see `abc80/gtk/README.md`), so this app was built to verify itself
instead — and it immediately earned its keep, catching that `--type` fed
its argument to the keyboard as raw bytes, so `PRINT "ÅÄÖ"` reached BASIC
as UTF-8 and errored, while the interactive paths decoded the same text
correctly. `abc802_utf8_to_chars()` (`render.c`) is now the one converter
both use.

Two ABC802-specific implementation facts are worth knowing before
touching that target, because neither is guessable from a memory map.
First, **the character RAM at `0x7800-0x7FFF` is decoded by the M1
line**, not by a bank register: an opcode fetch there reads ROM while a
data read reads character RAM, and the ROM genuinely has code in that
window (its keyboard-input routines). `abc802/emu/src/memory.c`
reproduces this *without* touching the shared core, because z80core's
`fetch_byte()` indexes the flat `ram` array directly and deliberately
bypasses `bus_read_hook` - so the currently-selected 32K is kept
physically resident in that array (as both other targets already do with
their ROM) and `abc802_note_instruction_fetch()` is called from the step
loop with each instruction's own PC, which is exactly the information
MAME's `m1_r` latches, at the same granularity. Routing `fetch_byte()`
through `z80_read_byte()` instead was considered and rejected: it would
change the instruction-fetch path of a core two working targets already
depend on, in particular ABC80's floating-bus hook. Second, **the I/O
port mirrors are load-bearing** - the real boot ROM writes the CTC's
interrupt vector to port `0x64`, not `0x60`, so decoding only the
literal documented ranges silently drops it.

Three additions to the shared core came out of that work, all additive
with NULL/zero defaults so CP/M and ABC80 are unaffected:
`bus_write_hook` (the symmetric counterpart to the existing
`bus_read_hook`, needed because ABC802's ROM and RAM genuinely coexist at
the same addresses, so a stray write is real corruption rather than the
harmlessly-ignored store ABC80's read-only hook could tolerate), and
`io_in_hook`/`io_out_hook` (ABC802's ports are real chips whose reads
depend on internal state, not on what was last written to a
256-entry array). The **block I/O instruction group**
(`INI`/`INIR`/`IND`/`INDR`, `OUTI`/`OTIR`/`OUTD`/`OTDR`, in
`z80core/alu.c`) was also genuinely missing until the ABC802 ROM drove
its DART and CTC through `OTIR`/`OUTI` - ZEXALL/ZEXDOC exercise no I/O at
all and could never have caught it, the same blind spot
`asm/examples/gaps_test.asm` exists to cover; its check 7 is the
permanent regression test.

The Makefile now tracks header dependencies (`-MMD -MP` plus a trailing
`-include`). This is not housekeeping: adding fields to `struct Z80` left
every already-built object compiled against the old, smaller struct while
`z80.o` used the new one, so `Z80 cpu = {0}` under-allocated and *every*
CP/M program silently produced no output at all - a failure a plain
`make clean` hides, which is exactly what makes it worth tracking.

`abc806/` is a *fourth* machine target - the Luxor ABC806 (1983), the top
of the ABC800 family - added on the terms the other two established: it
links the same `z80core/z80.o`/`alu.o` and mounts the same `abcbus/` card.
It is at **milestone 4**: `bin/abc806` (opt-in, `make abc806`) boots the
real 32K firmware, programs the MC6845 for 80x25, and runs a genuine live
session — `--interactive` gives real 3 MHz pacing, a live keyboard and a
screen redrawn at 30fps *in colour*, on the terms `bin/abc80 --interactive`
established. `--type` answers `PRINT 6*7` with `42`, `--screen` dumps the
text screen, `--screenshot` writes a real PNG, and `--disk` boots real
UFD-DOS — `BYE` reaches the DOS command shell and the shell loads and runs
`LIB` off the media, which is the assertion worth making since `LIB` is a
program on the disk rather than a shell built-in. No high-resolution
graphics and no GTK front-end yet.

**Milestone 5 (high-resolution graphics) is investigated but not started,
and what the investigation found is worth knowing before repeating it.**
The option PROM's keyword table holds `FGPOINT`, `FGLINE`, `FGFILL`,
`FGCTL`, `FGPAINT` and `FGPICTURE`, and they do draw - into a framebuffer
this emulator throws away. **The high-resolution plane is CPU-addressed at
`0x0000`-`0x77FF`: 240 rows of a 128-byte pitch at 4 bits per pixel, 30,720
bytes, ending exactly where character RAM begins at `0x7800`.** That is not
from a datasheet but from the ROM: it clears the region with
`LD (HL),0` + `LDIR` at `0x7CB2`, from code sitting at `0x7CAC` - *above*
the framebuffer, the only part of the low 32K that could still be ROM while
the plane covers the rest - and `FGPOINT`'s range check is `LD HL,00EFh`,
239 being 240-1. `FGLINE`'s plot at `0x7E31` is a masked read-modify-write
stepping `0x80` at a time.

`memory.c` currently drops every write below `0x8000`, which is **known to
be wrong and deliberately left rather than half-fixed**: routing writes to
the plane makes them land, but the clear's `LDIR` and the plot both *read*
that region too, and making reads symmetric kills the machine (the
interrupt vectors and the ROM's data tables are down there). The open
question is what distinguishes a ROM data read from a plane data read at
the same address; KEYDTR is written once and never changes, EME is on but
the page map is 256 uniformly-zero entries, and there is no bank-switching
`OUT` anywhere in the graphics path. Two diagnostics established all of
this and are kept: `ABC806_TRACE_WRITES=1` (every CPU write with the
EME/KEYDTR/HRS state, and every dropped one with its PC) and
`ABC806_PROFILE_ALL=1` (with `--profile`, for differential profiling).
**Diff write *counts*, not the set of addresses** - the coarser comparison
produced a confident wrong conclusion once already.

Three further ABC806 facts worth knowing before touching that target, none
guessable. **Its memory map is decided by a PAL16L8** (`ABC-P4-1.bin`,
committed, a well-formed JEDEC fuse map) rather than by address decode;
`emu/src/memory.c` currently follows MAME's behavioural form of it, which
is also where MAME's own `abc806 30K banking` TODO lives, so evaluating
the real fuse map is an open opportunity rather than a settled question.
**The page map's entries are stored inverted** - MAME reads them as
`m_map[page] ^ 0xff` before testing ENL in bit 7, so an entry of zero
means "do not divert"; with the polarity reversed, enabling EME sends
every access to video RAM and the machine dies thousands of instructions
later on an illegal opcode. And **DTR-B is not LRS here**: on the ABC802
that pin selects ROM or RAM in the low 32K, on the ABC806 it is KEYDTR,
swapping the low 32K between ROM and the high-resolution plane. Same chip,
same pin, different wiring - which is also why
`abc806/emu/src/ports.c` is deliberately still a near-copy of the
ABC802's rather than a shared module: extraction waits until it is known
what is genuinely common, exactly as `abcbus/` did.

Project history lives in the top-level `docs/` too, and is worth
consulting before repeating an investigation: `docs/JOURNAL.md` is a
running log organized by *when* (what was worked on, why an approach was
chosen, what turned out to be wrong first), and `docs/postmortems/` holds
write-ups of failures whose lesson outlived the fix — currently the
missing block I/O opcodes, `--type`'s raw-UTF-8 bug, the DART's
single-receive-byte constraint, and the "boot screen cannot validate the
feature" near-miss. Each machine target's *finished* work now lives in its
own `*_COMPLETED.md` (`cpm/docs/COMPLETED.md`,
`abc80/docs/ABC80_COMPLETED.md`, `abc802/docs/ABC802_COMPLETED.md`) rather
than in its roadmap, so each `ROADMAP.md` answers only "what works, what
doesn't, what's next" — they had grown to 4,605 lines between them, nearly
all of it completed-work narrative. The milestone write-ups themselves are
unchanged and still worth reading: most of this project's hard-won
hardware knowledge is in them, especially the "found the hard way" notes.

Two generic reference docs live in the top-level `docs/` (not
CP/M-specific, so not under `cpm/docs/` — same reasoning as `asm/`/
`disasm/` above, which they document): `Z80_REFERENCE.md` (the Z80
instruction set, including undocumented opcodes/flag behavior, plus
which of those this emulator can actually execute today) and
`ASSEMBLER.md` (the `z80asm` syntax — directives, expressions, macros).
`cpm/docs/CPM_REFERENCE.md` (the CP/M 2.2 BDOS/BIOS call spec — function
numbers, FCB layout, BIOS jump table — that Phase 3's `cpm.c` work
targets) stays in `cpm/docs/` alongside the roadmap, genuinely
CP/M-specific. This file (`CLAUDE.md`) instead covers *code*
architecture — how the dispatch/encoding is actually implemented, not
the ISA, syntax, or OS spec itself.

## Build & Run

The emulator lives in `cpm/emu/src/`, the assembler in `asm/src/`, the
disassembler in `disasm/src/`; the Makefile builds all three into `bin/`
at the repo root.

```
make               # builds bin/z80, bin/z80asm, and bin/z80dasm
make emulator      # just the emulator
make assembler     # just the assembler
make disassembler  # just the disassembler
make run           # build the emulator, then run zexall.com through it | less
make test          # build and run all three suites (see below)
make test-cpm      # just cpm/tests/run_tests.sh
make test-abc80    # just abc80/tests/run_tests.sh
make test-abc802   # just abc802/tests/run_tests.sh
make clean         # remove object files and all three binaries
```

`make test` covers every target that builds without external
dependencies, which is all of them except the two GTK apps. Each machine
target has its own suite (`abc80/tests/`, `abc802/tests/`) driving the
real ROMs and asserting on what the machine produced — the screen it
rendered, the file it wrote, the port it answered — with shared reporting
primitives in `scripts/testlib.sh`. Where a check can be written as
BASIC it is, since `OUT`/`INP`/`PEEK` make the emulated machine its own
test harness and so exercise the real port decode and the ROM's own
implementation rather than a C-level shortcut around both. The floppy
checks need real disk images this repo deliberately does not commit; set
`ABC80_TEST_DISKS`/`ABC802_TEST_DISKS` to a directory holding them and
they run, and otherwise they **skip loudly** and are counted separately
from passes. Two ASCII-art fixtures (`*/tests/fixtures/chargen.txt`,
regenerated by each target's own `regen-fixtures.sh`) cover the character
generator decodes, which no boot screen exercises. The suites were
validated by deliberately injecting six real regressions and confirming
each was caught — that sweep immediately found one check asserting on the
*echoed command line* rather than on BASIC's answer, which passed with
its subject entirely broken.

Correctness is primarily verified by running the ZEXALL exerciser and
reading its console output for per-opcode `ERROR` reports vs. `OK` lines.
ZEXALL/ZEXDOC (`cpm/emu/zexall/ZEXALL-main/`) are third-party, downloaded
pre-built CP/M test binaries (by Frank D. Cringle, via YAZE-AG, GPLv2) —
not code belonging to this project, and not meant to be edited. They're the
correctness oracle: if the emulator is right, every opcode reports "OK"; a
wrong flag or result shows up as an "ERROR" line naming the instruction. As
of the last full run, both `zexall.com` and `zexdoc.com` pass cleanly
(67/67 OK, 0 errors, 0 unimplemented opcodes). `make test`
(`cpm/tests/run_tests.sh`) turns that "eyeball the output" check into an exit
code: it runs both exercisers and fails on any `ERROR`/`Unimplemented
opcode` line or a missing `Tests complete`, then assembles and runs every
`asm/examples/*.asm` program and fails on any `FAIL` line (the `OK n`/
`FAIL n` convention `selftest.asm`/`gaps_test.asm` use). ZEXALL/ZEXDOC
don't exercise I/O ports, `IM`, `RETI`/`RETN`, or `LD A,I`/`LD A,R`/`LD
I,A`/`LD R,A` — `asm/examples/gaps_test.asm` is the only regression
coverage for those.

`bin/z80` takes the `.com` file to run as argv[1] (paths are resolved
relative to the working directory you invoke the binary from, typically
`cpm/` — `bin/` itself stays at the true repo root, so run these as
`../bin/z80` from inside `cpm/`, or `bin/z80` from the repo root with a
`cpm/`-prefixed argument) — there's no default program; running it with
no arguments (or `-h`/`--help`) just prints usage instead:

```
../bin/z80 emu/zexall/ZEXALL-main/zexdoc.com
```

Any further argv entries become the program's own CP/M command-line
arguments — `../bin/z80 cpm_disk/CC.COM HELLO.C` compiles `HELLO.C` the
way a real CCP-launched `CC HELLO.C` would. `write_command_tail()`
(`main.c`) seeds the raw tail at `0x0080` (length byte) /`0x0081`
onward — space-prefixed, uppercased, *not* null-terminated, confirmed
against this project's own real CCP source
(`cpm/resources/ccp/upstream/ccp.asm`'s `bmove0`/`bmove1`/`bmove2`) rather
than guessed. `write_default_fcb()` additionally auto-parses the first
two arguments into the default FCBs at `0x005C`/`0x006C`, matching what
a real CCP also does before running a program — command-line CP/M
utilities of this era commonly read one or the other (or both). Neither
existed before BDS C's `CC.COM`/`CLINK.COM` needed it: every program
tested here up to that point was menu-driven (Turbo Pascal, WordStar,
dBASE, Tasty Basic, MBASIC) and never took an argument this way.
`write_default_fcb()` also expands a bare `*` into `?` for every
remaining position in whichever field (name or type) it appears in,
rather than storing it literally — confirmed against
`cpm/resources/ccp/upstream/ccp.asm`'s own `setname`/`setnam0` and `setty`/
`setty0` routines (the "must be ?'s" comment there), not guessed. Found
via BDS C's own `LDIR.COM` (a real utility for listing `.LBR` library
archives, part of the full `bdsc-all.zip` distribution — see
`cpm/resources/bdsc/upstream/README.md`): given a `*.*` pattern, it silently
reported "No (matching) members found" for every library, since it reads
the pattern out of the FCB looking for `?` wildcards — any program that
reads a raw FCB instead of the text tail is entitled to assume a real
CCP already expanded `*` before it got there, and a literal `*` byte
doesn't match anything. `main.c` writes the default FCBs *before* the
raw tail, not after — FCB2 (`0x006C`-`0x008F`) physically overlaps the
tail buffer (`0x0080` onward), a well-documented real CP/M memory-map
quirk, and whichever gets written last wins that overlap. Real CCP
(`ccp.asm`'s `move0`-then-`bmove0..3` sequence right before its `tran`
call) builds the FCBs first and writes the tail last, so a real command
line is always intact no matter how many arguments it had — FCB2's own
meaningful fields (`DR`/name/type, `0x006C`-`0x0077`) don't reach into
the overlap at all, so real programs relying on FCB2 lose nothing. An
earlier version of this function had the order backwards (tail first,
FCBs second), which silently truncated the tail to nothing the moment a
*second* real argument existed — invisible until `l2 t -d` (BDS C's
alternate linker, two arguments) needed it; `l2 t` (one argument) worked
fine, since FCB2 is only written at all when a second argument exists.

The zexdoc variant checks only documented flag behavior; zexall also
checks the undocumented flags (bits 3 and 5, `FLAG_X`/`FLAG_Y`). Passing
`--ccp [ccp.com]` instead of a plain `.com` path boots a CP/M CCP shell
(default `cpm_disk/ccp.com`) rather than running a single program — see
this file's own BIOS section below and `cpm/docs/CPM_REFERENCE.md`'s CCP
section for how that works.

## Architecture

(For the Z80 instruction set itself — mnemonics, addressing modes,
undocumented opcodes/flags — see `docs/Z80_REFERENCE.md`. This section is
about how the dispatch code is structured, not the ISA.)

**Table-driven dispatch.** `z80_init_tables()` (in `z80core/z80.c`) populates
`main_opcode_table[256]` (a `Z80OpcodeHandler` array) mapping each opcode byte
to a handler function. `z80_execute()` (`z80.c`) is the actual machine-agnostic
core: it samples interrupts, fetches one opcode byte, bumps the R register,
and calls `main_opcode_table[opcode](cpu, ram)`. `z80.c`/`alu.c` have no
CP/M awareness at all - `z80_step()` (`cpm.c`) is CP/M's own thin wrapper
around `z80_execute()`, intercepting BDOS/BIOS calls (`PC == 0x0005`/
`PC == 0x0000`, see below) before handing off. This split exists because
those two addresses sit inside real firmware code for `abc80/`'s ABC80
machine target (see that project's own `docs/ABC80_ROADMAP.md`), which calls
`z80_execute()` directly instead - `bin/z80`'s own `main.c` is the only
caller of `z80_step()`. Handlers return the T-state cycle count for the
instruction (or a negative value on an unimplemented/fatal opcode, which
halts the caller's run loop).

Several opcode ranges are handled generically instead of one handler per
opcode, decoding register indices out of the opcode byte itself:
- `0x40`–`0x7F` → `z80_op_ld_r_r` (register-to-register loads; `0x76`/HALT is
  a special case within this range).
- `0x80`–`0xBF` → `z80_op_alu_group` (ADD/ADC/SUB/SBC/AND/XOR/OR/CP against
  any register/`(HL)`).
Both use `get_cb_reg`/`set_cb_reg` to map a 3-bit register index to
B/C/D/E/H/L/(HL)/A.

**Prefixed instructions** get their own dispatcher functions, wired up as
single entries in `main_opcode_table` (not separate 256-entry tables — despite
some earlier/commented-out code suggesting otherwise, `0xCB` and `0xED`
dispatch via an opcode `switch` inside `z80_op_prefix_cb`/`z80_op_prefix_ed`):
- `0xCB` → `z80_op_prefix_cb` (rotate/shift/BIT/SET/RES via `get_cb_reg`/
  `set_cb_reg`).
- `0xED` → `z80_op_prefix_ed` (extended ops: block transfer/search, NEG,
  16-bit ADC/SBC, `LD SP,(nn)`, `RLD`/`RRD`, `IN r,(C)`/`OUT (C),r`, `IM
  0`/`1`/`2`, `RETI`/`RETN`, `LD A,I`/`LD A,R`/`LD I,A`/`LD R,A`, etc.).
- `0xDD`/`0xFD` → `z80_op_prefix_index`, which explicitly decodes the
  IX/IY-specific opcodes (16-bit load/arith/inc/dec, push/pop, `EX (SP),IX`,
  `JP (IX)`, `LD SP,IX`, `(IX+d)` displacement forms, and the undocumented
  `IXH`/`IXL`/`IYH`/`IYL` 8-bit ops). For the `0x40`-`0x7F` and `0x80`-`0xBF`
  ranges it delegates to `z80_op_index_ld_r_r`/`z80_op_index_alu_group`,
  which substitute `IXH`/`IXL` for `H`/`L` *unless* the other operand is
  `(IX+d)` memory — real Z80 hardware quirk: `LD H,(IX+d)` loads real `H`,
  not `IXH`. Any opcode not covered by any of this (genuinely
  prefix-independent, e.g. arithmetic/logic against `B`/`C`/`D`/`E`/`A`) falls
  back to `main_opcode_table[opcode]`. A nested `0xDD/0xFD 0xCB d opcode`
  double prefix is handled by `z80_op_index_cb`.

**ALU logic lives in `z80core/alu.c`/`alu.h`**, separate from opcode dispatch: flag
bit masks (`FLAG_C`, `FLAG_N`, `FLAG_PV`, `FLAG_X`, `FLAG_H`, `FLAG_Y`,
`FLAG_Z`, `FLAG_S`) and the actual add/sub/logic/rotate/shift/block-op
implementations that compute result + flags. Opcode handlers in `z80.c` call
into these rather than duplicating flag math.

**Memory is a flat 64KB `uint8_t` array** (`RAM_SIZE` in `z80.h`), owned by
`main.c` and pointed to by `Z80.memory`. `z80_read_byte`/`z80_write_byte` are
a bus abstraction: `z80_write_byte` just indexes straight into that array (no
bank switching/MMU), and `z80_read_byte` does too *unless* `Z80.bus_read_hook`
is set — an optional per-machine function pointer, `NULL` by default (every
existing caller, CP/M included), checked after the flat-array read so it can
override specific addresses' values without the shared core knowing anything
about why. Added for `abc80/`'s own memory-mapped-bus needs (see that
project's own `docs/ABC80_ROADMAP.md`, Milestone 6) rather than speculatively
— the CP/M target never sets it, so its behavior is unchanged. **I/O ports**
are a separate 256-entry `cpu->io_ports` array
with their own `z80_io_in`/`z80_io_out` bus functions — no real devices are
attached, so a port read just returns whatever was last written there.

**CP/M BDOS emulation (`cpm.c`)**: `check_cpm_bdos()` runs at the top of
every `z80_step()` and, when `PC == 0x0005` (or `PC == BDOS_ENTRY`, see
below), handles the BDOS functions `cpm/docs/CPM_REFERENCE.md` documents —
`P_TERMCPM` (0), console output (2, 9), console input (1, 6, 10, 11),
`S_BDOSVER` (12), file I/O (15–23, 26, 33–35, 40), and drive/user
bookkeeping stubs (13, 14, 25, 32) — then manually pops the return
address off the stack into `PC` to simulate the `RET`, unconditionally,
regardless of which function matched (or none did) — so the actual
instruction bytes at `0x0005` never matter for BDOS calls to execute
correctly. `main.c` preloads a real `JP <BDOS_ENTRY>` there (`BDOS_ENTRY`
in `cpm.h`, a plausible-looking but otherwise-inert high address, not
real resident BDOS code) rather than just a bare `RET` — some real
software reads the address out of `0x0006`-`0x0007` as a proxy for "how
much TPA is free" (Turbo Pascal's `TINST.COM` is a concrete example: with
only a bare `RET` there, it read back essentially zero free memory and
refused to start at all). `check_cpm_bdos()` intercepts `BDOS_ENTRY`
itself identically to `0x0005`, the same self-referencing-target
reasoning as the BIOS vectors below, in case software calls that address
directly having read it back rather than always using `CALL 5`.
Immediately before that simulated `RET`, `check_cpm_bdos()` mirrors
whatever it put in `A` into `L` too (`H` set to `0`) — real CP/M BDOS
does this for every function except the couple (27, 31) that return a
genuine 16-bit pointer in `HL` instead of a status code (documented
behavior in the CP/M 2.2 Programmer's Reference, not an emulator
invention), and it matters here because BDS C's own `bdos()` library
wrapper returns its result via `HL` — the standard 8080 C
int-return-value register pair — not `A`. Four functions (1, 6, 11, 12)
already set `L` by hand before this existed, presumably because each
one's own real-software bug already surfaced the need; the generic
mirror at the end covers every other function instead of requiring each
future one to remember it individually. Found getting BDS C's own CDB
debugger operational (`cpm/resources/bdsc/upstream/README.md`'s "not
included" list) — CDB2's target-loading loop's compiled
`if (bdos(20,fcb)) break;` broke on the very first record despite
`F_READ` genuinely succeeding (`A=0`), because `HL` still held whatever
was left over from earlier in CDB2's own code, truncating every debugged
program to its first 128 bytes; assembly-level callers checking `A`
directly (every program validated before this one) never hit it.
`asm/examples/file_test.asm` check 8 is the permanent regression test.

**BIOS emulation (`cpm.c`)**: `check_cpm_bios()` runs alongside
`check_cpm_bdos()` and handles direct BIOS calls — some real software
(MBASIC's own console-output routine, for one) calls straight into the
BIOS instead of BDOS, bypassing the function-dispatch overhead, and
that's exactly why a full BIOS layer matters here rather than just
`check_cpm_bdos()`. `cpm_bios_init()` (called once from `main.c`) installs
a real `JP <wboot>` at address `0x0000` (not a bare `RET` — some software,
MBASIC included, reads this jump's target to locate the BIOS) and a
17-vector jump table at a fixed `BIOS_BASE`. Every vector is a genuine
`JP <self>`, not a bare `RET`, because MBASIC goes one step further: it
reads a vector's *own jump target* once at startup and self-patches that
address directly into its own code, permanently bypassing the jump table
for speed — a self-referencing `JP` means that trick and a direct call
both land on the identical address, so `check_cpm_bios()` intercepts
either path identically. It gives real behavior to `WBOOT`/`CONST`/
`CONIN`/`CONOUT` (reusing the same console plumbing as the BDOS
functions) and fixed, sensible responses for the rest; see
`cpm/docs/CPM_REFERENCE.md`'s BIOS section for the full vector-by-vector
rundown.

Console *input* needs the host terminal in raw mode
(no line buffering, no local echo) so character-at-a-time BDOS calls see
input the way real CP/M hardware would rather than waiting for a host
Enter keypress; `cpm_console_init()` (called once from `main.c`, `termios`-
based) only touches terminal mode when stdin is actually a TTY (`isatty`),
leaving a piped/redirected stdin untouched, and restores the original
mode via `atexit()`. Function 10 (`C_READSTR`, buffered line input) does
its own minimal line editing (echo, backspace/DEL) since raw mode disables
the terminal's own. Beyond `ICANON`/`ECHO` and the `ICRNL`/`INLCR`/`IGNCR`
CR-vs-LF fix, `IXON` (classic Unix software flow control) is disabled too
— left on, the host tty driver intercepts `Ctrl-S`/`Ctrl-Q` as XOFF/XON
and never delivers the byte at all, silently breaking any real program
that uses them for something else (Turbo Pascal's editor binds `Ctrl-S`
to cursor-left, which is what surfaced this).

`console_char_ready()` (BDOS `C_STAT`/BIOS `CONST`) can't just ask
`select()` "is stdin readable" - for a piped/redirected stdin, `select()`
reports readable both when a real byte is waiting *and* when stdin has
hit EOF (a `read()` genuinely wouldn't block either way), but a real
terminal's console status is never ambiguous like that (idle just means
no key pressed yet). Software that polls status before reading - BDS C's
own console-output routine checks for a Ctrl-C abort after printing
*every* character - saw "ready" forever once a non-interactive stdin ran
dry, called what it thought was a real read, and got EOF's `^Z` (26)
sentinel echoed into the output stream after each character it printed.
Fixed by having `console_char_ready()` actually attempt the read itself
to disambiguate: a genuine byte gets buffered in `pending_char` for the
next `console_read_char()` call (so it isn't lost), while a real EOF
sets a sticky `seen_eof` flag so status checks stop reporting "ready"
from then on, matching how a real console never spontaneously un-idles
on its own.

Console *output* (BDOS functions 1's echo, 2, 6, 9, and BIOS `CONOUT`)
routes every program-supplied byte through `console_emit()` rather than
calling `putchar()` directly. Plain ASCII (`< 0x80`) passes through
unchanged; high-bit bytes (`0x80`-`0xFF`) are translated from CP437 (IBM
PC/DOS "code page 437" — box-drawing, block-shading, and a handful of
accented/Greek/math glyphs) to the equivalent Unicode codepoint and
emitted as UTF-8, via `cp437_high[]` + `putchar_utf8()`. Real CP/M-era
software targeting a graphical terminal commonly emits CP437 bytes for
exactly this kind of output (SARGON's ANSI-enhanced port,
`cpm/resources/sargon/sargon78.com`, is a concrete example — its own README
tells PuTTY users to explicitly set "Code Page 437"). VT100/ANSI cursor-
positioning and color (`SGR`) escape codes need no translation at all —
they're plain ASCII bytes the host terminal already interprets correctly
on its own. Console *input* echo (typed keystrokes) skips `console_emit()`
entirely, since that's always plain ASCII from the keyboard.

`console_emit()` also runs a small state machine (`console_term_state`)
that translates two other, older protocols, each found the same way: a
real full-screen CP/M program behaving correctly in line mode but
printing garbage the moment it drew a form or editing screen.

Real Ashton-Tate dBASE II (`cpm/cpm_disk/DBASE.COM`) was hardcoded, at
whatever terminal type it was originally installed for, to a
Lear-Siegler ADM-3A-class terminal — shared by several CP/M machines'
own built-in terminals (Kaypro among them) — rather than VT100/ANSI:
direct cursor addressing is `ESC = <row+32> <col+32>` (not VT100's
`ESC [ row ; col H`), and `^Z` (0x1A) clears the screen (not VT100's
`ESC [ 2 J`). Confirmed by capturing dBASE II's own raw output
byte-for-byte while driving it through a pty with paced keystrokes (the
same pty/paced-keystroke technique the `find_or_reopen_file()`
investigation above used) — on a plain xterm, neither sequence means
anything, so it printed as literal garbage ("RECORD # 00001" preceded by
a stray "1", stray "!"/"@" where a cursor-address landed, etc.) instead
of moving the cursor. `ESC B <n>` / `ESC C <n>` bracket some video
attribute (almost certainly reverse-video/underline for field
highlighting) whose exact ADM-3A-variant mapping isn't confirmed from
primary-source documentation, so rather than guess at an SGR code,
`console_emit()` only strips those 3-byte sequences — that alone removes
the stray digits from the screen even without reproducing the highlight
itself.

Edward Ream's RED screen editor — part of the same BDS C distribution
as `CC.COM`/`CLINK.COM` (see the BDS C section below), but with its own,
separate copyright (`Copyright (C) 1986 by Enteleki, Inc.`, printed at
its own startup — not covered by Leor Zolman's public-domain release of
the compiler itself, so RED's own source isn't committed to this repo,
unlike `CC.COM`/`CLINK.COM`) — targets a VT52/Heath-Zenith-H19-class
terminal instead: a real, well-documented standard, not guessed. Cursor
addressing is `ESC Y <row+32> <col+32>` (VT52's own convention, the same
offset scheme as ADM-3A's `ESC =` just under a different letter), `ESC K`
erases to end of line (plain VT52), and `ESC l` erases the entire current
line without moving the cursor (an H19 extension beyond plain VT52). RED
doesn't use a dedicated clear-screen code at all — it clears by
positioning to each row in turn and issuing `ESC l`, confirmed the same
pty-capture way. `ESC M` (Reverse Index) also appears in that capture but
needs no translation at all — real ANSI/VT100 terminals already support
it natively as the identical bare `ESC M`, no `[` (CSI) required.

None of `ESC =`/`ESC Y`/`ESC B`/`ESC C`/`ESC K`/`ESC l` collide with real
VT100/ANSI, which always follows `ESC` with `[` (CSI) for cursor/color
control (or is otherwise a real, already-supported bare code like
`ESC M`), so this translation is a pure superset of the old
plain-passthrough behavior: an unrecognized byte after `ESC` (including
`[`) is replayed through untouched, so any other program's real ANSI
escape codes are unaffected. `asm/examples/term_test.asm` is the
permanent regression test — unlike the file-I/O checks, there's no
CP/M-visible way for a program to read back its own translated console
output, so `cpm/tests/run_tests.sh`'s own dedicated check greps the raw byte
stream this program produces for the expected ANSI translation instead
of relying on an in-program OK-n/FAIL-n self-check.

**File I/O** maps every drive/user number onto one host directory
(`CPM_DISK_DIR`/`cpm_disk/`, created by `cpm_fileio_init()` relative to
wherever `bin/z80` is invoked from) rather than emulating real disk
geometry — the simplest of the options weighed for how FCB-addressed
files should map onto the host filesystem, at the cost of not being able
to express real drive-switching or boot an actual CP/M disk image (see
`cpm/docs/CPM_REFERENCE.md`'s Implementation status section for the trade-off
in full). `build_host_path()` converts an FCB's name/type fields straight
into a host path; `fcb_pattern()`/`fcb_pattern_match()` implement `'?'`
wildcard matching for `F_SFIRST`/`F_SNEXT`/`F_DELETE`. Since there's no
disk-block bookkeeping, open files are tracked in a small table
(`open_files[]`) keyed by the FCB's own memory address — real CP/M
programs have no notion of a file handle distinct from the FCB they
opened, so this mirrors how callers already think about "which file."
Sequential I/O keeps the FCB's real `EX`/`CR` fields in step (one 16KB
extent = 128 records); random I/O (`R0`-`R2`) is a plain linear record
number multiplied by 128 and seeked to directly. `F_OPEN` honors a
caller-supplied nonzero `EX` (computing `RC` relative to that extent's
base record) instead of always resetting to 0 — needed for programs that
reposition mid-file by setting `EX`/`CR` before re-opening rather than
always reading sequentially from the start (a real CP/M `F_OPEN` searches
the directory for the extent matching whatever `EX`/`S1`/`S2` the caller
already put in the FCB). The common `EX==0` case (a fresh, from-the-start
open) is unaffected — `CR` is still reset to 0 there, since plenty of
real programs assume `F_OPEN` does that for them. `F_OPEN`'s
`alloc_open_file()` reuses (closing the stale handle first) any existing
`open_files[]` entry already at the target FCB address rather than
allocating a second entry alongside it — real CP/M has no file-handle
concept distinct from the FCB itself, so a program reusing one FCB
buffer for a new file without an intervening `F_CLOSE` (completely
normal — Turbo Pascal's compiler does exactly this loading `TURBO.MSG`
and then a work file through the same FCB) must transparently start
reading the new file, not silently keep re-reading whichever file
happened to open that address first (found via a real Turbo Pascal
compile that mysteriously always stopped at the same byte count no
matter the source file's real size or content — traced to `TURBO.MSG`'s
own trailing bytes, still open, shadowing every later open at that FCB
address). Random I/O (`F_READRAND`/`F_WRITERAND`/`F_WRITEZF`, functions
33/34/40) goes through `find_or_reopen_file()` rather than
`find_open_file()` — real CP/M's random-access functions work directly
off the FCB's own `EX`/`S1`/`S2` fields, which a `F_CLOSE` doesn't erase,
so a program reusing an already-closed FCB for random I/O without an
intervening `F_OPEN` is relying on real (if informally documented)
CP/M behavior, not committing a bug; `find_or_reopen_file()` transparently
opens the file by the FCB's own filename when no `open_files[]` entry
already exists, the same real-hardware reasoning `alloc_open_file()`
already applies to the sequential-I/O FCB-reuse case above. Found via a
real Ashton-Tate dBASE II binary: `CREATE`ing then `USE`ing a database
printed `End of file found unexpectedly`, and `QUIT`ing it afterward
printed `Disk is full` — both traced to the exact same cause (dBASE
re-reads a just-written `.DBF`'s header via random I/O through an FCB it
had already closed during `CREATE`, without reopening), not two separate
bugs, and not a disk-space or DPB issue at all despite dBASE's own
wording; both messages are gone with `find_or_reopen_file()` in place.
`asm/examples/file_test.asm`'s check 6 is the regression test. Sequential
`F_READ`/`F_WRITE` (functions 20/21) go through `find_or_reopen_file()`
too, for the identical reason applied to a different BDOS function: found
validating BDS C's own `CC.COM` on a source file large enough to need a
`#include` — its `#include` handling reuses a single FCB (`0x005C`) for
both the main source file and each included header, `F_CLOSE`ing it
after the header and resuming the outer file's `F_READ` from its saved
`EX`/`CR` with no intervening `F_OPEN`. Before this, that second
`F_READ` failed with error 9 ("unopened FCB"), which `CC.COM` surfaced to
the user as `Disk read error` partway through any file needing more than
one `#include`. `asm/examples/file_test.asm`'s check 7 is the regression
test.

**A fake Disk Parameter Block** (`DPH_BASE`/`DPB_BASE`/`DIRBUF_BASE`/
`ALV_BASE` in `cpm.c`, written once by `cpm_bios_init()`) backs BDOS
functions `DRV_DPB` (31) and `DRV_ALLOCVEC` (27), plus BIOS `SELDSK` —
previously unhandled, so a caller got back whatever `HL` already
contained rather than a real DPB address. Found via Turbo Pascal's `D`ir
command showing `Bytes Remaining On A: 0k` despite writes succeeding
(now reports a plausible `8160k`) — see `cpm/docs/CPM_REFERENCE.md`'s File
I/O Implementation status section for the exact values (an ~8MB fixed
disk, computed per the real CP/M 2.2 Alteration Guide's DPB formulas)
and the full story, including a real Ashton-Tate dBASE II binary whose
own `Disk is full` message looked like the same bug but turned out to
have a different, still-open cause.

**Booting a CCP (`main.c`, `cpm.c`)**: `bin/z80 --ccp <path>` loads a CP/M
Console Command Processor (the `A>` shell — see `cpm/resources/ccp/`) at
`CCP_BASE` (`0xE400`) instead of loading a single program at `0x100`, and
calls `cpm_set_ccp_mode(1)`. With CCP mode on, `check_cpm_bios()`'s
`WBOOT` handling — normally "set PC to 0, which the run loop treats as
the program terminating" — instead re-enters the CCP at `CCP_BASE`,
first loading register `C` from `ram[0x0004]` (the persisted disk/user
byte the CCP itself maintains via its own `setdiska` routine before ever
running a program), matching what a real BIOS's `WBOOT` does before
jumping to the CCP's cold-boot entry point. Two places besides
`check_cpm_bios()` needed to become CCP-mode-aware
(`cpm_is_ccp_mode()`/the `ccp_boot` flag `main.c` threads through) since
both otherwise assume `PC == 0x0000` always means "halt the emulator":
`main.c`'s own loop-level check (skipped entirely in CCP mode, since the
`JP <wboot>` instruction at address `0x0000` needs to actually execute
so `check_cpm_bios()` can catch it at the real `WBOOT` vector address),
and a second, separate guard inside `z80_step()` itself (also skipped in
CCP mode — without that, `PC` would never advance off of `0x0000` at all,
since `main.c` no longer breaks its loop there either).

## Assembler (`asm/src/`)

A conventional two-pass design, no lexer/token-stream stage — each source
line is parsed directly as a string:

- `symtab.c`/`.h` — a simple linked-list symbol table. `symtab_define()`
  tolerates being called twice with the *same* value (pass 1 defines a
  label, pass 2 redefines it to the same address) but rejects a genuine
  conflicting redefinition.
- `expr.c`/`.h` — recursive-descent expression evaluator (`+ - * / % & |
  ^ ~`, parens, `$` for the current address, `low()`/`high()`, `'c'` char
  literals, `0FFh`/`0xFF` hex). On pass 1, an undefined symbol evaluates to
  `0` and sets `env->unresolved` instead of erroring, since it may be
  defined later in the source; pass 2 treats the same case as a real error.
- `encode.c`/`.h` — the instruction encoder. `parse_operand()` classifies
  each comma-separated operand purely syntactically (register name, `(...)`
  memory form, or fall through to `OP_IMM` expression text) *without*
  evaluating expressions, which is what lets the same `encode_instruction()`
  path run unchanged on both passes: instruction length in Z80 depends only
  on the addressing-mode syntax, never on an expression's value, so pass 1
  doesn't need real values, only correct byte counts. Mirrors the emulator's
  own decoder logic in reverse, including the `IXH`/`IXL`/`IYH`/`IYL`
  half-index-register encodings and the real-`H`/`L`-when-memory-is-the-other-
  operand quirk (`idx_rfield()` here is the encode-side counterpart of
  `get_idx_reg8`/`set_idx_reg8` in `z80.c`).
- `assemble.c`/`.h` — line-level driver: strips comments (quote-aware, so a
  `'` or `"` containing `;` isn't mistaken for a comment), extracts an
  optional `label:`, and either handles a directive (`ORG`, `EQU`, `DB`/
  `DEFB`, `DW`/`DEFW`, `DS`/`DEFS`, `END`) or calls `encode_instruction()`.
  Bytes are written straight into a 64KB `AsmCtx.image` buffer at the
  current `pc` via `asm_emit()`, which only actually writes on pass 2 (pass
  1 just advances `pc` and tracks the min/max address touched) — this is
  also why `DB`/`DW`/`DS` have no line-length limit despite the small,
  fixed-size `EncOut.bytes[8]` used for instructions (real Z80 instructions
  never exceed a handful of bytes; `DB "long string"` can be arbitrary
  length, so those directives write to the image directly instead of
  routing through `EncOut`).
- `main.c` — CLI entry point and the two-pass driver (`run_pass()`, called
  once per pass). On success, writes `image[min_addr..max_addr)` to the
  output file — i.e. the output covers only the address range something was
  actually assembled into, trimmed to whatever `ORG` the source used.
  `run_pass()` walks the preprocessed line array by index (not a plain
  `for`) specifically so it can handle `REPT count`/`ENDM`: on hitting a
  `REPT` line it finds the matching `ENDM` (nesting-aware, for `REPT`
  inside `REPT`), evaluates `count` against that pass's live `$`/symbol
  state, and reprocesses the enclosed line range that many times before
  continuing — `assemble_line()` can't do this on its own since it only
  ever sees one line at a time, and (like `IF`) `REPT`'s count can be
  `$`-dependent, so it can't be resolved by `preprocess.c` before
  addresses exist.

Preprocessing (`preprocess.c`/`.h`) runs once, before the two passes above,
flattening `MACRO`/`ENDM`/`LOCAL`-expanded and `INCLUDE`-spliced source into
a flat line list; conditional assembly (`IF`/`ELSE`/`ENDIF`) and `REPT`/
`ENDM` are integrated into the real two-pass loop instead (`assemble.c` and
`main.c` respectively), since both need live `$`/symbol state rather than
pure text substitution. One preprocessing-level wrinkle `REPT` introduces:
`MACRO`/`ENDM` and `REPT`/`ENDM` both close with the literal keyword
`ENDM`, so capturing a macro body that contains a nested `REPT` block (as
`zexall.mac`'s own `dss` macro does) needs `preprocess.c` to track that
nesting explicitly — otherwise the inner block's `ENDM` would be mistaken
for the end of the macro itself. See `docs/ASSEMBLER.md` for the syntax
this all produces/consumes, and `cpm/docs/ROADMAP.md` for exact project
status — as of the last update, `bin/z80asm` assembles the real,
unmodified ZSM4 sources `zexall.mac`/`zexdoc.mac` (not just the
Perl-generated `zexall.z80`/`zexdoc.z80`) with zero errors, and the result
runs cleanly through `bin/z80`.

## Disassembler (`disasm/src/`)

The inverse of `asm/src/encode.c`, in a separate binary rather than a
`z80asm` mode flag — reading is a different shape of problem than writing
(no expression evaluator or symbol table, but does need to re-derive
labels).

- `decode.c`/`.h` — `decode_instruction(mem, addr)` decodes exactly one
  instruction from a full 64KB memory image and returns its formatted
  text, byte length, and (if the instruction references an absolute
  address — a jump/call target or a `(nn)` memory operand) that address,
  tagged as code or data. Structured like the emulator's own dispatch
  (`decode_cb`/`decode_ed`/`decode_index`/`decode_index_cb` mirroring
  `z80_op_prefix_cb`/`_ed`/`_index`/`z80_op_index_cb`), except every path
  returns text instead of executing. The `DD`/`FD` "not one of my special
  cases" fallback recurses into `decode_instruction_impl` one byte later
  rather than falling back to a table lookup like the emulator does —
  which, as a side effect, correctly handles repeated/stacked `DD`/`FD`
  prefixes for free (each redundant prefix just adds 1 to the wrapping
  `length`, and whichever prefix byte is actually adjacent to the real
  opcode is the one whose special-case table gets consulted). Any byte
  pattern with no real decode (most of `0xED`'s space) falls back to `DB
  nnh` so the decoder never fails to make progress.
- `main.c` — two-pass driver, mirroring the assembler's pass structure in
  reverse: pass 1 is a worklist-driven reachability walk, seeded from
  `origin` (the CP/M `.com` entry point / ROM reset vector), that follows
  `decode_instruction()`'s own `ref_is_code` jump/call/`RST` targets as
  real code and every instruction's fall-through address (unless it's an
  unconditional terminator — `JP nn`/`JR e`/`RET`/`JP (HL)`/`(IX)`/`(IY)`/
  `RETI`/`RETN`, detected by peeking the raw opcode byte(s) directly,
  the same pattern `abc80/emu/src/step.c` uses to predict control flow
  ahead of execution), marking each reached address's status and
  collecting every referenced address into a label table (`Lxxxx` for
  code targets, `Dxxxx` for data — a data reference gets a label without
  being treated as an entry point to follow). Pass 2 walks the same
  address range linearly again and, per address, either decodes and
  prints an instruction (if pass 1 actually reached it as code) or prints
  a single labeled `DB nnh` byte (otherwise) — substituting a label name
  for the raw hex address in an instruction's formatted text wherever
  pass 1 found one there (string search for the hex literal
  `decode_instruction` already produced, not a re-formatting step). A
  label definition line (`Lxxxx:`/`Dxxxx:`) is emitted before any address
  that appears in the table.

**Known, standard limitation** (see `cpm/docs/ROADMAP.md` for the fuller
writeup): code reachable only via an indirect/computed jump (`JP (HL)`/
`(IX)`/`(IY)` with no static target) has no address for pass 1's traversal
to follow, so it comes out as `DB` data unless also reachable via a direct
`JP`/`CALL`/`JR` — a standard limitation of any reachability-based
disassembler, not something specific to this implementation.
`docs/Z80_REFERENCE.md` documents the opcode coverage in detail — this
disassembler targets real Z80 machine code generally, not just this
project's own emulator's current capability (the two are now aligned,
since `IN`/`OUT`, `IM`, `RETI`/`RETN`, `LD A,I` etc. are implemented on the
emulator side too, but that wasn't always true and isn't a given for any
future gap).

## GTK terminal (`cpm/gtk/src/`, work in progress)

`bin/z80-gtk` (built via the opt-in `make gtk`, never part of
`make`/`make test`) is a thin GTK4 launcher, not a terminal emulator of
its own: it spawns the real, unmodified `bin/z80` attached to a pty and
hands that pty to a `VteTerminal` widget, which does the actual
VT100/ANSI interpretation — `z80.c`/`cpm.c`/`cpm/emu/src/main.c` needed zero
changes for this, since `console_emit()`/`console_read_char()` already
just talk to stdin/stdout, and a pty's slave side *is* stdin/stdout from
the child's point of view. `main()` calls `lower_fd_limit()` before
touching GTK/VTE at all, capping `RLIMIT_NOFILE` down to 4096 — this
shell's default open-file limit is over a million, and VTE's
`vte_terminal_spawn_async()` closes every inherited fd below that
ceiling in the child before `exec` (glib's `fdwalk()` fallback, since
macOS has no `/proc/self/fd`), which overflowed the spawn thread's stack
at that size (confirmed via a real crash report:
`vte::base::SpawnContext::exec` → `fdwalk` → `__chkstk_darwin`,
"Thread stack size exceeded"). Still blocked, separately, by an
intermittent (~2-3% of launches, matching Apple's own reported rate)
crash inside `libsystem_malloc`'s new "xzone" allocator during
`posix_spawn()` of the large `bin/z80-gtk` binary itself, before any of
this project's own code runs — a confirmed macOS 26 OS bug (see Apple
Developer Forums thread 821081, "Sporadic crash in
xzm_main_malloc_zone_init_range_groups when spawning large binaries"),
not a Homebrew bottle mismatch as first suspected and not fixable from
application code — see `cpm/gtk/README.md` for the full diagnostic writeup
and `cpm/docs/ROADMAP.md`'s Phase 4 for status.

`bin/abc80-gtk` (`abc80/gtk/src/`, built via the opt-in `make abc80-gtk`)
is a *different* kind of GTK app for the ABC80 target — not a thin VTE
launcher like `bin/z80-gtk` above, but a real Cairo pixel framebuffer:
ABC80 has genuine bitmap GRAPHICS-mode graphics a terminal widget can't
address, which `bin/abc80`'s own `--interactive` mode approximates with
Unicode block characters (see `abc80/emu/src/render.c`'s own comment).
Runs the CPU core in-process via a `g_timeout_add()` batch loop rather
than spawning a child process, sharing `abc80_step()`
(`abc80/emu/src/step.h`) with `--interactive`'s own loop so the
per-instruction logic isn't duplicated. Single-threaded except for one
deliberately narrow exception: live SN76477 audio (SDL2) runs its own
real-time callback thread, handed the current sound register via a
single atomic byte rather than a queue or lock. Verified working via
real screenshots: the ROM's own sign-on banner and genuine GRAPHICS-mode
2×3 block-mosaic pixels both render correctly, real interactive keyboard
input reaches BASIC, `--quickload`/`--quicksave` round-trip
byte-identically against the CLI's own, and the live-audio tone's
frequency is independently verified via zero-crossing analysis — see
`abc80/gtk/README.md` and `abc80/docs/ABC80_ROADMAP.md`'s Milestone 11
for the full write-up (that milestone now has no open items).

