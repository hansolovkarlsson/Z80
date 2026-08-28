// abc802/emu/src/chargen_dump.c - bin/abc802-chargen-dump, a standalone
// verification tool for chargen.c's decode.
//
// It exists because the thing chargen.c mostly implements - the three row
// attributes - is exactly the thing a real boot screen never exercises.
// `bin/abc802 --screenshot` proves the font decodes and the cursor lands
// in the right place, and nothing more: the ROM's own sign-on screen uses
// no Row Graphic, no Row Flash and no Row Clear, so a completely broken
// attribute state machine would render that screen perfectly. This drives
// a synthetic character RAM that uses all of them, which is the only way
// to see them work.
//
// Mirrors bin/abc80-chargen-dump's role for the other target: no CPU core,
// no ROM execution, just the decode fed known input. That is possible only
// because abc802_render_pixels() takes everything it needs as parameters
// rather than reading emulator globals.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "chargen.h"
#include "png.h"

#define CHAR_ROM_SIZE 0x1000
#define CHAR_RAM_SIZE 0x800

// The attribute character codes, read straight out of the committed font:
// these are the codes whose ROM bytes carry ATE. See chargen.c's header
// comment for the encoding.
#define ROW_GRAPHIC_ON  0x11
#define ROW_GRAPHIC_OFF 0x01
#define ROW_FLASH_ON    0x08
#define ROW_FLASH_OFF   0x09
#define ROW_CLEAR_ON    0x18

static bool load_char_rom(const char *path, uint8_t *dest) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open '%s'\n", path);
        return false;
    }
    // The chip is 8K; the video hardware only ever addresses the low 4K.
    uint8_t buf[0x2000];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n != sizeof(buf)) {
        fprintf(stderr, "'%s' is %zu bytes, expected %zu\n", path, n, sizeof(buf));
        return false;
    }
    memcpy(dest, buf, CHAR_ROM_SIZE);
    return true;
}

// Write a string into character RAM at (row, col), one cell per byte.
static void put_text(uint8_t *ram, int cols, int row, int col, const char *text, uint8_t or_mask) {
    for (int i = 0; text[i]; i++) {
        int addr = (row * cols + col + i) & 0x7FF;
        ram[addr] = (uint8_t)(text[i] | or_mask);
    }
}

static void put_byte(uint8_t *ram, int cols, int row, int col, uint8_t value) {
    ram[(row * cols + col) & 0x7FF] = value;
}

// Dump one character's 10 scanlines from both halves of the ROM, which is
// the most direct check that the address arithmetic (code << 4, plus 0x800
// for Row Graphic) is right.
static void print_glyph(const uint8_t *rom, uint8_t code) {
    printf("code 0x%02X: text half            graphics half\n", code);
    for (int line = 0; line < ABC802_CHAR_HEIGHT; line++) {
        uint8_t t = rom[((code & 0x7F) << 4) + line];
        uint8_t g = rom[(((code & 0x7F) << 4) | 0x800) + line];
        printf("  ");
        for (int b = 5; b >= 0; b--) fputc((t >> b) & 1 ? '#' : '.', stdout);
        printf("   %02X        ", t);
        for (int b = 5; b >= 0; b--) fputc((g >> b) & 1 ? '#' : '.', stdout);
        printf("   %02X\n", g);
    }
}

