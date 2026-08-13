// abc80/emu/src/chargen_dump.c - standalone verification tool for
// chargen.c's ROM decode: prints every one of the 128 addressable
// characters as ASCII art, one glyph per block, so the decode can be
// visually confirmed correct rather than just assumed from a formula that
// happens to compile. See chargen.c's own top comment for the primary
// source this is grounded against and what verifying it turned up (the
// Swedish/Finnish national-charset glyphs in the punctuation range).
//
// Usage: abc80-chargen-dump [rom_path]  (default: resources/rom/chargen.bin,
// i.e. run from inside abc80/, matching bin/abc80's own convention).

#include <stdio.h>
#include <stdlib.h>
#include <ctype.h>

#include "chargen.h"

int main(int argc, char *argv[]) {
    const char *rom_path = (argc > 1) ? argv[1] : "resources/rom/chargen.bin";

    static uint8_t rom[ABC80_CHARGEN_ROM_SIZE];
    if (!abc80_chargen_load(rom_path, rom)) {
        return EXIT_FAILURE;
    }

    for (int ch = 0; ch < 128; ch++) {
        char label = (isprint(ch)) ? (char)ch : '?';
        printf("--- char 0x%02X ('%c') ---\n", ch, label);
        for (int row = 0; row < ABC80_CHARGEN_CHAR_HEIGHT - 1; row++) {
            uint8_t data = abc80_chargen_row(rom, (uint8_t)ch, (uint8_t)row);
            for (int bit = 0; bit < ABC80_CHARGEN_CHAR_WIDTH; bit++) {
                putchar((data & 0x80) ? '#' : '.');
                data <<= 1;
            }
            putchar('\n');
        }
        putchar('\n');
    }

    return EXIT_SUCCESS;
}
