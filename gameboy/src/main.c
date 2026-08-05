#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "mmu.h"
#include "cart.h"

// Phase 2 bring-up driver: load a real cartridge (any ROM/RAM size,
// MBC-less/MBC1/MBC3/MBC5 - see cart.c) through gb_cart_load(), run it,
// and print whatever it sends out the serial port. Blargg's test ROMs
// use the serial port (SB/SC, see mmu.h) to report PASS/FAIL text
// without a real link-cable peer - there's no PPU (Phase 3) to read a
// "printed to screen" result from yet, so this is the only real
// correctness signal available this phase. Not a permanent test harness
// shape - Phase 3 onward will need a real way to drive and observe
// these ROMs visually too.

static void serial_putc(uint8_t byte) {
    putchar(byte);
    fflush(stdout);
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr, "Usage:\n  %s <rom.gb>   Load a cartridge and run it\n", argv[0]);
        return 1;
    }

    GBCart cart;
    if (gb_cart_load(&cart, argv[1]) != 0) {
        return 1;
    }

    GBCpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.memory = calloc(1, GB_MEM_SIZE); // VRAM/WRAM/OAM/I-O-regs/HRAM only - see cpu.h
    cpu.cart = &cart;
    gb_cpu_init_tables();
    gb_cpu_reset(&cpu);
    gb_serial_output_hook = serial_putc;

    fprintf(stderr, "Starting SM83 execution loop...\n\n");

    // A generous, fixed instruction budget rather than any kind of
    // completion detection - Blargg's test ROMs spin in an infinite
    // loop once done (no clean "exit" signal to detect), and Phase 4
    // doesn't exist yet to make HALT ever legitimately return control
    // here. 20M instructions is far more than any cpu_instrs sub-test
    // needs to finish and print its result.
    const long budget = 20000000;
    long executed = 0;
    for (; executed < budget; executed++) {
        int cycles = gb_cpu_step(&cpu);
        if (cycles < 0) {
            fprintf(stderr, "\nIllegal/unimplemented opcode at PC=0x%04X\n", (unsigned)(cpu.pc - 1));
            break;
        }
    }

    fprintf(stderr, "\n\nExecuted %ld instructions (budget %ld). Final PC=0x%04X\n",
            executed, budget, (unsigned)cpu.pc);

    free(cpu.memory);
    gb_cart_free(&cart);
    return 0;
}
