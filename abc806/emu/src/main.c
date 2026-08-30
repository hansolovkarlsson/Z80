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
#include "../../../abcbus/disk.h"
#include "chargen.h"
#include "memory.h"
#include "png.h"
#include "ports.h"
#include "rtc.h"
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
    printf("  --disk FILE      attach FILE as a floppy image on the ABC-bus\n");
    printf("  --type TEXT      send TEXT to the keyboard once the ROM is ready\n");
    printf("  --type-at N      hold TEXT back until N T-states have run\n");
    printf("  --screen         print the text screen when the run ends\n");
    printf("  --screenshot F   write the screen as a real PNG to F - actual\n");
    printf("                   pixels from the character ROM and the RAD PROM,\n");
    printf("                   including the attributes --screen cannot show\n");
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
    bool show_screen = false;
    const char *screenshot_path = NULL;
    const char *type_text = NULL;
    long long type_at = 0;
    const char *disk_paths[8];
    int disk_count = 0;

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
        } else if (!strcmp(argv[i], "--disk") && i + 1 < argc) {
            if (disk_count < 8) disk_paths[disk_count++] = argv[++i];
            else { fprintf(stderr, "Too many --disk arguments\n"); return 1; }
        } else if (!strcmp(argv[i], "--type") && i + 1 < argc) {
            type_text = argv[++i];
        } else if (!strcmp(argv[i], "--type-at") && i + 1 < argc) {
            type_at = atoll(argv[++i]);
        } else if (!strcmp(argv[i], "--screen")) {
            show_screen = true;
        } else if (!strcmp(argv[i], "--screenshot") && i + 1 < argc) {
            screenshot_path = argv[++i];
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
    abc806_rtc_init();

    for (int d = 0; d < disk_count; d++)
        if (!abcbus_disk_attach_arg(disk_paths[d])) return 1;

    printf("ABC806: loaded 32K ROM from '%s' (DOS PROM '%s')\n", rom_dir, dos_rom);
    if (disk_count > 0) {
        printf("ABC-bus: %s floppy controller, %d drive%s attached, interleave %u\n",
               abcbus_disk_type_name(), abcbus_disk_attached_count(),
               abcbus_disk_attached_count() == 1 ? "" : "s",
               abcbus_disk_interleave());
    }

    static unsigned long long hits[0x10000];
    long long cycles = 0;
    long long instructions = 0;
    bool halted = false;

    // Keystrokes are paced the same way the ABC802's are, and for the same
    // hardware reason: the DART holds exactly one received byte, so a
    // burst would overwrite itself. ~0.1s of emulated time per key.
    const long long key_gap = 300000;
    size_t type_pos = 0;
    size_t type_len = type_text ? strlen(type_text) : 0;
    long long next_key_at = type_at;

    while (cycles < max_cycles) {
        if (type_pos < type_len && cycles >= next_key_at &&
            !abc806_keyboard_busy() && abc806_keyboard_ready()) {
            uint8_t ch = (uint8_t)type_text[type_pos++];
            abc806_keyboard_send(ch == '\n' ? 0x0D : ch);
            next_key_at = cycles + key_gap;
        }
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

    // Everything the decode needs, gathered in one place. abc806_render_pixels()
    // is pure, so this struct is the entire interface between the machine
    // and the picture.
    Abc806Screen screen = {
        .char_ram = abc806_char_ram(),
        .attr_ram = abc806_attr_ram(),
        .char_rom = abc806_char_rom(),
        .rad_prom = abc806_rad_prom(),
        .columns = abc806_80_column() ? 80 : 40,
        .rows = abc806_crtc_reg(6),
        .scanlines = (abc806_crtc_reg(9) & 0x1F) + 1,
        .start_addr = (uint16_t)(((abc806_crtc_reg(12) << 8) |
                                   abc806_crtc_reg(13)) & 0x7FF),
        .cursor_addr = abc806_cursor_address(),
        .flash_on = false,
        .forty = !abc806_80_column(),
    };

    if (show_screen) {
        if (!abc806_crtc_programmed()) {
            printf("(CRTC not programmed - no display yet)\n");
        } else {
            // A character-level dump, for reading in a terminal. It cannot
            // show colour or any of the RAD-PROM attributes - that is what
            // --screenshot is for - so it deliberately renders the codes
            // rather than pretending otherwise.
            const uint8_t *cram = abc806_char_ram();
            printf("+");
            for (int x = 0; x < screen.columns; x++) putchar('-');
            printf("+\n");
            for (int row = 0; row < screen.rows; row++) {
                putchar('|');
                for (int col = 0; col < screen.columns; col++) {
                    uint16_t ma = (uint16_t)(screen.start_addr +
                                             row * screen.columns + col);
                    uint8_t ch = cram[ma & 0x7FF] & 0x7F;
                    putchar((ch >= 0x20 && ch < 0x7F) ? (char)ch : ' ');
                }
                printf("|\n");
            }
            printf("+");
            for (int x = 0; x < screen.columns; x++) putchar('-');
            printf("+\n");
        }
    }

    if (screenshot_path) {
        static uint8_t pixels[ABC806_MAX_PIXELS];
        int w = abc806_pixel_width(&screen);
        int h = abc806_pixel_height(&screen);
        static uint32_t pal[8];
        for (int i = 0; i < 8; i++) pal[i] = abc806_palette(i);
        if (!abc806_render_pixels(&screen, pixels, sizeof pixels)) {
            fprintf(stderr, "Screenshot failed: CRTC not programmed, or screen too large\n");
        } else if (!abc806_write_png(screenshot_path, pixels, w, h, pal, 8)) {
            fprintf(stderr, "Screenshot failed: could not write '%s'\n", screenshot_path);
        } else {
            printf("Screenshot: %s (%dx%d)\n", screenshot_path, w, h);
        }
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
