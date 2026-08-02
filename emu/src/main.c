#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>

#include "common.h"

static u_int8_t ram[RAM_SIZE];

// Helper to load binary files into RAM
bool load_file(const char *filename, uint16_t load_address) {
    FILE *f = fopen(filename, "rb");
    if (!f) {
        perror("Failed to open file");
        return false;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (load_address + size > RAM_SIZE) {
        fprintf(stderr, "File too large for memory limits!\n");
        fclose(f);
        return false;
    }

    fread(&ram[load_address], 1, size, f);
    fclose(f);
    printf("Loaded '%s' (%ld bytes) at 0x%04X\n", filename, size, load_address);
    return true;
}

int main(int argc, char *argv[]) {
    const char *test_file = (argc > 1) ? argv[1] : "emu/zexall/ZEXALL-main/zexall.com";

    // 1. Initialize CPU & lookup tables
    Z80 cpu = {0};
    cpu.memory = ram;
    z80_init_tables();

    // 2. Load zexall.com at 0x0100 (Standard CP/M transient program area)
    if (!load_file(test_file, 0x0100)) {
        return EXIT_FAILURE;
    }

    // 3. Set initial registers according to CP/M standard
    cpu.pc = 0x0100; // Programs start at 0x0100
    cpu.sp = 0xF000; // Set stack pointer near the top of memory

    // CP/M injects a RET (0xC9) instruction at 0x0000 so calls to 0x0000 exit back
    ram[0x0000] = 0xC9; 

    // CP/M injects a RET (0xC9) instruction at 0x0005 to return from BDOS calls
    ram[0x0005] = 0xC9; 

    printf("Starting Z80 Execution Loop...\n\n");

    // 4. Main Execution Loop
    bool running = true;
    uint64_t total_cycles = 0;

    while (running) {
        // If PC reaches address 0, the program has finished or terminated
        if (cpu.pc == 0x0000) {
            printf("\nProgram terminated normally at PC=0x0000.\n");
            break;
        }

        // Run one instruction step
        int cycles = z80_step(&cpu, ram);
        total_cycles += cycles;

        // Debug fallback: stop if CPU hits a loop or fatal unhandled opcode
        if (cycles < 0) {
            fprintf(stderr, "Execution halted at PC: 0x%04X\n", cpu.pc);
            running = false;
        }
    }

    printf("Finished. Total T-states executed: %llu\n", total_cycles);
    return EXIT_SUCCESS;
}


