CC := gcc
CFLAGS := -Wall -Wextra -O2

BIN_DIR := bin

# Shared Z80 core (z80.c/alu.c) - lives at the repo root rather than under
# cpm/, since it's genuinely machine-agnostic and used by two independent
# targets (bin/z80's CP/M layer and bin/abc80's ABC80 layer), not owned by
# either one - see CLAUDE.md's own project-structure paragraph.
Z80CORE_SRC_DIR := z80core
Z80CORE_SRCS := $(wildcard $(Z80CORE_SRC_DIR)/*.c)
Z80CORE_OBJS := $(Z80CORE_SRCS:.c=.o)

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
ABC80_OBJS := $(ABC80_SRC_DIR)/main.o $(ABC80_SRC_DIR)/render.o $(ABC80_SRC_DIR)/charset.o $(ABC80_SRC_DIR)/video_timing.o $(ABC80_SRC_DIR)/keyboard.o $(ABC80_SRC_DIR)/cassette.o $(ABC80_SRC_DIR)/sound.o $(ABC80_SRC_DIR)/disk.o $(ABC80_SRC_DIR)/step.o $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC80_TARGET := $(BIN_DIR)/abc80

# bin/abc80-chargen-dump: the chargen.c decode-verification tool (see its
# own top comment) - no CPU core needed, just the ROM decode itself.
ABC80_CHARGEN_DUMP_OBJS := $(ABC80_SRC_DIR)/chargen_dump.o $(ABC80_SRC_DIR)/chargen.o
ABC80_CHARGEN_DUMP_TARGET := $(BIN_DIR)/abc80-chargen-dump

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
ABC80_GTK_SRC_DIR := abc80/gtk/src
ABC80_GTK_SRCS := $(wildcard $(ABC80_GTK_SRC_DIR)/*.c)
ABC80_GTK_OBJS := $(ABC80_GTK_SRCS:.c=.o) $(ABC80_SRC_DIR)/video_timing.o $(ABC80_SRC_DIR)/chargen.o $(ABC80_SRC_DIR)/keyboard.o $(ABC80_SRC_DIR)/disk.o $(ABC80_SRC_DIR)/step.o $(ABC80_SRC_DIR)/sound.o $(ABC80_SRC_DIR)/cassette.o $(ABC80_SRC_DIR)/charset.o $(Z80CORE_SRC_DIR)/z80.o $(Z80CORE_SRC_DIR)/alu.o
ABC80_GTK_TARGET := $(BIN_DIR)/abc80-gtk
ABC80_GTK_PKGS := gtk4 sdl2
ABC80_GTK_CFLAGS := $(shell pkg-config --cflags $(ABC80_GTK_PKGS) 2>/dev/null)
ABC80_GTK_LIBS := $(shell pkg-config --libs $(ABC80_GTK_PKGS) 2>/dev/null)

.PHONY: all emulator assembler disassembler gtk abc80 abc80-gtk abc80-chargen-dump abc80-video-timing-dump abc80-render-demo abc80-sound-demo run test clean

all: emulator assembler disassembler

emulator: $(EMU_TARGET)

assembler: $(ASM_TARGET)

disassembler: $(DASM_TARGET)

gtk: $(GTK_TARGET)

abc80: $(ABC80_TARGET)

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

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(GTK_SRC_DIR)/%.o: $(GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

$(ABC80_GTK_SRC_DIR)/%.o: $(ABC80_GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(ABC80_GTK_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: emulator
	./$(EMU_TARGET) cpm/emu/zexall/ZEXALL-main/zexall.com | less

test: emulator assembler disassembler $(EMU_TEST_INTERRUPTS_TARGET)
	./cpm/tests/run_tests.sh

clean:
	rm -f $(EMU_OBJS) $(ASM_OBJS) $(DASM_OBJS) $(GTK_OBJS) $(ABC80_OBJS) $(ABC80_GTK_OBJS) $(ABC80_CHARGEN_DUMP_OBJS) $(ABC80_VIDEO_TIMING_DUMP_OBJS) $(ABC80_RENDER_DEMO_OBJS) $(ABC80_SOUND_DEMO_OBJS) $(EMU_TARGET) $(ASM_TARGET) $(DASM_TARGET) $(GTK_TARGET) $(ABC80_TARGET) $(ABC80_GTK_TARGET) $(ABC80_CHARGEN_DUMP_TARGET) $(ABC80_VIDEO_TIMING_DUMP_TARGET) $(ABC80_RENDER_DEMO_TARGET) $(ABC80_SOUND_DEMO_TARGET) $(EMU_TEST_INTERRUPTS_TARGET)