int main(int argc, char **argv) {
    const char *rom_path = "abc802/resources/rom/ABC802-char.6490191-01.bin";
    const char *png_path = NULL;
    bool eighty = true;

    for (int i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-h") || !strcmp(argv[i], "--help")) {
            printf("Usage: %s [options]\n\n", argv[0]);
            printf("Renders a synthetic ABC802 screen exercising every row\n");
            printf("attribute, which a real boot screen never does.\n\n");
            printf("  --rom FILE   character ROM (default: %s)\n", rom_path);
            printf("  --png FILE   also write the render as a PNG\n");
            printf("  --columns 40|80  column mode (default 80)\n");
            return 0;
        } else if (!strcmp(argv[i], "--rom") && i + 1 < argc) {
            rom_path = argv[++i];
        } else if (!strcmp(argv[i], "--png") && i + 1 < argc) {
            png_path = argv[++i];
        } else if (!strcmp(argv[i], "--columns") && i + 1 < argc) {
            eighty = atoi(argv[++i]) == 80;
        } else {
            fprintf(stderr, "Unknown option '%s'\n", argv[i]);
            return 1;
        }
    }

    static uint8_t rom[CHAR_ROM_SIZE];
    static uint8_t ram[CHAR_RAM_SIZE];
    if (!load_char_rom(rom_path, rom)) return 1;

    printf("=== Font decode: 'A', and a mosaic code where the halves differ ===\n");
    print_glyph(rom, 'A');
    print_glyph(rom, 0x60);

    printf("\n=== Attribute codes found in this ROM ===\n");
    int found = 0;
    for (int code = 0; code < 0x80; code++) {
        uint8_t b = rom[code << 4];
        if (b & 0x80) {
            static const char *names[] = {"Row Graphic", "Row Flash", "Row Clear", "undefined"};
            printf("  0x%02X -> %-12s = %d\n", code, names[b & 3], (b & 0x40) ? 1 : 0);
            found++;
        }
    }
    printf("  (%d attribute codes)\n", found);

    // A synthetic screen using every attribute. 12 columns is enough to
    // show each effect and keeps the ASCII art readable.
    const int cols = 80, rows = 7;
    memset(ram, 0x20, sizeof(ram)); // spaces

    put_text(ram, cols, 0, 0, "PLAIN", 0x00);
    put_text(ram, cols, 1, 0, "INVERSE", 0x80);      // bit 7 = per-character INV
    put_byte(ram, cols, 2, 0, ROW_GRAPHIC_ON);
    put_text(ram, cols, 2, 1, "01234567", 0x00);     // mosaics, not digits
    put_byte(ram, cols, 3, 0, ROW_FLASH_ON);
    put_text(ram, cols, 3, 1, "FLASH", 0x00);
    put_byte(ram, cols, 4, 0, ROW_CLEAR_ON);
    put_text(ram, cols, 4, 1, "CLEARED", 0x00);
    // Row Graphic switched off partway along the row. Digits, not letters:
    // only codes 0x21-0x3F and 0x60-0x7F differ between the ROM's two
    // halves, so a row of uppercase letters would render identically
    // either way and would silently "pass" even with the attribute
    // ignored entirely.
    put_byte(ram, cols, 5, 0, ROW_GRAPHIC_ON);
    put_text(ram, cols, 5, 1, "012", 0x00);          // mosaics
    put_byte(ram, cols, 5, 4, ROW_GRAPHIC_OFF);
    put_text(ram, cols, 5, 5, "012", 0x00);          // real digits again
    put_text(ram, cols, 6, 0, "CURSOR", 0x00);

    Abc802Screen screen = {
        .char_rom = rom,
        .char_ram = ram,
        .cols = cols,
        .rows = rows,
        .start = 0,
        .eighty_column = eighty,
        .cursor_addr = 6 * cols + 3,  // on the 'S' of CURSOR
        .flash_on = false,
    };

    static uint8_t pixels[ABC802_MAX_PIXELS];
    int w = abc802_pixel_width(&screen);
    int h = abc802_pixel_height(&screen);

    // Only the leftmost part is interesting; the rest is blank.
    int show_w = eighty ? 90 : 180;
    if (show_w > w) show_w = w;

    for (int phase = 0; phase < 2; phase++) {
        screen.flash_on = (phase != 0);
        memset(pixels, 0, sizeof(pixels));
        if (!abc802_render_pixels(&screen, pixels, sizeof(pixels))) {
            fprintf(stderr, "render failed\n");
            return 1;
        }
        printf("\n=== Render, FLSH clock %s (%dx%d, showing left %d px) ===\n",
               screen.flash_on ? "high - flashing row blanked" : "low - flashing row visible",
               w, h, show_w);
        // Print a cropped view row by row.
        for (int y = 0; y < h; y++) {
            for (int x = 0; x < show_w; x++) {
                fputc(pixels[(size_t)y * (size_t)w + (size_t)x] ? '#' : '.', stdout);
            }
            fputc('\n', stdout);
        }
    }

    if (png_path) {
        static const uint8_t amber[3] = {247, 170, 0};
        static const uint8_t black[3] = {0, 0, 0};
        if (!abc802_write_png(png_path, pixels, w, h, amber, black)) {
            fprintf(stderr, "Failed to write '%s'\n", png_path);
            return 1;
        }
        printf("\nWrote %s (%dx%d)\n", png_path, w, h);
    }

    return 0;
}
