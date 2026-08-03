#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <stdbool.h>
#include <string.h>

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
    // --ccp <path> boots a CCP (a "shell": DIR/TYPE/ERA/etc. plus running
    // other .com files by name) instead of running a single program - see
    // cpm.h's CCP_BASE/cpm_set_ccp_mode comments for how a warm boot then
    // re-enters the CCP instead of halting the emulator.
    bool ccp_boot = argc > 1 && strcmp(argv[1], "--ccp") == 0;
    const char *test_file;
    if (ccp_boot) {
        test_file = (argc > 2) ? argv[2] : "cpm_disk/ccp.com";
    } else {
        test_file = (argc > 1) ? argv[1] : "emu/zexall/ZEXALL-main/zexall.com";
    }
    uint16_t load_address = ccp_boot ? CCP_BASE : 0x0100;

    // 1. Initialize CPU & lookup tables
    Z80 cpu = {0};
    cpu.memory = ram;
    z80_init_tables();
    cpm_console_init();
    cpm_fileio_init();
    cpm_set_ccp_mode(ccp_boot);

    // 2. Load the program (or CCP) into RAM
    if (!load_file(test_file, load_address)) {
        return EXIT_FAILURE;
    }

    // 3. Set initial registers according to CP/M standard
    cpu.pc = load_address;
    cpu.sp = 0xF000; // Set stack pointer near the top of memory - the CCP
                      // resets this itself on entry, same as a real BIOS
                      // cold boot would before ever reaching the CCP.

    // Installs the minimal BIOS jump table (JP <wboot> at 0x0000, plus
    // CONST/CONIN/CONOUT and no-op stubs for the rest) - see cpm.c's BIOS
    // comment for why real software needs this, not just a bare RET.
    cpm_bios_init(ram);

    // CP/M injects a RET (0xC9) instruction at 0x0005 to return from BDOS calls
    ram[0x0005] = 0xC9;

    printf("Starting Z80 Execution Loop...\n\n");

    // 4. Main Execution Loop
    bool running = true;
    uint64_t total_cycles = 0;

    while (running) {
        // If PC reaches address 0, the program has finished or terminated.
        // Skipped in CCP mode: address 0 only holds a JP to the real WBOOT
        // vector (installed by cpm_bios_init) - that JP needs to actually
        // execute (via the z80_step() call below) so check_cpm_bios() can
        // intercept it at the vector address and redirect back into the
        // CCP. Checking pc==0 here first, before that JP ever runs, would
        // halt the whole emulator instead - a program simply doing `jp 0`
        // (hello.com's own way of returning to CP/M) never even reaches
        // the WBOOT vector otherwise.
        if (!ccp_boot && cpu.pc == 0x0000) {
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


