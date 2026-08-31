// abc802/emu/src/render.c - text-screen rendering from character RAM.
//
// The ABC802 is a character-cell machine with no bitmap mode (that is the
// main thing separating it from the ABC800M/806), but a text dump is still
// not simply one glyph per character code: the Row Graphic attribute
// switches the whole rest of a row to a mosaic font, and Row Flash and Row
// Clear blank it. Those go through the same attribute walk the pixel
// renderer uses (abc802_decode_row(), chargen.c), and the mosaics are
// drawn with Unicode sextants - the approximation abc80/emu/src/render.c
// makes for ABC80's GRAPHICS mode, needed here for the same reason.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "render.h"
#include "chargen.h"
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

size_t abc802_utf8_to_chars(const char *utf8, uint8_t *out, size_t out_size) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p && n < out_size; ) {
        unsigned char c = *p++;
        if (c < 0x80) {
            // Newline in a host string means "press Return", which the
            // machine sees as CR - the same rewrite every caller of this
            // used to do by hand.
            out[n++] = (c == '\n') ? 0x0D : c;
        } else if (c >= 0xC2 && c <= 0xDF && (*p & 0xC0) == 0x80) {
            // The ABC802's whole character set lives in the Latin-1
            // Supplement block, which UTF-8 always encodes in exactly two
            // bytes, so no 3-/4-byte lead bytes need handling here.
            uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(*p++ & 0x3F);
            int byte = abc802_charset_byte_for_codepoint(cp);
            if (byte >= 0) out[n++] = (uint8_t)byte;
            // A codepoint this machine has no character for is dropped,
            // matching what the interactive keyboard path does with one.
        } else {
            // A malformed or out-of-range sequence: skip its continuation
            // bytes rather than feeding them to the ROM as characters.
            while ((*p & 0xC0) == 0x80) p++;
        }
    }
    return n;
}

static void abc802_put_char(FILE *out, uint8_t code);

// Row Graphic draws from the alternate font, which is a teletext 2x3
// block mosaic: 6 pixels wide split 3+3, ten scanlines split 3+4+3. Six
// cells, and the character code carries them in bits 0,1,2,3,4 and *6* -
// bit 5 is skipped, because in teletext it is what separates the graphics
// codes from the alphanumeric ones. Verified by rendering the font's own
// glyphs out of the ROM rather than assumed from the standard: 0x21 is
// top-left alone, 0x60 bottom-right alone, 0x7F all six.
static uint8_t abc802_mosaic_cells(uint8_t code) {
    return (uint8_t)((code & 0x1F) | ((code & 0x40) >> 1));
}

// Six mosaic cells to a Unicode codepoint, in reading order:
// bit0=top-left, bit1=top-right, bit2=mid-left, bit3=mid-right,
// bit4=bottom-left, bit5=bottom-right.
//
// Unicode's "Symbols for Legacy Computing" sextant block (U+1FB00-1FB3B)
// omits the four patterns that already had characters - empty, full, and
// the two half blocks - so the index has to skip them.
//
// Deliberately a near-copy of abc80/emu/src/render.c's own
// abc80_sextant_codepoint(), not a shared helper. The two targets have no
// build-time relationship, the callers differ (that one's cells arrive in
// bits 0-5 already, this one has teletext's bit-6 quirk to undo), and
// twelve lines is cheaper than inventing a shared dependency for them.
// This is the same rule the charset table above is kept under, and the
// same one abc806/emu/src/ports.c is: a third consumer is the trigger to
// lift it out.
static uint32_t abc802_sextant_codepoint(uint8_t cells) {
    if (cells == 0x00) return 0x20;    // SPACE
    if (cells == 0x3F) return 0x2588;  // FULL BLOCK
    if (cells == 0x15) return 0x258C;  // LEFT HALF BLOCK  (cells 1,3,5)
    if (cells == 0x2A) return 0x2590;  // RIGHT HALF BLOCK (cells 2,4,6)
    int index = (int)cells - 1;
    if (cells > 0x15) index--;
    if (cells > 0x2A) index--;
    return 0x1FB00u + (uint32_t)index;
}

