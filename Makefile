CC := gcc
# -MMD -MP makes the compiler emit a .d file per object listing the headers
# that object actually included, which the `-include` at the bottom of this
# file then feeds back to make. Without it, editing a header rebuilt
# nothing: a change to z80core/z80.h that added fields to `struct Z80` left
# cpm/emu/src/*.o compiled against the *old*, smaller struct while
# z80core/z80.o used the new one, so `Z80 cpu = {0}` allocated too little
# stack and every CP/M program silently produced no output at all. A real
# failure this actually caused, not a hypothetical - and one a plain
# `make clean` hid, which is what makes it worth tracking properly.
CFLAGS := -Wall -Wextra -O2 -MMD -MP

BIN_DIR := bin

# Shared Z80 core (z80.c/alu.c) - lives at the repo root rather than under
# cpm/, since it's genuinely machine-agnostic and used by two independent
# targets (bin/z80's CP/M layer and bin/abc80's ABC80 layer), not owned by
# either one - see CLAUDE.md's own project-structure paragraph.
Z80CORE_SRC_DIR := z80core
Z80CORE_SRCS := $(wildcard $(Z80CORE_SRC_DIR)/*.c)
Z80CORE_OBJS := $(Z80CORE_SRCS:.c=.o)

# Shared ABC-bus peripherals (abcbus/disk.c), at the repo root for exactly
# the same reason z80core/ is: the ABC bus is a bus, not a machine. Both
# the ABC80 and the ABC800 family carry it, both drive it with the same
# four-byte command header and the same status bits, and both targets link
# this one synthetic controller rather than each keeping its own - see
# CLAUDE.md's project-structure paragraph and abcbus/disk.h's own comment.
ABCBUS_SRC_DIR := abcbus
ABCBUS_OBJS := $(ABCBUS_SRC_DIR)/disk.o

EMU_SRC_DIR := cpm/emu/src
EMU_SRCS := $(wildcard $(EMU_SRC_DIR)/*.c)
EMU_OBJS := $(EMU_SRCS:.c=.o) $(Z80CORE_OBJS)
EMU_TARGET := $(BIN_DIR)/z80

ASM_SRC_DIR := asm/src
ASM_SRCS := $(wildcard $(ASM_SRC_DIR)/*.c)
ASM_OBJS := $(ASM_SRCS:.c=.o)
ASM_TARGET := $(BIN_DIR)/z80asm

DASM_SRC_DIR := disasm/src
DASM_SRCS := $(wildcard $(DASM_SRC_DIR)/*.c)
DASM_OBJS := $(DASM_SRCS:.c=.o)
DASM_TARGET := $(BIN_DIR)/z80dasm

# Luxor ABC80 machine target (Milestone 1: boots the real BASIC ROM on the
# shared core; Milestone 2 in progress: video generation - see
# abc80/docs/ABC80_ROADMAP.md). Two separate binaries live in
# $(ABC80_SRC_DIR), each with its own main() - not one wildcard-built
# binary like the other targets above, since chargen_dump is a standalone
# ROM-decode verification tool with no need for (or business linking) the
# CPU core at all.
ABC80_SRC_DIR := abc80/emu/src

# bin/abc80: reuses z80core/z80.o and alu.o directly rather than
# recompiling them under a different name - both are already
# machine-agnostic (z80_execute(), not the CP/M-specific z80_step()) and
# built with identical flags, so there's nothing to duplicate. Opt-in only
# (never part of `all`/`test`), same as `gtk` below - with no keyboard
# emulated yet, there's nothing test-suite-verifiable about it yet beyond
# a one-shot end-of-run render. Links render.o/charset.o/video_timing.o
# for that final render - not chargen.o, since render.c's terminal backend
# prints whole Unicode glyphs per cell rather than reconstructing pixels
# from the chargen ROM (see render.c's own top comment).
ABC80_OBJS := $(ABC80_SRC_DIR)/main.o $(ABC80_SRC_DIR)/render.o $(ABC80_SRC_DIR)/charset.o $(ABC80_SRC_DIR)/video_timing.o $(ABC80_SRC_DIR)/keyboard.o $(ABC80_SRC_DIR)/cassette.o $(ABC80_SRC_DIR)/sound.o $(ABC80_SRC_DIR)/abcbus.o $(ABC80_SRC_DIR)/step.o $(ABCBUS_OBJS) $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC80_TARGET := $(BIN_DIR)/abc80

# bin/abc802: the Luxor ABC802 machine target (abc802/docs/ABC802_ROADMAP.md).
# A third consumer of the same shared core, on the identical terms as
# bin/abc80 above - z80core/z80.o and alu.o are linked directly rather than
# rebuilt. Opt-in only (never part of `all`), same as `abc80`.
ABC802_SRC_DIR := abc802/emu/src
ABC802_OBJS := $(ABC802_SRC_DIR)/main.o $(ABC802_SRC_DIR)/memory.o $(ABC802_SRC_DIR)/ports.o $(ABC802_SRC_DIR)/render.o $(ABC802_SRC_DIR)/chargen.o $(ABC802_SRC_DIR)/png.o $(ABC802_SRC_DIR)/step.o $(ABCBUS_OBJS) $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC802_TARGET := $(BIN_DIR)/abc802

# bin/abc806: the Luxor ABC806 machine target, at milestone 1 - memory map
# and boot only, no video/keyboard/disk yet. A fourth consumer of the same
# shared core, on the terms abc80 and abc802 established. Opt-in, never
# part of `all`, same as the other machine targets.
ABC806_SRC_DIR := abc806/emu/src
ABC806_OBJS := $(ABC806_SRC_DIR)/main.o $(ABC806_SRC_DIR)/memory.o $(ABC806_SRC_DIR)/ports.o $(ABC806_SRC_DIR)/chargen.o $(ABC806_SRC_DIR)/png.o $(ABC806_SRC_DIR)/render.o $(ABC806_SRC_DIR)/text.o $(ABC806_SRC_DIR)/rtc.o $(ABC806_SRC_DIR)/step.o $(ABCBUS_OBJS) $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC806_TARGET := $(BIN_DIR)/abc806

# bin/abc806-chargen-dump: the verification tool for chargen.c *and*
# text.c - a synthetic screen exercising every attribute path, with no CPU
# core. Not optional cover: this machine's boot screen is white on black
# and uses one attribute, so both the pixel decode and the terminal
# renderer's eight colours would look perfect on it while being badly
# broken. text.o links here precisely because it is pure; render.o, which
# reaches for the live machine, deliberately does not.
ABC806_CHARGEN_DUMP_OBJS := $(ABC806_SRC_DIR)/chargen_dump.o $(ABC806_SRC_DIR)/chargen.o $(ABC806_SRC_DIR)/text.o $(ABC806_SRC_DIR)/png.o
ABC806_CHARGEN_DUMP_TARGET := $(BIN_DIR)/abc806-chargen-dump

# bin/abc806-gtk: a GTK4 window for the ABC806. Opt-in (`make abc806-gtk`),
# never part of `make`/`make test`, on the same terms as the other two.
#
# Shorter than either predecessor for a reason worth keeping: the pixel
# decode is already a pure function verified by bin/abc806-chargen-dump, and
# abc806_step() is already shared with the CLI's --interactive loop, so this
# app only turns palette indices into a Cairo surface. Needs gtk4 alone - no
# SDL2 and no threads, since this machine's only sound is a strobe the
# emulator does not sound.
ABC806_GTK_SRC_DIR := abc806/gtk/src
ABC806_GTK_SRCS := $(wildcard $(ABC806_GTK_SRC_DIR)/*.c)
ABC806_GTK_OBJS := $(ABC806_GTK_SRCS:.c=.o) $(ABC806_SRC_DIR)/memory.o $(ABC806_SRC_DIR)/ports.o $(ABC806_SRC_DIR)/render.o $(ABC806_SRC_DIR)/text.o $(ABC806_SRC_DIR)/chargen.o $(ABC806_SRC_DIR)/rtc.o $(ABC806_SRC_DIR)/step.o $(ABCBUS_OBJS) $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC806_GTK_TARGET := $(BIN_DIR)/abc806-gtk
ABC806_GTK_PKGS := gtk4
ABC806_GTK_CFLAGS := $(shell pkg-config --cflags $(ABC806_GTK_PKGS) 2>/dev/null)
ABC806_GTK_LIBS := $(shell pkg-config --libs $(ABC806_GTK_PKGS) 2>/dev/null)

# bin/abcdisk: creates and inspects ABC-bus floppy images. Lives beside
# abcbus/disk.c for that file's own reason - the bus is not a machine, and
# both ABC targets mount the same media - and needs no CPU core, since it
# only writes a filesystem. Part of `all`: unlike the chargen/timing dump
# tools below, this one is a user-facing utility rather than a decode
# check, and a formatted blank disk is otherwise unobtainable (neither ROM
# has a FORMAT command).
ABCDISK_OBJS := $(ABCBUS_SRC_DIR)/mkdisk.o
ABCDISK_TARGET := $(BIN_DIR)/abcdisk

# bin/abc80-chargen-dump: the chargen.c decode-verification tool (see its
# own top comment) - no CPU core needed, just the ROM decode itself.
ABC80_CHARGEN_DUMP_OBJS := $(ABC80_SRC_DIR)/chargen_dump.o $(ABC80_SRC_DIR)/chargen.o
ABC80_CHARGEN_DUMP_TARGET := $(BIN_DIR)/abc80-chargen-dump

# bin/abc802-chargen-dump: chargen.c's decode-verification tool. Same role
# and same reasoning as abc80-chargen-dump above - no CPU core, just the
# ROM decode fed a synthetic screen - and it earns its place because the
# row attributes it exercises are precisely what a real ROM boot screen
# never uses, so `--screenshot` alone could not catch a broken one.
ABC802_CHARGEN_DUMP_OBJS := $(ABC802_SRC_DIR)/chargen_dump.o $(ABC802_SRC_DIR)/chargen.o $(ABC802_SRC_DIR)/png.o
ABC802_CHARGEN_DUMP_TARGET := $(BIN_DIR)/abc802-chargen-dump

# bin/abc80-video-timing-dump: the video_timing.c PROM-decode/address-
# mapping verification tool (see its own top comment) - same reasoning as
# chargen_dump above.
ABC80_VIDEO_TIMING_DUMP_OBJS := $(ABC80_SRC_DIR)/video_timing_dump.o $(ABC80_SRC_DIR)/video_timing.o
ABC80_VIDEO_TIMING_DUMP_TARGET := $(BIN_DIR)/abc80-video-timing-dump

# bin/abc80-render-demo: render.c's verification tool (see its own top
# comment) - renders known synthetic video RAM, not a real CPU run, so it
# only needs video_timing.o (for abc80_videoram_addr()/abc80_attr_lookup())
# alongside render.o/charset.o, not the CPU core.
ABC80_RENDER_DEMO_OBJS := $(ABC80_SRC_DIR)/render_demo.o $(ABC80_SRC_DIR)/render.o $(ABC80_SRC_DIR)/charset.o $(ABC80_SRC_DIR)/video_timing.o
ABC80_RENDER_DEMO_TARGET := $(BIN_DIR)/abc80-render-demo

# bin/abc80-sound-demo: sound.c's verification tool (see its own top
# comment) - feeds a known synthetic register-event sequence, no CPU
# core needed. Links -lm for sound.c's fmod() call.
ABC80_SOUND_DEMO_OBJS := $(ABC80_SRC_DIR)/sound_demo.o $(ABC80_SRC_DIR)/sound.o
ABC80_SOUND_DEMO_TARGET := $(BIN_DIR)/abc80-sound-demo

# z80.c's own interrupt-acceptance unit test (cpm/tests/test_interrupts.c) -
# a direct C-level test, not a .asm program run through run_tests.sh like
# every other regression check here, since no CP/M-executable instruction
# can raise an interrupt against itself (INT/NMI are host-side signals on
# real hardware) - see that file's own top comment. Built from the real
# $(Z80CORE_SRC_DIR)/z80.c/alu.c plus $(EMU_SRC_DIR)/cpm.c, not a separate
# reimplementation.
EMU_TEST_INTERRUPTS_TARGET := $(BIN_DIR)/z80-test-interrupts

# Opt-in only (never part of `all`/`test`) - the only build target with an
# external dependency beyond a bare C compiler. A thin GTK4+VTE launcher
# for the real bin/z80, not a separate emulator - see cpm/gtk/src/main.c's
# own comment for why.
GTK_SRC_DIR := cpm/gtk/src
GTK_SRCS := $(wildcard $(GTK_SRC_DIR)/*.c)
GTK_OBJS := $(GTK_SRCS:.c=.o)
GTK_TARGET := $(BIN_DIR)/z80-gtk
GTK_PKGS := gtk4 vte-2.91-gtk4
GTK_CFLAGS := $(shell pkg-config --cflags $(GTK_PKGS) 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs $(GTK_PKGS) 2>/dev/null)

# bin/abc80-gtk: a real GTK4 window for ABC80 (Milestone 11, see
# abc80/docs/ABC80_ROADMAP.md) - a genuine Cairo pixel framebuffer, not a
# VTE-terminal thin launcher like cpm/gtk/ above (ABC80 has real bitmap
# GRAPHICS-mode graphics a terminal widget can't address). Opt-in only,
# same as `gtk`/`abc80` - never part of `all`/`test`. Needs `gtk4` (no
# VTE - this target never spawns a child process) and `sdl2` (Milestone
# 11's live-audio callback - the only real dependency reason, `bin/abc80`
# itself stays SDL2-free and keeps its own batch `--wav` renderer as-is).
# Links the shared core plus the ABC80 modules abc80/gtk/src/main.c
# itself needs (video_timing.o/chargen.o for pixel decode, keyboard.o/
# disk.o/step.o for the shared CPU step, sound.o for both abc80_step()'s
# signature and the live-audio sample generator, charset.o for the
# TEXT-mode character decode the plain-text .bas Save/Load path reuses
# from render.c's own already-verified table) - no render.o, since this
# target has its own Cairo renderer instead of render.c's terminal-glyph
# one.
# bin/abc802-gtk: a GTK4 window for the ABC802 (ABC802_ROADMAP.md's
# Milestone 4). Like bin/abc80-gtk it is a real Cairo framebuffer running
# the core in-process, not a VTE launcher - but it needs no SDL2, since
# this machine's only sound is a speaker strobe the emulator does not
# sound. Links the emulator modules it actually uses: chargen.o for the
# pixel decode (the same pure function --screenshot and
# abc802-chargen-dump verify), render.o for the charset/cursor helpers,
# and step.o for the shared per-instruction logic. Opt-in only, never part
# of `all`/`test`.
ABC802_GTK_SRC_DIR := abc802/gtk/src
ABC802_GTK_SRCS := $(wildcard $(ABC802_GTK_SRC_DIR)/*.c)
ABC802_GTK_OBJS := $(ABC802_GTK_SRCS:.c=.o) $(ABC802_SRC_DIR)/memory.o $(ABC802_SRC_DIR)/ports.o $(ABC802_SRC_DIR)/render.o $(ABC802_SRC_DIR)/chargen.o $(ABC802_SRC_DIR)/step.o $(ABCBUS_OBJS) $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC802_GTK_TARGET := $(BIN_DIR)/abc802-gtk
ABC802_GTK_PKGS := gtk4
ABC802_GTK_CFLAGS := $(shell pkg-config --cflags $(ABC802_GTK_PKGS) 2>/dev/null)
ABC802_GTK_LIBS := $(shell pkg-config --libs $(ABC802_GTK_PKGS) 2>/dev/null)

ABC80_GTK_SRC_DIR := abc80/gtk/src
ABC80_GTK_SRCS := $(wildcard $(ABC80_GTK_SRC_DIR)/*.c)
ABC80_GTK_OBJS := $(ABC80_GTK_SRCS:.c=.o) $(ABC80_SRC_DIR)/video_timing.o $(ABC80_SRC_DIR)/chargen.o $(ABC80_SRC_DIR)/keyboard.o $(ABC80_SRC_DIR)/abcbus.o $(ABC80_SRC_DIR)/step.o $(ABCBUS_OBJS) $(ABC80_SRC_DIR)/sound.o $(ABC80_SRC_DIR)/cassette.o $(ABC80_SRC_DIR)/charset.o $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC80_GTK_TARGET := $(BIN_DIR)/abc80-gtk
ABC80_GTK_PKGS := gtk4 sdl2
ABC80_GTK_CFLAGS := $(shell pkg-config --cflags $(ABC80_GTK_PKGS) 2>/dev/null)
ABC80_GTK_LIBS := $(shell pkg-config --libs $(ABC80_GTK_PKGS) 2>/dev/null)

.PHONY: all emulator assembler disassembler abcdisk gtk abc80 abc802 abc806 abc806-chargen-dump abc802-chargen-dump abc806-gtk abc802-gtk abc80-gtk abc80-chargen-dump abc80-video-timing-dump abc80-render-demo abc80-sound-demo run test test-cpm test-abc80 test-abc802 test-abc806 clean

all: emulator assembler disassembler abcdisk

emulator: $(EMU_TARGET)

assembler: $(ASM_TARGET)

disassembler: $(DASM_TARGET)

abcdisk: $(ABCDISK_TARGET)

gtk: $(GTK_TARGET)

abc80: $(ABC80_TARGET)

abc802: $(ABC802_TARGET)

abc806: $(ABC806_TARGET)

abc806-chargen-dump: $(ABC806_CHARGEN_DUMP_TARGET)

abc802-chargen-dump: $(ABC802_CHARGEN_DUMP_TARGET)

abc806-gtk: $(ABC806_GTK_TARGET)

abc802-gtk: $(ABC802_GTK_TARGET)

abc80-chargen-dump: $(ABC80_CHARGEN_DUMP_TARGET)

abc80-video-timing-dump: $(ABC80_VIDEO_TIMING_DUMP_TARGET)

abc80-render-demo: $(ABC80_RENDER_DEMO_TARGET)

abc80-sound-demo: $(ABC80_SOUND_DEMO_TARGET)

abc80-gtk: $(ABC80_GTK_TARGET)

$(EMU_TARGET): $(EMU_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(EMU_OBJS)

$(ASM_TARGET): $(ASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ASM_OBJS)

$(DASM_TARGET): $(DASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(DASM_OBJS)

$(ABC80_TARGET): $(ABC80_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC80_OBJS) -lm

$(ABC802_TARGET): $(ABC802_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC802_OBJS)

$(ABC806_TARGET): $(ABC806_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC806_OBJS)

$(ABC806_CHARGEN_DUMP_TARGET): $(ABC806_CHARGEN_DUMP_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC806_CHARGEN_DUMP_OBJS)

$(ABCDISK_TARGET): $(ABCDISK_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABCDISK_OBJS)

$(ABC802_CHARGEN_DUMP_TARGET): $(ABC802_CHARGEN_DUMP_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC802_CHARGEN_DUMP_OBJS)

$(ABC80_CHARGEN_DUMP_TARGET): $(ABC80_CHARGEN_DUMP_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC80_CHARGEN_DUMP_OBJS)

$(ABC80_VIDEO_TIMING_DUMP_TARGET): $(ABC80_VIDEO_TIMING_DUMP_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC80_VIDEO_TIMING_DUMP_OBJS)

$(ABC80_RENDER_DEMO_TARGET): $(ABC80_RENDER_DEMO_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC80_RENDER_DEMO_OBJS)

$(ABC80_SOUND_DEMO_TARGET): $(ABC80_SOUND_DEMO_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC80_SOUND_DEMO_OBJS) -lm

$(EMU_TEST_INTERRUPTS_TARGET): cpm/tests/test_interrupts.c $(Z80CORE_SRC_DIR)/z80.c $(Z80CORE_SRC_DIR)/alu.c $(EMU_SRC_DIR)/cpm.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cpm/tests/test_interrupts.c $(Z80CORE_SRC_DIR)/z80.c $(Z80CORE_SRC_DIR)/alu.c $(EMU_SRC_DIR)/cpm.c

$(GTK_TARGET): $(GTK_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GTK_OBJS) $(GTK_LIBS)

$(ABC80_GTK_TARGET): $(ABC80_GTK_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC80_GTK_OBJS) $(ABC80_GTK_LIBS)

$(ABC806_GTK_TARGET): $(ABC806_GTK_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC806_GTK_OBJS) $(ABC806_GTK_LIBS)

$(ABC802_GTK_TARGET): $(ABC802_GTK_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC802_GTK_OBJS) $(ABC802_GTK_LIBS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(GTK_SRC_DIR)/%.o: $(GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

$(ABC80_GTK_SRC_DIR)/%.o: $(ABC80_GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(ABC80_GTK_CFLAGS) -c $< -o $@

$(ABC806_GTK_SRC_DIR)/%.o: $(ABC806_GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(ABC806_GTK_CFLAGS) -c $< -o $@

$(ABC802_GTK_SRC_DIR)/%.o: $(ABC802_GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(ABC802_GTK_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: emulator
	./$(EMU_TARGET) cpm/emu/zexall/ZEXALL-main/zexall.com | less

# `make test` covers every target that builds with no external
# dependencies - which is all of them except the two GTK apps. The machine
# targets' suites drive the real ROMs; their floppy checks need real disk
# images this repo does not commit, and skip loudly without them (see each
# script's own header for the environment variable to set).
test: test-cpm test-abc80 test-abc802 test-abc806

test-cpm: emulator assembler disassembler $(EMU_TEST_INTERRUPTS_TARGET)
	./cpm/tests/run_tests.sh

test-abc80: abc80 abc80-video-timing-dump abc80-chargen-dump abc80-sound-demo
	./abc80/tests/run_tests.sh

test-abc806: abc806 abc806-chargen-dump
	./abc806/tests/run_tests.sh

test-abc802: abc802 abc802-chargen-dump
	./abc802/tests/run_tests.sh

# Every .d generated by -MMD above, so the -include below never has to
# guess at names.
DEPS := $(EMU_OBJS:.o=.d) $(ASM_OBJS:.o=.d) $(DASM_OBJS:.o=.d) $(GTK_OBJS:.o=.d) $(ABC80_OBJS:.o=.d) $(ABC802_OBJS:.o=.d) $(ABCBUS_OBJS:.o=.d) $(ABCDISK_OBJS:.o=.d) $(ABC806_OBJS:.o=.d) $(ABC806_CHARGEN_DUMP_OBJS:.o=.d) $(ABC80_GTK_OBJS:.o=.d) $(ABC80_CHARGEN_DUMP_OBJS:.o=.d) $(ABC80_VIDEO_TIMING_DUMP_OBJS:.o=.d) $(ABC80_RENDER_DEMO_OBJS:.o=.d) $(ABC80_SOUND_DEMO_OBJS:.o=.d)

clean:
	rm -f $(DEPS) $(EMU_OBJS) $(ASM_OBJS) $(DASM_OBJS) $(GTK_OBJS) $(ABC80_OBJS) $(ABC80_GTK_OBJS) $(ABC80_CHARGEN_DUMP_OBJS) $(ABC80_VIDEO_TIMING_DUMP_OBJS) $(ABC80_RENDER_DEMO_OBJS) $(ABC80_SOUND_DEMO_OBJS) $(ABC802_OBJS) $(ABC806_OBJS) $(ABC806_CHARGEN_DUMP_OBJS) $(ABC806_CHARGEN_DUMP_TARGET) $(ABC806_GTK_OBJS) $(ABC806_GTK_TARGET) $(ABC802_GTK_OBJS) $(ABC802_GTK_TARGET) $(ABC802_CHARGEN_DUMP_OBJS) $(ABC802_CHARGEN_DUMP_TARGET) $(ABCBUS_OBJS) $(ABCDISK_OBJS) $(ABCDISK_TARGET) $(ABC806_TARGET) $(ABC802_TARGET) $(EMU_TARGET) $(ASM_TARGET) $(DASM_TARGET) $(GTK_TARGET) $(ABC80_TARGET) $(ABC80_GTK_TARGET) $(ABC80_CHARGEN_DUMP_TARGET) $(ABC80_VIDEO_TIMING_DUMP_TARGET) $(ABC80_RENDER_DEMO_TARGET) $(ABC80_SOUND_DEMO_TARGET) $(EMU_TEST_INTERRUPTS_TARGET)

# Header dependencies recorded by -MMD (see CFLAGS above). Leading `-` so a
# clean tree with no .d files yet is not an error.
-include $(DEPS)
