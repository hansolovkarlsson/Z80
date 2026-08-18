// abc802/emu/src/main.c - bin/abc802, the Luxor ABC802 machine target.
//
// A second Z80 machine alongside abc80/ and cpm/, sharing the same proven
// core (z80core/z80.o + alu.o) via z80_execute(). See
// abc802/docs/ABC802_ROADMAP.md for status and abc802/docs/ABC802_REFERENCE.md
// for the hardware being modeled.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../../z80core/z80.h"
#include "memory.h"
#include "ports.h"
#include "render.h"

#define DEFAULT_ROM_DIR "abc802/resources/rom"
#define DEFAULT_DOS_ROM "ABC802-dos.32-31.bin"

static void usage(const char *argv0) {
    printf("Usage: %s [options]\n", argv0);
    printf("\nOptions:\n");
    printf("  --rom-dir DIR    ROM directory (default: %s)\n", DEFAULT_ROM_DIR);
    printf("  --dos-rom FILE   DOS/option ROM image (default: %s)\n", DEFAULT_DOS_ROM);
    printf("  --cycles N       stop after N T-states (default: 20000000)\n");
    printf("  --screen         print the text screen when the run ends\n");
    printf("  --profile        print the most-executed addresses when the run ends\n");
    printf("  --type TEXT      send TEXT to the keyboard once the ROM is ready for it\n");
    printf("  --columns 40|80  characters per line (DIP S3, default 40 as on MAME)\n");
    printf("  -h, --help       this message\n");
    printf("\nEnvironment:\n");
    printf("  ABC802_TRACE_IO=1   log every I/O port access to stderr\n");
}

int main(int argc, char **argv) {
    const char *rom_dir = DEFAULT_ROM_DIR;
    const char *dos_rom = DEFAULT_DOS_ROM;
    long long max_cycles = 20000000;
    int show_screen = 0;
    int show_profile = 0;
    const char *type_text = NULL;
    int columns = 40;

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
        } else if (!strcmp(argv[i], "--screen")) {
            show_screen = 1;
        } else if (!strcmp(argv[i], "--profile")) {
            show_profile = 1;
        } else if (!strcmp(argv[i], "--type") && i + 1 < argc) {
            type_text = argv[++i];
        } else if (!strcmp(argv[i], "--columns") && i + 1 < argc) {
            columns = atoi(argv[++i]);
            if (columns != 40 && columns != 80) {
                fprintf(stderr, "--columns must be 40 or 80\n");
                return 1;
            }
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argv[i]);
            usage(argv[0]);
            return 1;
        }
    }

    static uint8_t ram[RAM_SIZE];
    memset(ram, 0, sizeof(ram));

    // cpu.memory and the `ram` argument passed to z80_execute() must be the
    // *same* buffer - opcode fetch reads `ram` directly while operand access
    // goes through z80_read_byte()/cpu.memory. Same requirement the ABC80 and
    // CP/M targets document in their own main.c.
    Z80 cpu = {0};
    cpu.memory = ram;
    cpu.pc = 0x0000;
    z80_init_tables();

    if (!abc802_memory_init(&cpu, rom_dir, dos_rom)) return 1;
    abc802_memory_attach(&cpu);
    abc802_ports_attach(&cpu);
    abc802_set_config(columns == 80, true);

    printf("ABC802: loaded 32K ROM from '%s' (DOS ROM '%s')\n", rom_dir, dos_rom);

    // Per-address execution counts. A flat 64K array of counters is the
    // simplest thing that answers "where is it actually spending its time",
    // which is the first question to ask of a ROM that runs but produces
    // nothing - the same reason abc80/emu/src/main.c keeps its own visited[]
    // array.
    static long long pc_hits[RAM_SIZE];

    // Keyboard feed. Bytes are handed over one at a time, each only once
    // the ROM has actually consumed the previous one - the DART holds a
    // single receive byte, so typing faster than the ROM reads would just
    // overwrite it. The gap is measured in T-states rather than
    // instructions so it tracks emulated time.
    size_t type_pos = 0;
    size_t type_len = type_text ? strlen(type_text) : 0;
    long long next_key_at = 0;
    // Gap between keystrokes, in T-states (~0.1s of emulated time at
    // 3 MHz). Generous on purpose: the ROM discards input while it is
    // still initializing after the sign-on banner, so typing at a
    // realistic human pace is what makes the first characters survive.
    const long long key_gap = 300000;

    long long cycles = 0;
    long long instructions = 0;
    while (cycles < max_cycles) {
        // Stands in for the real M1 line - see memory.c's header comment.
        // Must happen before the instruction runs, since it is that
        // instruction's own data reads that consult it.
        abc802_note_instruction_fetch(cpu.pc);
        if (show_profile) pc_hits[cpu.pc]++;
        int taken = z80_execute(&cpu, ram);
        if (taken < 0) {
            fprintf(stderr, "Halted: unimplemented opcode at PC=%04X\n", cpu.pc);
            break;
        }
        cycles += taken;
        instructions++;
        abc802_ports_tick(&cpu, taken);

        if (type_pos < type_len && cycles >= next_key_at &&
            abc802_keyboard_ready() && !abc802_keyboard_busy()) {
            char ch = type_text[type_pos++];
            abc802_keyboard_send((uint8_t)(ch == '\n' ? 0x0D : ch));
            // ~10ms of emulated time between keystrokes at 3 MHz, which is
            // slower than any real typist and leaves the ROM ample room to
            // process each one.
            next_key_at = cycles + key_gap;
        }
    }

    printf("Ran %lld instructions / %lld T-states; PC=%04X\n",
           instructions, cycles, cpu.pc);
    printf("CRTC programmed: %s (R1=%d cols, R6=%d rows)\n",
           abc802_crtc_programmed() ? "yes" : "no",
           abc802_crtc_reg(1), abc802_crtc_reg(6));

    if (show_profile) {
        printf("\nMost-executed addresses:\n");
        for (int rank = 0; rank < 20; rank++) {
            int best = -1;
            for (int a = 0; a < RAM_SIZE; a++) {
                if (pc_hits[a] > 0 && (best < 0 || pc_hits[a] > pc_hits[best])) best = a;
            }
            if (best < 0) break;
            printf("  %04X  %lld\n", best, pc_hits[best]);
            pc_hits[best] = 0;
        }
    }

    {
        const uint8_t *cr = abc802_char_ram();
        int nonzero = 0, nonspace = 0;
        for (int i = 0; i < ABC802_CHAR_RAM_SIZE; i++) {
            if (cr[i] != 0) nonzero++;
            if (cr[i] != 0 && cr[i] != 0x20) nonspace++;
        }
        printf("Character RAM: %d/%d nonzero, %d non-space\n",
               nonzero, ABC802_CHAR_RAM_SIZE, nonspace);
    }

    if (show_screen) abc802_render_text_screen(stdout);
    return 0;
}