// Minimal UTF-8 encoder, for the sextant codepoints above - everything
// else this file emits comes out of the charset table as literal UTF-8.
static void abc802_put_codepoint(FILE *out, uint32_t cp) {
    if (cp < 0x80) {
        fputc((int)cp, out);
    } else if (cp < 0x800) {
        fputc((int)(0xC0 | (cp >> 6)), out);
        fputc((int)(0x80 | (cp & 0x3F)), out);
    } else if (cp < 0x10000) {
        fputc((int)(0xE0 | (cp >> 12)), out);
        fputc((int)(0x80 | ((cp >> 6) & 0x3F)), out);
        fputc((int)(0x80 | (cp & 0x3F)), out);
    } else {
        fputc((int)(0xF0 | (cp >> 18)), out);
        fputc((int)(0x80 | ((cp >> 12) & 0x3F)), out);
        fputc((int)(0x80 | ((cp >> 6) & 0x3F)), out);
        fputc((int)(0x80 | (cp & 0x3F)), out);
    }
}

// One resolved cell. A blanked cell (Row Flash on its dark phase, or Row
// Clear) draws nothing at all, and a Row Graphic cell draws a mosaic
// rather than the glyph its code would otherwise name.
static void abc802_put_cell(FILE *out, const Abc802Cell *cell) {
    if (cell->attribute || cell->blanked) {
        fputc(' ', out);
    } else if (cell->graphic) {
        abc802_put_codepoint(out, abc802_sextant_codepoint(
            abc802_mosaic_cells(cell->code & 0x7F)));
    } else {
        abc802_put_char(out, cell->code);
    }
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

// The CRTC counts at most 80 cells per row in either column mode, so this
// bounds every per-row cell buffer below.
#define ABC802_MAX_COLS 80

// Read one row out of character RAM and run the attribute walk over it.
// The walk sees *every* column, not the stepped subset the 40-column
// renderers draw: an attribute code occupies a real cell wherever the ROM
// put it, and skipping half of them would lose it.
static int abc802_decode_row_at(const Abc802Geometry *g, const uint8_t *ram,
                                int row, bool flash_on, Abc802Cell *cells) {
    uint8_t codes[ABC802_MAX_COLS];
    int count = g->cols < ABC802_MAX_COLS ? g->cols : ABC802_MAX_COLS;
    for (int x = 0; x < count; x++) {
        codes[x] = ram[(g->start + row * g->cols + x) & 0x7FF];
    }
    return abc802_decode_row(abc802_char_rom(), codes, count,
                             abc802_80_column(), flash_on,
                             cells, ABC802_MAX_COLS);
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
        Abc802Cell cells[ABC802_MAX_COLS];
        int n = abc802_decode_row_at(&g, ram, y, false, cells);
        fprintf(out, "|");
        // One character per *drawn cell*, in order. Not indexed by column:
        // in 40-column mode a drawn character consumes the cell after it
        // and an attribute code does not, so column numbers and visual
        // positions come apart the moment a row carries an attribute.
        for (int i = 0; i < n; i++) abc802_put_cell(out, &cells[i]);
        for (int i = n; i < g.drawn; i++) fputc(' ', out);
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

void abc802_render_frame(FILE *out, bool flash_on) {
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
        Abc802Cell cells[ABC802_MAX_COLS];
        int n = abc802_decode_row_at(&g, ram, y, flash_on, cells);
        for (int i = 0; i < n; i++) {
            int addr = (g.start + y * g.cols + cells[i].column) & 0x7FF;
            uint8_t code = cells[i].code;
            // Two independent reasons to draw a cell reversed: the
            // character's own inverse-video bit, and the cursor sitting
            // on it. Either one alone reverses; both together cancel,
            // which is what real inverse-video hardware does with a
            // cursor drawn on top of already-inverted text.
            int inverse = ((code & 0x80) != 0) ^ (addr == cursor_addr);
            if (inverse) fputs("\x1b[7m", out);
            abc802_put_cell(out, &cells[i]);
            if (inverse) fputs("\x1b[0m", out);
        }
        fputc('\n', out);
    }
    fflush(out);
}
