CC := gcc
CFLAGS := -Wall -Wextra -O2

SRC_DIR := src
BIN_DIR := bin
TARGET := $(BIN_DIR)/z80_emulator

SRCS := $(wildcard $(SRC_DIR)/*.c)
OBJS := $(SRCS:.c=.o)

.PHONY: all run clean

all: $(TARGET)

$(TARGET): $(OBJS) | $(BIN_DIR)
	$(CC) $(CFLAGS) -o $@ $(OBJS)

$(BIN_DIR):
	mkdir -p $(BIN_DIR)

%.o: %.c
	$(CC) $(CFLAGS) -c $< -o $@

run: all
	./$(TARGET) | less

clean:
	rm -f $(OBJS) $(TARGET)
