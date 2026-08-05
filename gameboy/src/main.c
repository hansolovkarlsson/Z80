#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cpu.h"
#include "mmu.h"
#include "cart.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"

// Phase 4 bring-up driver: load a real cartridge, run it, ticking the
// PPU and timer alongside the CPU each step (same clock, same call
// site - see gb_ppu_step()'s own comment), and either print serial
// output (Blargg-style text tests) or dump a rendered frame as a PPM
// image (--ppm) once enough VBlanks have passed. Interrupts now
// dispatch for real (cpu.c), so dmg-acid2's mid-frame raster effects
// and Blargg's 02-interrupts.gb/instr_timing.gb are all in scope this
// phase - see docs/GAMEBOY_ROADMAP.md's Phase 4 status for results.
// No real input source exists yet (Phase 7's job) - the joypad reports
// "nothing pressed" for the whole run.

static void serial_putc(uint8_t byte) {
    putchar(byte);
    fflush(stdout);
}

// DMG shade index (0=white..3=black) -> 8-bit grayscale sample, evenly
// spaced across the full 0-255 range - matches how the dmg-acid2
// reference PNG (a 2-bit grayscale image) maps its own 4 shades, so a
// byte-for-byte comparison against it doesn't need any special-casing.
static uint8_t shade_to_gray(uint8_t shade) {
    switch (shade) {
        case 0: return 255;
        case 1: return 170;
        case 2: return 85;
        default: return 0; // 3
    }
}

static void write_ppm(const GBPpu *ppu, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Couldn't open '%s' for writing\n", path);
        return;
    }
    fprintf(f, "P5\n%d %d\n255\n", GB_SCREEN_WIDTH, GB_SCREEN_HEIGHT);
    for (int y = 0; y < GB_SCREEN_HEIGHT; y++) {
        for (int x = 0; x < GB_SCREEN_WIDTH; x++) {
            uint8_t gray = shade_to_gray(ppu->framebuffer[y][x]);
            fwrite(&gray, 1, 1, f);
        }
    }
    fclose(f);
    fprintf(stderr, "Wrote frame to '%s'\n", path);
}

int main(int argc, char **argv) {
    if (argc < 2 || strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
        fprintf(stderr,
                "Usage:\n"
                "  %s <rom.gb>                        Run for a fixed instruction budget\n"
                "  %s <rom.gb> --ppm <out.ppm> [--frames N]\n"
                "                                      Run until N VBlanks complete (default 2),\n"
                "                                      dump the frame as a PPM image, and exit\n",
                argv[0], argv[0]);
        return 1;
    }

    const char *ppm_path = NULL;
    int target_frames = 2;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "--ppm") == 0 && i + 1 < argc) {
            ppm_path = argv[++i];
        } else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
            target_frames = atoi(argv[++i]);
        }
    }

    GBCart cart;
    if (gb_cart_load(&cart, argv[1]) != 0) {
        return 1;
    }

    GBCpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.memory = calloc(1, GB_MEM_SIZE); // VRAM/WRAM/OAM/I-O-regs/HRAM only - see cpu.h
    cpu.cart = &cart;

    GBPpu ppu;
    cpu.ppu = &ppu;
    gb_ppu_reset(&ppu);

    GBTimer timer;
    cpu.timer = &timer;
    gb_timer_reset(&timer);

    GBJoypad joypad;
    cpu.joypad = &joypad;
    gb_joypad_reset(&joypad);

    gb_cpu_init_tables();
    gb_cpu_reset(&cpu);
    gb_serial_output_hook = serial_putc;

    fprintf(stderr, "Starting SM83 execution loop...\n\n");

    // A generous, fixed instruction budget rather than any kind of
    // completion detection - Blargg's test ROMs spin in an infinite
    // loop once done (no clean "exit" signal to detect). 20M
    // instructions is far more than any cpu_instrs sub-test (or a
    // static test image like dmg-acid2) needs to finish.
    const long budget = 20000000;
    long executed = 0;
    int frames_seen = 0;
    for (; executed < budget; executed++) {
        int cycles = gb_cpu_step(&cpu);
        if (cycles < 0) {
            fprintf(stderr, "\nIllegal/unimplemented opcode at PC=0x%04X\n", (unsigned)(cpu.pc - 1));
            break;
        }
        gb_ppu_step(&ppu, &cpu, cycles);
        gb_timer_step(&timer, &cpu, cycles);

        if (ppu.frame_ready) {
            ppu.frame_ready = 0;
            frames_seen++;
            if (ppm_path && frames_seen >= target_frames) break;
        }
    }

    if (ppm_path) {
        write_ppm(&ppu, ppm_path);
    }

    fprintf(stderr, "\n\nExecuted %ld instructions (budget %ld), %d frame(s). Final PC=0x%04X\n",
            executed, budget, frames_seen, (unsigned)cpu.pc);

    free(cpu.memory);
    gb_cart_free(&cart);
    return 0;
}
