CC := gcc
CFLAGS := -Wall -Wextra -O2

BIN_DIR := bin

EMU_SRC_DIR := src
EMU_SRCS := $(wildcard $(EMU_SRC_DIR)/*.c)
EMU_OBJS := $(EMU_SRCS:.c=.o)
EMU_TARGET := $(BIN_DIR)/z80_emulator

ASM_SRC_DIR := asm/src
ASM_SRCS := $(wildcard $(ASM_SRC_DIR)/*.c)
ASM_OBJS := $(ASM_SRCS:.c=.o)
ASM_TARGET := $(BIN_DIR)/z80asm

.PHONY: all emulator assembler run clean

all: emulator assembler

emulator: $(EMU_TARGET)

assembler: $(ASM_TARGET)

$(EMU_TARGET): $(EMU_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(EMU_OBJS)

$(ASM_TARGET): $(ASM_OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(ASM_OBJS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: emulator
	./$(EMU_TARGET) | less

clean:
	rm -f $(EMU_OBJS) $(ASM_OBJS) $(EMU_TARGET) $(ASM_TARGET)
