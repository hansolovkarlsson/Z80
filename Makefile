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

# Luxor ABC80 machine target (Milestone 1: boots the real BASIC ROM on the
# shared core - see abc80/docs/ABC80_ROADMAP.md). Reuses cpm/emu/src/z80.o
# and alu.o directly rather than recompiling them under a different name -
# both are already machine-agnostic (z80_execute(), not the CP/M-specific
# z80_step()) and built with identical flags, so there's nothing to
# duplicate. Opt-in only (never part of `all`/`test`), same as `gtk` below -
# with no video/keyboard emulated yet, there's nothing test-suite-verifiable
# about it yet.
ABC80_SRC_DIR := abc80/emu/src
ABC80_SRCS := $(wildcard $(ABC80_SRC_DIR)/*.c)
ABC80_OBJS := $(ABC80_SRCS:.c=.o) $(EMU_SRC_DIR)/z80.o $(EMU_SRC_DIR)/alu.o
ABC80_TARGET := $(BIN_DIR)/abc80

# z80.c's own interrupt-acceptance unit test (cpm/tests/test_interrupts.c) -
# a direct C-level test, not a .asm program run through run_tests.sh like
# every other regression check here, since no CP/M-executable instruction
# can raise an interrupt against itself (INT/NMI are host-side signals on
# real hardware) - see that file's own top comment. Built from
# $(EMU_SRC_DIR)'s real z80.c/alu.c/cpm.c, not a separate reimplementation.
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

.PHONY: all emulator assembler disassembler gtk abc80 run test clean

all: emulator assembler disassembler

emulator: $(EMU_TARGET)

assembler: $(ASM_TARGET)

disassembler: $(DASM_TARGET)

gtk: $(GTK_TARGET)

abc80: $(ABC80_TARGET)

$(EMU_TARGET): $(EMU_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(EMU_OBJS)

$(ASM_TARGET): $(ASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ASM_OBJS)

$(DASM_TARGET): $(DASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(DASM_OBJS)

$(ABC80_TARGET): $(ABC80_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ABC80_OBJS)

$(EMU_TEST_INTERRUPTS_TARGET): cpm/tests/test_interrupts.c $(EMU_SRC_DIR)/z80.c $(EMU_SRC_DIR)/alu.c $(EMU_SRC_DIR)/cpm.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ cpm/tests/test_interrupts.c $(EMU_SRC_DIR)/z80.c $(EMU_SRC_DIR)/alu.c $(EMU_SRC_DIR)/cpm.c

$(GTK_TARGET): $(GTK_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(GTK_OBJS) $(GTK_LIBS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(GTK_SRC_DIR)/%.o: $(GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: emulator
	./$(EMU_TARGET) cpm/emu/zexall/ZEXALL-main/zexall.com | less

test: emulator assembler $(EMU_TEST_INTERRUPTS_TARGET)
	./cpm/tests/run_tests.sh

clean:
	rm -f $(EMU_OBJS) $(ASM_OBJS) $(DASM_OBJS) $(GTK_OBJS) $(ABC80_SRCS:.c=.o) $(EMU_TARGET) $(ASM_TARGET) $(DASM_TARGET) $(GTK_TARGET) $(ABC80_TARGET) $(EMU_TEST_INTERRUPTS_TARGET)
