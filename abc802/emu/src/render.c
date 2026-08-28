// abc802/emu/src/render.c - text-screen rendering from character RAM.
//
// The ABC802 is a character-cell machine with no bitmap mode (that is the
// main thing separating it from the ABC800M/806), so a text dump is a
// genuine, complete rendering of what the screen shows - not the
// approximation abc80/emu/src/render.c has to make for ABC80's GRAPHICS
// mode block mosaics.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
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
//
// One table drives both directions - the output decode below and the
// keyboard-input encode abc802_charset_byte_for_codepoint() does for
// --interactive - specifically so the two can never drift apart. A
// display that renders Å and a keyboard that cannot type it would be a
// silent, confusing half-feature.
static const struct {
    uint8_t code;
    uint32_t codepoint;
    const char *utf8;
} ABC802_CHARSET[] = {
    {0x40, 0x00C9, "É"},
    {0x5B, 0x00C4, "Ä"},
    {0x5C, 0x00D6, "Ö"},
    {0x5D, 0x00C5, "Å"},
    {0x5E, 0x00DC, "Ü"},
    {0x60, 0x00E9, "é"},
    {0x7B, 0x00E4, "ä"},
    {0x7C, 0x00F6, "ö"},
    {0x7D, 0x00E5, "å"},
    {0x7E, 0x00FC, "ü"},
};
#define ABC802_CHARSET_LEN (sizeof(ABC802_CHARSET) / sizeof(ABC802_CHARSET[0]))

static const char *abc802_char_to_utf8(uint8_t code) {
    for (size_t i = 0; i < ABC802_CHARSET_LEN; i++) {
        if (ABC802_CHARSET[i].code == (code & 0x7F)) return ABC802_CHARSET[i].utf8;
    }
    return NULL;
}

int abc802_charset_byte_for_codepoint(uint32_t codepoint) {
    for (size_t i = 0; i < ABC802_CHARSET_LEN; i++) {
        if (ABC802_CHARSET[i].codepoint == codepoint) return ABC802_CHARSET[i].code;
    }
    return -1;
}

// Emit one character cell's glyph. `code` is the raw character-RAM byte,
// bit 7 included - the caller decides whether that bit means anything.
static void abc802_put_char(FILE *out, uint8_t code) {
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

// The CRTC's own account of the screen geometry, shared by both renderers
// below. R1 (horizontal displayed) and R6 (vertical displayed) are what
// the ROM actually programmed rather than a hardcoded 80x24; R12/R13 give
// the display start address, which is how the ROM scrolls - it moves the
// window rather than copying characters around.
//
// In 40-column mode the CRTC still counts 80 character cells per row; the
// video hardware draws every *other* cell at double width and skips the
// one between. So the ROM lays its text out in the even cells, and
// rendering all 80 would show a space after every character. Stepping by
// two is the text-mode equivalent of the double-width draw.
typedef struct {
    int cols, rows, start, step, drawn;
} Abc802Geometry;

static bool abc802_geometry(Abc802Geometry *g) {
    g->cols = abc802_crtc_reg(1);
    g->rows = abc802_crtc_reg(6);
    g->start = ((abc802_crtc_reg(12) & 0x3F) << 8) | abc802_crtc_reg(13);
    g->step = abc802_80_column() ? 1 : 2;
    g->drawn = (g->cols + g->step - 1) / g->step;
    return g->cols > 0 && g->rows > 0;
}

void abc802_render_text_screen(FILE *out) {
    const uint8_t *ram = abc802_char_ram();
    Abc802Geometry g;

    if (!abc802_geometry(&g)) {
        fprintf(out, "(CRTC not programmed - no display to render)\n");
        return;
    }

    fprintf(out, "+");
    for (int x = 0; x < g.drawn; x++) fprintf(out, "-");
    fprintf(out, "+\n");

    for (int y = 0; y < g.rows; y++) {
        fprintf(out, "|");
        for (int x = 0; x < g.cols; x += g.step) {
            // Bit 7 is the per-character inverse-video flag, not part of
            // the character code.
            abc802_put_char(out, ram[(g.start + y * g.cols + x) & 0x7FF]);
        }
        fprintf(out, "|\n");
    }

    fprintf(out, "+");
    for (int x = 0; x < g.drawn; x++) fprintf(out, "-");
    fprintf(out, "+\n");
}

// MC6845 R10 (cursor start) bits 6:5 select the cursor mode: 00 non-blink
// (displayed steadily), 01 non-display, 10 blink at 1/16 the field rate,
// 11 blink at 1/32. The ABC802 ROM only ever uses 00 and 01, toggling
// between them from its own clock interrupt to blink the cursor in
// software (see render.h). The two hardware blink modes are treated as
// "visible" here rather than ignored: nothing in this ROM selects them,
// but showing a cursor that exists beats hiding one that does.
#define ABC802_CRTC_CURSOR_MODE_MASK      0x60
#define ABC802_CRTC_CURSOR_MODE_NODISPLAY 0x20

int abc802_cursor_address(void) {
    if ((abc802_crtc_reg(10) & ABC802_CRTC_CURSOR_MODE_MASK) == ABC802_CRTC_CURSOR_MODE_NODISPLAY) {
        return -1;
    }
    return ((abc802_crtc_reg(14) << 8) | abc802_crtc_reg(15)) & 0x7FF;
}

void abc802_render_frame(FILE *out) {
    const uint8_t *ram = abc802_char_ram();
    Abc802Geometry g;

    fputs("\x1b[H\x1b[2J", out); // ANSI home cursor + clear screen

    if (!abc802_geometry(&g)) {
        fprintf(out, "(CRTC not programmed - no display yet)\n");
        fflush(out);
        return;
    }

    int cursor_addr = abc802_cursor_address();

    for (int y = 0; y < g.rows; y++) {
        for (int x = 0; x < g.cols; x += g.step) {
            int addr = (g.start + y * g.cols + x) & 0x7FF;
            uint8_t code = ram[addr];
            // Two independent reasons to draw a cell reversed: the
            // character's own inverse-video bit, and the cursor sitting
            // on it. Either one alone reverses; both together cancel,
            // which is what real inverse-video hardware does with a
            // cursor drawn on top of already-inverted text.
            int inverse = ((code & 0x80) != 0) ^ (addr == cursor_addr);
            if (inverse) fputs("\x1b[7m", out);
            abc802_put_char(out, code);
            if (inverse) fputs("\x1b[0m", out);
        }
        fputc('\n', out);
    }
    fflush(out);
}
