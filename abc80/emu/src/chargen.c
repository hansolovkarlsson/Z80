// abc80/emu/src/chargen.c - decodes the ABC80's character-generator ROM
// (SN74S263N "Row Output Character Generator", board position "H2";
// abc80/resources/rom/chargen.bin, CRC32 9e064e91 - see that directory's
// own README.md for full provenance, including the important caveat that
// MAME's own source marks this dump BAD_DUMP/"created by hand", since the
// SN74S263 is a mask-programmed chip with no electronically readable
// contents).
//
// Address formula and dimensions are lifted directly from MAME's own
// device implementation (src/devices/video/sn74s262.cpp,
// sn74s262_device::read()):
//
//   u8 sn74s262_device::read(u8 character, u8 row)
//   {
//       if ((row & 0xf) > 8) return 0;
//       return m_char_rom[((character & 0x7f) * 10) + (row & 0xf)];
//   }
//
// i.e. 128 characters (top bit of the code ignored) x 10 bytes each, but
// only rows 0-8 hold real data - row 9 (and the rest of the 4-bit `row & 0xf`
// range) is always blank, giving each character cell one blank row of
// vertical spacing for free without it needing to be stored explicitly.
// Each byte's top ABC80_CHARGEN_CHAR_WIDTH (6) bits are the pixel row,
// MSB-first/leftmost-first - confirmed against src/mame/luxor/abc80_v.cpp's
// draw_character(): `bool color = BIT(data, 7); ... data <<= 1;` shifted
// exactly 6 times. The low 2 bits of every stored byte are unused.
//
// Verified (not just "matches MAME's own possibly-wrong dump"): decoding
// this ROM produces clean, correct letterforms for A/B/0/S/!, and character
// 0x5B (`[` in plain ASCII) decodes to a clear "A with umlaut dots" glyph -
// independent confirmation both that the formula is right and that this
// really is the SN74S263's documented Swedish/Finnish national-charset
// variant, not plain ASCII, exactly as MAME's own device-type comment
// states (`// Swedish/Finnish`).

#include <stdio.h>
#include "chargen.h"

int abc80_chargen_load(const char *path, uint8_t *rom_out) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open chargen ROM '%s': ", path);
        perror(NULL);
        return 0;
    }

    fseek(f, 0, SEEK_END);
    long size = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (size != ABC80_CHARGEN_ROM_SIZE) {
        fprintf(stderr, "Chargen ROM '%s' is %ld bytes, expected exactly %d\n",
                path, size, ABC80_CHARGEN_ROM_SIZE);
        fclose(f);
        return 0;
    }

    size_t read = fread(rom_out, 1, ABC80_CHARGEN_ROM_SIZE, f);
    fclose(f);
    if (read != ABC80_CHARGEN_ROM_SIZE) {
        fprintf(stderr, "Short read loading chargen ROM '%s'\n", path);
        return 0;
    }
    return 1;
}

uint8_t abc80_chargen_row(const uint8_t *rom, uint8_t character, uint8_t row) {
    if ((row & 0x0F) > 8) {
        return 0;
    }
    return rom[((character & 0x7F) * 10) + (row & 0x0F)];
}
