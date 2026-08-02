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

.PHONY: all emulator assembler disassembler run test clean

all: emulator assembler disassembler

emulator: $(EMU_TARGET)

assembler: $(ASM_TARGET)

disassembler: $(DASM_TARGET)

$(EMU_TARGET): $(EMU_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(EMU_OBJS)

$(ASM_TARGET): $(ASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ASM_OBJS)

$(DASM_TARGET): $(DASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(DASM_OBJS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: emulator
	./$(EMU_TARGET) | less

test: emulator assembler
	./tests/run_tests.sh

clean:
	rm -f $(EMU_OBJS) $(ASM_OBJS) $(DASM_OBJS) $(EMU_TARGET) $(ASM_TARGET) $(DASM_TARGET)
