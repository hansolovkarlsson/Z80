// abc806/emu/src/text.c - the character set, and the screen as characters.
//
// See text.h for why this is separate from render.c.

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "chargen.h"
#include "text.h"

// ABC806 character codes follow the same Swedish/Finnish ISO 646 variant
// (SEN 850200 Annex B) as the ABC80 and ABC802: the positions ASCII uses
// for []\^`{|}~ carry ÄÖÅÜ / äöåü, and @ carries É.
//
// This is the *third* copy of these ten mappings in the repository, which
// is one more than the rule abc802/emu/src/render.c wrote down for itself:
// "if a third consumer ever appears, this is the point to lift it into a
// common file". That consumer is this file. It is deliberately not lifted
// yet, and the reason is worth stating rather than leaving as an
// oversight: the shared thing would be a table plus a UTF-8 decoder plus a
// codepoint encoder, and where it should live depends on whether the
// ABC806 ends up sharing more with the ABC802 than a character set - which
// is the same question emu/src/ports.c's duplication is waiting on. Both
// get answered at once, or neither honestly can be.
//
// One table drives both directions - the display decode and the keyboard
// encode - so a screen that can show Å and a keyboard that cannot type it
// is not a reachable state.
static const struct {
    uint8_t code;
    uint32_t codepoint;
    const char *utf8;
} ABC806_CHARSET[] = {
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
#define ABC806_CHARSET_LEN (sizeof(ABC806_CHARSET) / sizeof(ABC806_CHARSET[0]))

static const char *char_to_utf8(uint8_t code) {
    for (size_t i = 0; i < ABC806_CHARSET_LEN; i++)
        if (ABC806_CHARSET[i].code == (code & 0x7F)) return ABC806_CHARSET[i].utf8;
    return NULL;
}

int abc806_charset_byte_for_codepoint(uint32_t codepoint) {
    for (size_t i = 0; i < ABC806_CHARSET_LEN; i++)
        if (ABC806_CHARSET[i].codepoint == codepoint) return ABC806_CHARSET[i].code;
    return -1;
}

size_t abc806_utf8_to_chars(const char *utf8, uint8_t *out, size_t out_size) {
    size_t n = 0;
    for (const unsigned char *p = (const unsigned char *)utf8; *p && n < out_size; ) {
        unsigned char c = *p++;
        if (c < 0x80) {
            // Newline in a host string means "press Return", which the
            // machine sees as CR.
            out[n++] = (c == '\n') ? 0x0D : c;
        } else if (c >= 0xC2 && c <= 0xDF && (*p & 0xC0) == 0x80) {
            // This machine's whole character set is in the Latin-1
            // Supplement block, which UTF-8 always encodes in exactly two
            // bytes, so no 3-/4-byte lead bytes need handling.
            uint32_t cp = ((uint32_t)(c & 0x1F) << 6) | (uint32_t)(*p++ & 0x3F);
            int byte = abc806_charset_byte_for_codepoint(cp);
            if (byte >= 0) out[n++] = (uint8_t)byte;
            // A codepoint the machine has no character for is dropped,
            // matching what the interactive keyboard path does with one.
        } else {
            // Malformed or out of range: skip its continuation bytes
            // rather than feeding them to the ROM as characters.
            while ((*p & 0xC0) == 0x80) p++;
        }
    }
    return n;
}

static void put_char(FILE *out, uint8_t code) {
    uint8_t ch = code & 0x7F;
    const char *utf8 = char_to_utf8(ch);
    if (utf8) fputs(utf8, out);
    else if (ch >= 0x20 && ch < 0x7F) fputc(ch, out);
    else fputc(' ', out);
}

void abc806_text_screen(FILE *out, const Abc806Screen *s) {
    if (!s || s->columns <= 0 || s->rows <= 0) {
        fprintf(out, "(CRTC not programmed - no display to render)\n");
        return;
    }

    // The frame is as wide as the widest row actually drew, not as wide as
    // the column count: a row of double-width text draws half as many
    // cells. Measured rather than assumed, so the box always closes.
    int widest = 0;
    for (int row = 0; row < s->rows; row++) {
        Abc806Cell cells[ABC806_MAX_COLUMNS];
        int n = abc806_decode_row(s, row, cells, ABC806_MAX_COLUMNS);
        if (n > widest) widest = n;
    }

    fputc('+', out);
    for (int x = 0; x < widest; x++) fputc('-', out);
    fputs("+\n", out);

    for (int row = 0; row < s->rows; row++) {
        Abc806Cell cells[ABC806_MAX_COLUMNS];
        int n = abc806_decode_row(s, row, cells, ABC806_MAX_COLUMNS);
        fputc('|', out);
        for (int i = 0; i < n; i++) put_char(out, cells[i].code);
        for (int i = n; i < widest; i++) fputc(' ', out);
        fputs("|\n", out);
    }

    fputc('+', out);
    for (int x = 0; x < widest; x++) fputc('-', out);
    fputs("+\n", out);
}

void abc806_ansi_frame(FILE *out, const Abc806Screen *s) {
    if (!s || s->columns <= 0 || s->rows <= 0) {
        fputs("(CRTC not programmed - no display yet)\n", out);
        return;
    }

    for (int row = 0; row < s->rows; row++) {
        Abc806Cell cells[ABC806_MAX_COLUMNS];
        int n = abc806_decode_row(s, row, cells, ABC806_MAX_COLUMNS);

        // Track what is already selected so a run of same-coloured cells
        // emits one escape rather than one per character. A screen is
        // mostly long runs, and at 30 frames a second that is the terminal
        // keeping up or not.
        int cur_fg = -1, cur_bg = -1, cur_ul = -1;

        for (int i = 0; i < n; i++) {
            const Abc806Cell *c = &cells[i];
            int fg = c->fg, bg = c->bg;

            // Flash is a real attribute here, not a terminal one: the pen
            // drops to the background for half the cycle. Driven this way
            // rather than with ANSI blink deliberately - blink is widely
            // ignored or rendered as bold, and it would flash out of step
            // with the phase a screenshot was taken at.
            if (c->flash && !s->flash_on) fg = bg;

            // On real hardware the cursor replaces the glyph with a solid
            // bar. A terminal cannot draw that inside a cell, so it gets
            // the nearest honest equivalent: reversed colours.
            if (fg != cur_fg || bg != cur_bg || c->underline != cur_ul) {
                fprintf(out, "\x1b[0m\x1b[%d;%dm", 30 + (fg & 7), 40 + (bg & 7));
                if (c->underline) fputs("\x1b[4m", out);
                cur_fg = fg; cur_bg = bg; cur_ul = c->underline;
            }
            if (c->cursor) fputs("\x1b[7m", out);
            put_char(out, c->code);
            if (c->cursor) { fputs("\x1b[0m", out); cur_fg = cur_bg = cur_ul = -1; }
        }
        fputs("\x1b[0m\n", out);
    }
}
