// abc802/emu/src/render.c - text-screen rendering from character RAM.
//
// The ABC802 is a character-cell machine with no bitmap mode (that is the
// main thing separating it from the ABC800M/806), so a text dump is a
// genuine, complete rendering of what the screen shows - not the
// approximation abc80/emu/src/render.c has to make for ABC80's GRAPHICS
// mode block mosaics.

#include <stdio.h>

#include "render.h"
#include "memory.h"
#include "ports.h"

// ABC802 character codes follow the same Swedish/Finnish ISO 646 variant
// as the ABC80 (SEN 850200 Annex B): the positions ASCII uses for
// []\^`{|}~ carry ÄÖÅÜ / äöåü instead, and @ carries É. Kept as its own
// table rather than shared with abc80/emu/src/charset.c: the two targets
// have no build-time relationship today, and duplicating eleven mappings
// is cheaper than inventing a shared dependency for them. If a third
// consumer ever appears, this is the point to lift it into a common file -
// the same rule z80core/ itself was moved under.
static const char *abc802_char_to_utf8(uint8_t code) {
    switch (code & 0x7F) {
        case 0x40: return "É";
        case 0x5B: return "Ä";
        case 0x5C: return "Ö";
        case 0x5D: return "Å";
        case 0x5E: return "Ü";
        case 0x60: return "é";
        case 0x7B: return "ä";
        case 0x7C: return "ö";
        case 0x7D: return "å";
        case 0x7E: return "ü";
        default:   return NULL;
    }
}

void abc802_render_text_screen(FILE *out) {
    const uint8_t *ram = abc802_char_ram();

    // R1 (horizontal displayed) and R6 (vertical displayed) are the CRTC's
    // own account of the screen size, so the dump follows whatever the ROM
    // actually programmed rather than a hardcoded 80x24. R12/R13 give the
    // start address, which is how the ROM scrolls: it moves the window
    // rather than copying characters around.
    int cols = abc802_crtc_reg(1);
    int rows = abc802_crtc_reg(6);
    int start = ((abc802_crtc_reg(12) & 0x3F) << 8) | abc802_crtc_reg(13);

    if (cols <= 0 || rows <= 0) {
        fprintf(out, "(CRTC not programmed - no display to render)\n");
        return;
    }

    // In 40-column mode the CRTC still counts 80 character cells per row;
    // the video hardware draws every *other* cell at double width and
    // skips the one between. So the ROM lays its text out in the even
    // cells, and rendering all 80 would show a space after every
    // character. Stepping by two is the text-mode equivalent of the
    // double-width draw.
    int step = abc802_80_column() ? 1 : 2;

    int drawn = (cols + step - 1) / step;

    fprintf(out, "+");
    for (int x = 0; x < drawn; x++) fprintf(out, "-");
    fprintf(out, "+\n");

    for (int y = 0; y < rows; y++) {
        fprintf(out, "|");
        for (int x = 0; x < cols; x += step) {
            uint8_t code = ram[(start + y * cols + x) & 0x7FF];
            // Bit 7 is the per-character inverse-video flag, not part of
            // the character code.
            uint8_t ch = code & 0x7F;
            const char *utf8 = abc802_char_to_utf8(ch);
            if (utf8) {
                fprintf(out, "%s", utf8);
            } else if (ch >= 0x20 && ch < 0x7F) {
                fputc(ch, out);
            } else {
                fputc(' ', out);
            }
        }
        fprintf(out, "|\n");
    }

    fprintf(out, "+");
    for (int x = 0; x < drawn; x++) fprintf(out, "-");
    fprintf(out, "+\n");
}
