// abc806/emu/src/main.c - bin/abc806, the Luxor ABC806 machine target.
//
// Milestone 1 only: bring the machine up far enough to prove the memory
// map works. The gate ABC806_SCOPING.md set for this milestone is that the
// machine executes past reset and programs the CRTC - the same signal that
// gated the ABC802's own first milestone, and for the same reason: a
// programmed CRTC means the ROM got through its initialisation rather than
// wedging in it.
//
// Deliberately absent, because they belong to later milestones: video
// rendering, keyboard input, disk, and the interactive front-end. This
// binary runs a fixed number of T-states and reports what the machine did.

#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../z80core/z80.h"
#include "memory.h"
#include "ports.h"
#include "step.h"

#define DEFAULT_ROM_DIR "abc806/resources/rom"
#define DEFAULT_DOS_ROM "ABC806-dos.66-31.bin"

static void usage(const char *argv0) {
    printf("Usage: %s [options]\n\n", argv0);
    printf("Milestone 1: memory map and boot. No video, keyboard or disk yet.\n\n");
    printf("Options:\n");
    printf("  --rom-dir DIR    ROM directory (default: %s)\n", DEFAULT_ROM_DIR);
    printf("  --dos-rom FILE   DOS PROM at 0x6000 (default: %s)\n", DEFAULT_DOS_ROM);
    printf("  --cycles N       stop after N T-states (default: 20000000)\n");
    printf("  --profile        print the most-executed addresses when the run ends\n");
    printf("  -h, --help       this message\n");
    printf("\nEnvironment:\n");
    printf("  ABC806_TRACE_IO=1   log every I/O port access to stderr\n");
}

int main(int argc, char **argv) {
    const char *rom_dir = DEFAULT_ROM_DIR;
    const char *dos_rom = DEFAULT_DOS_ROM;
    long long max_cycles = 20000000;
    bool profile = false;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            usage(argv[0]);
            return 0;
        } else if (!strcmp(argv[i], "--rom-dir") && i + 1 < argc) {
            rom_dir = argv[++i];
        } else if (!strcmp(argv[i], "--dos-rom") && i + 1 < argc) {
            dos_rom = argv[++i];
        } else if (!strcmp(argv[i], "--cycles") && i + 1 < argc) {
            max_cycles = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--profile")) {
            profile = true;
        } else {
            fprintf(stderr, "Unknown argument '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    static uint8_t ram[RAM_SIZE];
    Z80 cpu = {0};
    cpu.memory = ram;
    z80_init_tables();

    if (!abc806_memory_init(&cpu, rom_dir, dos_rom)) return 1;
    abc806_memory_attach(&cpu);
    abc806_ports_attach(&cpu);

    printf("ABC806: loaded 32K ROM from '%s' (DOS PROM '%s')\n", rom_dir, dos_rom);

    static unsigned long long hits[0x10000];
    long long cycles = 0;
    long long instructions = 0;
    bool halted = false;

    while (cycles < max_cycles) {
        if (profile) hits[cpu.pc]++;
        int taken = abc806_step(&cpu, ram, &cycles);
        if (taken < 0) {
            printf("Unimplemented opcode at PC=%04X; stopping\n", cpu.pc);
            halted = true;
            break;
        }
        instructions++;
    }

    printf("Ran %lld instructions / %lld T-states; PC=%04X (%s)\n",
           instructions, cycles, cpu.pc,
           halted ? "halted" : "reached T-state cap");

    // The milestone gate.
    if (abc806_crtc_programmed()) {
        printf("CRTC programmed: yes (R1=%d cols, R6=%d rows)\n",
               abc806_crtc_reg(1), abc806_crtc_reg(6));
    } else {
        printf("CRTC programmed: no - the ROM did not get that far\n");
    }

    // A second, independent sign that the boot did real work rather than
    // merely surviving: the ABC802's own milestone 1 printed the same
    // summary. A CRTC programmed but a blank character RAM would mean the
    // ROM configured the video and then wedged before drawing anything.
    {
        const uint8_t *cram = abc806_char_ram();
        const uint8_t *aram = abc806_attr_ram();
        int nonzero = 0, nonspace = 0, attrs = 0;
        for (int i = 0; i < ABC806_CHAR_RAM_SIZE; i++) {
            if (cram[i]) nonzero++;
            if ((cram[i] & 0x7F) != 0x20 && cram[i]) nonspace++;
            if (aram[i]) attrs++;
        }
        printf("Character RAM: %d/%d nonzero, %d non-space; "
               "attribute plane: %d nonzero\n",
               nonzero, ABC806_CHAR_RAM_SIZE, nonspace, attrs);
    }

    if (profile) {
        printf("Most-executed addresses:\n");
        for (int n = 0; n < 20; n++) {
            int best = -1;
            for (int a = 0; a < 0x10000; a++)
                if (hits[a] && (best < 0 || hits[a] > hits[best])) best = a;
            if (best < 0) break;
            printf("  %04X  %llu\n", best, hits[best]);
            hits[best] = 0;
        }
    }

    return halted ? EXIT_FAILURE : EXIT_SUCCESS;
}
