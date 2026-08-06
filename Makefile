CC := gcc
CFLAGS := -Wall -Wextra -O2

BIN_DIR := bin

EMU_SRC_DIR := cpm/emu/src
EMU_SRCS := $(wildcard $(EMU_SRC_DIR)/*.c)
EMU_OBJS := $(EMU_SRCS:.c=.o)
EMU_TARGET := $(BIN_DIR)/z80

ASM_SRC_DIR := cpm/asm/src
ASM_SRCS := $(wildcard $(ASM_SRC_DIR)/*.c)
ASM_OBJS := $(ASM_SRCS:.c=.o)
ASM_TARGET := $(BIN_DIR)/z80asm

DASM_SRC_DIR := cpm/disasm/src
DASM_SRCS := $(wildcard $(DASM_SRC_DIR)/*.c)
DASM_OBJS := $(DASM_SRCS:.c=.o)
DASM_TARGET := $(BIN_DIR)/z80dasm

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

# Opt-in only (never part of `all`/`test`), same reasoning as GTK above
# but for a different reason: this is a brand new, separate emulator
# (see gameboy/docs/GAMEBOY_ROADMAP.md), still too early to fold into the
# default build/test - revisit once it can run real games end to end.
GAMEBOY_SRC_DIR := gameboy/src
GAMEBOY_SRCS := $(wildcard $(GAMEBOY_SRC_DIR)/*.c)
GAMEBOY_OBJS := $(GAMEBOY_SRCS:.c=.o)
GAMEBOY_TARGET := $(BIN_DIR)/gameboy

# cart.c's own unit tests (gameboy/tests/test_cart.c) - unlike Blargg's
# cpu_instrs (fetched locally, never committed - see
# gameboy/docs/GAMEBOY_ROADMAP.md's licensing note), this is this project's own
# code with no licensing question, so it's a real `make`-able regression
# gate for the MBC1/MBC3/MBC5 banking logic despite no real MBC test ROM
# being available to commit.
GAMEBOY_TEST_TARGET := $(BIN_DIR)/gameboy-test-cart

# timer.c's own unit tests (gameboy/tests/test_timer.c), same reasoning
# as GAMEBOY_TEST_TARGET above but for the DIV/TAC-write "spurious
# tick" quirks and the TIMA overflow-reload delay - obscure enough to
# be worth a direct, ROM-independent check even though Blargg's
# instr_timing.gb (used locally, not committed) already exercises this
# timer broadly.
GAMEBOY_TEST_TIMER_TARGET := $(BIN_DIR)/gameboy-test-timer

# apu.c's own unit tests (gameboy/tests/test_apu.c) - "zombie mode"
# volume-nudge writes, a real obscure DMG APU behavior a real ROM
# (gameboy/test_roms/droneboy/) turned out to depend on. gb_apu_write()
# needs no GBCpu/mmu stub at all, same ROM-independent reasoning as
# GAMEBOY_TEST_TARGET/GAMEBOY_TEST_TIMER_TARGET above.
GAMEBOY_TEST_APU_TARGET := $(BIN_DIR)/gameboy-test-apu

# cpu.c's own unit tests (gameboy/tests/test_cpu.c) - the "HALT
# immediately after EI" sub-case of the HALT bug, a real gap a real ROM
# (gameboy/test_roms/tobutobugirl/) found. Needs mmu.c linked (for
# gb_read_byte/gb_write_byte) but no cart/ppu/timer/joypad/apu, same
# minimal-dependency reasoning as GAMEBOY_TEST_APU_TARGET above.
GAMEBOY_TEST_CPU_TARGET := $(BIN_DIR)/gameboy-test-cpu

# dmg-acid2 (gameboy/test_roms/dmg-acid2/ - MIT-licensed, committed
# unlike Blargg's ROMs) is the PPU's real correctness gate: render a
# frame, compare it pixel-for-pixel against the reference image.
# Informational against a regression floor rather than a hard 100%
# pass/fail - see gameboy/docs/GAMEBOY_ROADMAP.md's Phase 4 status for the
# current match rate and its still-open remaining gap, and
# compare_frame.py's own comment for the regression-baseline reasoning.
GAMEBOY_VISUAL_ROM := gameboy/test_roms/dmg-acid2/dmg-acid2.gb
GAMEBOY_VISUAL_REF := gameboy/test_roms/dmg-acid2/reference-dmg.png
GAMEBOY_VISUAL_OUT := $(BIN_DIR)/dmg-acid2-output.ppm

# 2048-gb (gameboy/test_roms/2048-gb/ - zlib-licensed, committed same as
# dmg-acid2) is Phase 6's real-game validation target: a genuine,
# unmodified third-party homebrew game, scripted (via main.c's --input)
# through starting a game and playing far enough to trigger a real tile
# merge, then diffed byte-for-byte against a known-good captured frame -
# see gameboy/test_roms/2048-gb/README.md for the full story, including a
# real cartridge-loading bug this ROM found and fixed.
GAMEBOY_2048_ROM := gameboy/test_roms/2048-gb/2048.gb
GAMEBOY_2048_SCRIPT := gameboy/test_roms/2048-gb/input_script.txt
GAMEBOY_2048_REF := gameboy/test_roms/2048-gb/reference_frame.ppm
GAMEBOY_2048_OUT := $(BIN_DIR)/2048-gb-output.ppm

# Droneboy (gameboy/test_roms/droneboy/ - MIT-licensed, committed same as
# 2048-gb) is the live-audio counterpart to dmg-acid2's PPU test: real,
# sustained multi-channel sound from boot with no input needed, unlike
# 2048-gb's single startup blip - see gameboy/test_roms/droneboy/README.md.
GAMEBOY_DRONEBOY_ROM := gameboy/test_roms/droneboy/droneboy.gb
GAMEBOY_DRONEBOY_REF := gameboy/test_roms/droneboy/reference_audio.wav
GAMEBOY_DRONEBOY_OUT := $(BIN_DIR)/droneboy-output.wav

# Tobu Tobu Girl (gameboy/test_roms/tobutobugirl/ - MIT-licensed, committed
# same as 2048-gb) is a second real-game validation target: a well-known
# action/platformer rather than a puzzle game - see
# gameboy/test_roms/tobutobugirl/README.md.
GAMEBOY_TOBU_ROM := gameboy/test_roms/tobutobugirl/tobu.gb
GAMEBOY_TOBU_REF := gameboy/test_roms/tobutobugirl/reference_frame.ppm
GAMEBOY_TOBU_OUT := $(BIN_DIR)/tobu-output.ppm

# RGBDS (rgbasm/rgblink/rgbfix, `brew install rgbds`) - opt-in, same
# external-dependency reasoning as GTK4 above. The chosen toolchain for
# any future custom Game Boy test content, rather than building/
# extending a homegrown assembler for a second, very different CPU
# target: RGBDS is already the de facto standard the whole GB homebrew
# scene uses (2048-gb, Tobu Tobu Girl, and Droneboy - see
# gameboy/test_roms/ - are all built with RGBDS or GBDK, which itself
# builds on RGBDS's assembler), so adopting it costs nothing against
# real effort extending cpm/asm/src for a second instruction set. See
# gameboy/rgbds/examples/hello.asm and gameboy/docs/GAMEBOY_ROADMAP.md's
# Phase 7 status for the full reasoning.
RGBDS_HELLO_SRC := gameboy/rgbds/examples/hello.asm
RGBDS_HELLO_OBJ := $(BIN_DIR)/rgbds-hello.o
RGBDS_HELLO_ROM := $(BIN_DIR)/rgbds-hello.gb

# MBC3's real-time clock, driven through the actual memory-mapped
# interface (bank-select, latch sequence, the shared $A000 window) a
# real MBC3+RTC game would use - see
# gameboy/rgbds/examples/mbc3_rtc.asm's own top comment for why this is
# worth having alongside test_cart.c's synthetic-struct RTC checks, and
# gameboy/docs/GAMEBOY_ROADMAP.md's Phase 6 status for the gap this closes.
RGBDS_MBC3_RTC_SRC := gameboy/rgbds/examples/mbc3_rtc.asm
RGBDS_MBC3_RTC_OBJ := $(BIN_DIR)/rgbds-mbc3-rtc.o
RGBDS_MBC3_RTC_ROM := $(BIN_DIR)/rgbds-mbc3-rtc.gb

# Phase 7's real front end (gameboy/gtk/src/main.c) - opt-in, same GTK4
# dependency/reasoning as GTK above, but links the core directly instead
# of spawning a separate process (see main.c's own top comment for why
# cpm/gtk's spawn-and-hand-a-pty-to-VTE approach doesn't transfer here).
# Built from the core sources directly rather than $(GAMEBOY_OBJS),
# since that includes gameboy/src/main.c's own competing main().
GAMEBOY_CORE_SRCS := $(filter-out $(GAMEBOY_SRC_DIR)/main.c,$(GAMEBOY_SRCS))
GAMEBOY_CORE_OBJS := $(GAMEBOY_CORE_SRCS:.c=.o)
GAMEBOY_GTK_SRC_DIR := gameboy/gtk/src
GAMEBOY_GTK_SRCS := $(wildcard $(GAMEBOY_GTK_SRC_DIR)/*.c)
GAMEBOY_GTK_OBJS := $(GAMEBOY_GTK_SRCS:.c=.o)
GAMEBOY_GTK_TARGET := $(BIN_DIR)/gameboy-gtk
GAMEBOY_GTK_CFLAGS := $(GTK_CFLAGS) -I$(GAMEBOY_SRC_DIR)
# -framework AudioToolbox: live audio via CoreAudio's AudioQueue (see
# gtk/src/main.c's own comment for why CoreAudio specifically, not a
# portable library) - a macOS system framework, no brew/pkg-config
# dependency needed.
GAMEBOY_GTK_LIBS := $(GTK_LIBS) -framework AudioToolbox

.PHONY: all emulator assembler disassembler gtk gameboy gameboy-test gameboy-visual-test gameboy-2048-test gameboy-droneboy-test gameboy-tobu-test gameboy-rgbds-test gameboy-rgbds-mbc3-test gameboy-gtk run test clean

all: emulator assembler disassembler

emulator: $(EMU_TARGET)

assembler: $(ASM_TARGET)

disassembler: $(DASM_TARGET)

gtk: $(GTK_TARGET)

gameboy: $(GAMEBOY_TARGET)

gameboy-test: $(GAMEBOY_TEST_TARGET) $(GAMEBOY_TEST_TIMER_TARGET) $(GAMEBOY_TEST_APU_TARGET) $(GAMEBOY_TEST_CPU_TARGET)
	./$(GAMEBOY_TEST_TARGET)
	./$(GAMEBOY_TEST_TIMER_TARGET)
	./$(GAMEBOY_TEST_APU_TARGET)
	./$(GAMEBOY_TEST_CPU_TARGET)

gameboy-visual-test: $(GAMEBOY_TARGET)
	./$(GAMEBOY_TARGET) $(GAMEBOY_VISUAL_ROM) --ppm $(GAMEBOY_VISUAL_OUT) --frames 2
	python3 gameboy/tests/compare_frame.py $(GAMEBOY_VISUAL_OUT) $(GAMEBOY_VISUAL_REF)

gameboy-2048-test: $(GAMEBOY_TARGET)
	./$(GAMEBOY_TARGET) $(GAMEBOY_2048_ROM) --input $(GAMEBOY_2048_SCRIPT) --ppm $(GAMEBOY_2048_OUT) --frames 180
	cmp $(GAMEBOY_2048_OUT) $(GAMEBOY_2048_REF) && echo "gameboy-2048-test: OK (frame matches known-good reference)"

gameboy-droneboy-test: $(GAMEBOY_TARGET)
	./$(GAMEBOY_TARGET) $(GAMEBOY_DRONEBOY_ROM) --wav $(GAMEBOY_DRONEBOY_OUT) --seconds 2
	cmp $(GAMEBOY_DRONEBOY_OUT) $(GAMEBOY_DRONEBOY_REF) && echo "gameboy-droneboy-test: OK (audio matches known-good reference)"

gameboy-tobu-test: $(GAMEBOY_TARGET)
	./$(GAMEBOY_TARGET) $(GAMEBOY_TOBU_ROM) --ppm $(GAMEBOY_TOBU_OUT) --frames 60
	cmp $(GAMEBOY_TOBU_OUT) $(GAMEBOY_TOBU_REF) && echo "gameboy-tobu-test: OK (frame matches known-good reference)"

gameboy-rgbds-test: $(GAMEBOY_TARGET) | $(BIN_DIR)
	rgbasm -o $(RGBDS_HELLO_OBJ) $(RGBDS_HELLO_SRC)
	rgblink -o $(RGBDS_HELLO_ROM) $(RGBDS_HELLO_OBJ)
	rgbfix -v -p 0xFF $(RGBDS_HELLO_ROM)
	./$(GAMEBOY_TARGET) $(RGBDS_HELLO_ROM) 2>&1 | grep -q "HELLO GAMEBOY" \
		&& echo "gameboy-rgbds-test: OK (RGBDS-built ROM ran correctly)" \
		|| (echo "gameboy-rgbds-test: FAIL (expected serial output not seen)"; exit 1)

gameboy-rgbds-mbc3-test: $(GAMEBOY_TARGET) | $(BIN_DIR)
	rgbasm -o $(RGBDS_MBC3_RTC_OBJ) $(RGBDS_MBC3_RTC_SRC)
	rgblink -o $(RGBDS_MBC3_RTC_ROM) $(RGBDS_MBC3_RTC_OBJ)
	rgbfix -v -m 0x10 -r 0x03 -p 0xFF $(RGBDS_MBC3_RTC_ROM)
	./$(GAMEBOY_TARGET) $(RGBDS_MBC3_RTC_ROM) 2>&1 | \
		grep -q "RAM:Rr RTC1:ABCDE RTC2(unlatched):ABCDE RTC3(relatched):abcde DONE" \
		&& echo "gameboy-rgbds-mbc3-test: OK (RTC latch/isolation behavior correct)" \
		|| (echo "gameboy-rgbds-mbc3-test: FAIL (expected serial output not seen)"; exit 1)

gameboy-gtk: $(GAMEBOY_GTK_TARGET)

$(EMU_TARGET): $(EMU_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(EMU_OBJS)

$(ASM_TARGET): $(ASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ASM_OBJS)

$(DASM_TARGET): $(DASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(DASM_OBJS)

$(GTK_TARGET): $(GTK_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GTK_OBJS) $(GTK_LIBS)

$(GAMEBOY_TARGET): $(GAMEBOY_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GAMEBOY_OBJS)

$(GAMEBOY_GTK_TARGET): $(GAMEBOY_GTK_OBJS) $(GAMEBOY_CORE_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GAMEBOY_GTK_OBJS) $(GAMEBOY_CORE_OBJS) $(GAMEBOY_GTK_LIBS)

$(GAMEBOY_TEST_TARGET): gameboy/tests/test_cart.c $(GAMEBOY_SRC_DIR)/cart.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ gameboy/tests/test_cart.c $(GAMEBOY_SRC_DIR)/cart.c

$(GAMEBOY_TEST_TIMER_TARGET): gameboy/tests/test_timer.c $(GAMEBOY_SRC_DIR)/timer.c $(GAMEBOY_SRC_DIR)/mmu.c $(GAMEBOY_SRC_DIR)/cart.c $(GAMEBOY_SRC_DIR)/ppu.c $(GAMEBOY_SRC_DIR)/joypad.c $(GAMEBOY_SRC_DIR)/apu.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -lm -o $@ gameboy/tests/test_timer.c $(GAMEBOY_SRC_DIR)/timer.c $(GAMEBOY_SRC_DIR)/mmu.c $(GAMEBOY_SRC_DIR)/cart.c $(GAMEBOY_SRC_DIR)/ppu.c $(GAMEBOY_SRC_DIR)/joypad.c $(GAMEBOY_SRC_DIR)/apu.c

$(GAMEBOY_TEST_APU_TARGET): gameboy/tests/test_apu.c $(GAMEBOY_SRC_DIR)/apu.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -lm -o $@ gameboy/tests/test_apu.c $(GAMEBOY_SRC_DIR)/apu.c

$(GAMEBOY_TEST_CPU_TARGET): gameboy/tests/test_cpu.c $(GAMEBOY_SRC_DIR)/cpu.c $(GAMEBOY_SRC_DIR)/alu.c $(GAMEBOY_SRC_DIR)/mmu.c $(GAMEBOY_SRC_DIR)/cart.c $(GAMEBOY_SRC_DIR)/ppu.c $(GAMEBOY_SRC_DIR)/joypad.c $(GAMEBOY_SRC_DIR)/apu.c $(GAMEBOY_SRC_DIR)/timer.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -lm -o $@ gameboy/tests/test_cpu.c $(GAMEBOY_SRC_DIR)/cpu.c $(GAMEBOY_SRC_DIR)/alu.c $(GAMEBOY_SRC_DIR)/mmu.c $(GAMEBOY_SRC_DIR)/cart.c $(GAMEBOY_SRC_DIR)/ppu.c $(GAMEBOY_SRC_DIR)/joypad.c $(GAMEBOY_SRC_DIR)/apu.c $(GAMEBOY_SRC_DIR)/timer.c

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(GTK_SRC_DIR)/%.o: $(GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

$(GAMEBOY_GTK_SRC_DIR)/%.o: $(GAMEBOY_GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(GAMEBOY_GTK_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: emulator
	./$(EMU_TARGET) cpm/emu/zexall/ZEXALL-main/zexall.com | less

test: emulator assembler
	./cpm/tests/run_tests.sh

clean:
	rm -f $(EMU_OBJS) $(ASM_OBJS) $(DASM_OBJS) $(GTK_OBJS) $(GAMEBOY_OBJS) $(GAMEBOY_GTK_OBJS) $(EMU_TARGET) $(ASM_TARGET) $(DASM_TARGET) $(GTK_TARGET) $(GAMEBOY_TARGET) $(GAMEBOY_TEST_TARGET) $(GAMEBOY_TEST_TIMER_TARGET) $(GAMEBOY_TEST_APU_TARGET) $(GAMEBOY_TEST_CPU_TARGET) $(GAMEBOY_VISUAL_OUT) $(GAMEBOY_2048_OUT) $(GAMEBOY_DRONEBOY_OUT) $(GAMEBOY_TOBU_OUT) $(GAMEBOY_GTK_TARGET) $(RGBDS_HELLO_OBJ) $(RGBDS_HELLO_ROM) $(RGBDS_MBC3_RTC_OBJ) $(RGBDS_MBC3_RTC_ROM)
