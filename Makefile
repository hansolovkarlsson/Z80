CC := gcc
CFLAGS := -Wall -Wextra -O2

BIN_DIR := bin

EMU_SRC_DIR := emu/src
EMU_SRCS := $(wildcard $(EMU_SRC_DIR)/*.c)
EMU_OBJS := $(EMU_SRCS:.c=.o)
EMU_TARGET := $(BIN_DIR)/z80

ASM_SRC_DIR := asm/src
ASM_SRCS := $(wildcard $(ASM_SRC_DIR)/*.c)
ASM_OBJS := $(ASM_SRCS:.c=.o)
ASM_TARGET := $(BIN_DIR)/z80asm

DASM_SRC_DIR := disasm/src
DASM_SRCS := $(wildcard $(DASM_SRC_DIR)/*.c)
DASM_OBJS := $(DASM_SRCS:.c=.o)
DASM_TARGET := $(BIN_DIR)/z80dasm

# Opt-in only (never part of `all`/`test`) - the only build target with an
# external dependency beyond a bare C compiler. A thin GTK4+VTE launcher
# for the real bin/z80, not a separate emulator - see gtk/src/main.c's
# own comment for why.
GTK_SRC_DIR := gtk/src
GTK_SRCS := $(wildcard $(GTK_SRC_DIR)/*.c)
GTK_OBJS := $(GTK_SRCS:.c=.o)
GTK_TARGET := $(BIN_DIR)/z80-gtk
GTK_PKGS := gtk4 vte-2.91-gtk4
GTK_CFLAGS := $(shell pkg-config --cflags $(GTK_PKGS) 2>/dev/null)
GTK_LIBS := $(shell pkg-config --libs $(GTK_PKGS) 2>/dev/null)

# Opt-in only (never part of `all`/`test`), same reasoning as GTK above
# but for a different reason: this is a brand new, separate emulator
# (see docs/GAMEBOY_ROADMAP.md), still too early to fold into the
# default build/test - revisit once it can run real games end to end.
GAMEBOY_SRC_DIR := gameboy/src
GAMEBOY_SRCS := $(wildcard $(GAMEBOY_SRC_DIR)/*.c)
GAMEBOY_OBJS := $(GAMEBOY_SRCS:.c=.o)
GAMEBOY_TARGET := $(BIN_DIR)/gameboy

# cart.c's own unit tests (gameboy/tests/test_cart.c) - unlike Blargg's
# cpu_instrs (fetched locally, never committed - see
# docs/GAMEBOY_ROADMAP.md's licensing note), this is this project's own
# code with no licensing question, so it's a real `make`-able regression
# gate for the MBC1/MBC3/MBC5 banking logic despite no real MBC test ROM
# being available to commit.
GAMEBOY_TEST_TARGET := $(BIN_DIR)/gameboy-test-cart

.PHONY: all emulator assembler disassembler gtk gameboy gameboy-test run test clean

all: emulator assembler disassembler

emulator: $(EMU_TARGET)

assembler: $(ASM_TARGET)

disassembler: $(DASM_TARGET)

gtk: $(GTK_TARGET)

gameboy: $(GAMEBOY_TARGET)

gameboy-test: $(GAMEBOY_TEST_TARGET)
	./$(GAMEBOY_TEST_TARGET)

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

$(GAMEBOY_TEST_TARGET): gameboy/tests/test_cart.c $(GAMEBOY_SRC_DIR)/cart.c | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ gameboy/tests/test_cart.c $(GAMEBOY_SRC_DIR)/cart.c

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

$(GTK_SRC_DIR)/%.o: $(GTK_SRC_DIR)/%.c
	$(CC) $(CFLAGS) $(GTK_CFLAGS) -c $< -o $@

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: emulator
	./$(EMU_TARGET) emu/zexall/ZEXALL-main/zexall.com | less

test: emulator assembler
	./tests/run_tests.sh

clean:
	rm -f $(EMU_OBJS) $(ASM_OBJS) $(DASM_OBJS) $(GTK_OBJS) $(GAMEBOY_OBJS) $(EMU_TARGET) $(ASM_TARGET) $(DASM_TARGET) $(GTK_TARGET) $(GAMEBOY_TARGET) $(GAMEBOY_TEST_TARGET)
