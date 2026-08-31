// abc802/emu/src/chargen.c - decoding the character generator ROM into
// pixels, including the row-attribute state machine.
//
// Ported from MAME's own abc802_update_row()
// (src/mame/luxor/abc80x_v.cpp), which is in turn annotated with the real
// PAL16R4 equations from the machine's video board - not inferred from the
// ROM contents, though every fact below was then cross-checked against the
// committed ROM image and matched.
//
// The scheme is unusual and worth stating plainly, because nothing about a
// memory map hints at it: **the character generator ROM's own output byte
// decides whether a cell is a character or an attribute command.** Fetch
// the byte for a cell; if bit 7 (ATE) is set it is not pixel data at all,
// but an instruction to change one of three row attributes - bit 6 (ATD)
// carries the new value and bits 1:0 pick which attribute (0 = Row
// Graphic, 1 = Row Flash, 2 = Row Clear; 3 is undefined and ignored). So
// the *font* is what defines which character codes act as attribute
// codes. In the committed ROM exactly 17 codes do: 0x01-0x09 and
// 0x11-0x18. An attribute cell draws no pixels of its own.
//
// The three attributes work by substituting the scanline address, not by
// post-processing pixels:
//   - Row Graphic ORs 0x800 into the ROM address, selecting the alternate
//     2K half of the 4K ROM - a block-mosaic font. (Verified: 63 codes,
//     0x21-0x3F and 0x60-0x7F, differ between the two halves; the rest are
//     byte-identical.)
//   - Row Flash and Row Clear force the scanline to 0x0E, which is blank
//     for every printable code (verified across the whole ROM).
//   - The cursor forces the scanline to 0x0F, which is 0x3F - a solid
//     6-pixel bar - for every printable code (also verified). So the real
//     cursor is a solid block substituted for the glyph, not an inversion
//     of it.
// Order matters: the cursor is applied first and flash/clear can then
// override it, exactly as MAME sequences them.
//
// Pixels are bits 5..0 of the ROM byte, most-significant first. MAME
// expresses this as `data <<= 2` followed by six rounds of `BIT(data, 7)`;
// the effect is identical and the shift-out form is kept here. Row
// attributes reset at the start of every scanline.

#include "chargen.h"

// From MAME's abc80x.h: the bit assignments this whole scheme rests on.
#define ABC802_ATE 0x80  // char ROM output: this byte is an attribute command
#define ABC802_ATD 0x40  // char ROM output: the attribute's new value
#define ABC802_INV 0x80  // character code: per-character inverse video

#define ABC802_ATTR_ROW_GRAPHIC 0x00
#define ABC802_ATTR_ROW_FLASH   0x01
#define ABC802_ATTR_ROW_CLEAR   0x02

// Scanline addresses the hardware substitutes rather than fetching the
// glyph's real row. See this file's header comment.
#define ABC802_RA_BLANK  0x0E
#define ABC802_RA_CURSOR 0x0F

bool abc802_render_pixels(const Abc802Screen *s, uint8_t *pixels, size_t capacity) {
    if (!s || !s->char_rom || !s->char_ram) return false;
    if (s->cols <= 0 || s->rows <= 0) return false;

    int width = abc802_pixel_width(s);
    int height = abc802_pixel_height(s);
    if ((size_t)width * (size_t)height > capacity) return false;

    // Scanline-major, exactly as the hardware scans and as MAME's
    // MC6845_UPDATE_ROW callback is invoked - once per scanline, sweeping
    // the whole row of columns, with the row attributes reset at the start
    // of every scanline. Rendering cell-major instead would give the same
    // picture for this ROM (an attribute code's byte is identical on every
    // scanline), but only by accident, and it would quietly diverge from a
    // font where that was not true.
    for (int row = 0; row < s->rows; row++) {
        for (int line = 0; line < ABC802_CHAR_HEIGHT; line++) {
            int rg = 0, rf = 0, rc = 0;
            int y = row * ABC802_CHAR_HEIGHT + line;

            for (int column = 0; column < s->cols; column++) {
                int addr = (s->start + row * s->cols + column) & 0x7FF;
                uint8_t code = s->char_ram[addr];

                int ri = (code & ABC802_INV) ? 1 : 0;
                uint16_t rom_addr = (uint16_t)((code & 0x7F) << 4);
                uint8_t ra = (uint8_t)line;

                if (s->cursor_addr >= 0 && addr == (s->cursor_addr & 0x7FF)) {
                    ra = ABC802_RA_CURSOR;
                }
                // Applied after the cursor, so a flashing or cleared row
                // hides the cursor rather than the other way round - the
                // order MAME sequences these in.
                if ((s->flash_on && rf) || rc) {
                    ra = ABC802_RA_BLANK;
                }
                if (rg) rom_addr |= 0x800;

                uint8_t data = s->char_rom[(rom_addr + ra) & 0xFFF];

                if (data & ABC802_ATE) {
                    // An attribute command rather than pixel data. The
                    // cell draws nothing at all - not even an inverted
                    // background - so the pixel loop is skipped entirely.
                    int value = (data & ABC802_ATD) ? 1 : 0;
                    switch (data & 0x03) {
                        case ABC802_ATTR_ROW_GRAPHIC: rg = value; break;
                        case ABC802_ATTR_ROW_FLASH:   rf = value; break;
                        case ABC802_ATTR_ROW_CLEAR:   rc = value; break;
                        default: break; // 0x03 is undefined
                    }
                    continue;
                }

                data = (uint8_t)(data << 2); // pixels are bits 5..0, MSB first

                // 40-column mode draws each character double-width and
                // then skips the cell after it, so a row is the same
                // number of pixels wide in either mode.
                int span = s->eighty_column ? 1 : 2;

                for (int bit = 0; bit < ABC802_CHAR_WIDTH; bit++) {
                    uint8_t color = (uint8_t)((((data & 0x80) != 0) ^ ri) ? 1 : 0);
                    data = (uint8_t)(data << 1);
                    for (int rep = 0; rep < span; rep++) {
                        int x = column * ABC802_CHAR_WIDTH + bit * span + rep;
                        if (x < width) pixels[(size_t)y * (size_t)width + (size_t)x] = color;
                    }
                }

                if (!s->eighty_column) column++;
            }
        }
    }

    return true;
}

// Real hardware toggles the FLSH clock in abc802_state::vs_w(): on each
// vertical sync it increments a counter and, once that counter reaches
// 0x20 (32), flips the clock and resets it - so a flip every 33 fields.
// At the 50 Hz frame rate this machine's DIP defaults to, that is
// 33/50 = 0.66s per phase, a full flash cycle of 1.32s (~0.76 Hz).
//
// This emulator models no vertical sync, so the same rate is expressed in
// T-states: 3,000,000 T-states/s / 50 fields/s = 60,000 per field, times
// 33 fields = 1,980,000 T-states per phase.
// The attribute walk, shared with the terminal renderer (render.c) so the
// two are one decode rather than two. See chargen.h for why reading
// scanline 0 is equivalent to what the pixel loop above does per scanline.
int abc802_decode_row(const uint8_t *char_rom, const uint8_t *codes, int count,
                      bool flash_on, Abc802Cell *cells, int max) {
    int rg = 0, rf = 0, rc = 0, n = 0;

    for (int column = 0; column < count; column++) {
        uint8_t code = codes[column];
        uint16_t rom_addr = (uint16_t)((code & 0x7F) << 4);
        // Mirrors the pixel loop, and deliberately so, but note that with
        // *this* font it changes nothing: every code that is an attribute
        // command in the alphanumeric font is the same command in the
        // mosaic one, so attribute detection lands identically either way.
        // Removing it breaks no test. It stays because the rule is "read
        // the font the row is in", not "read font 0", and a font that
        // distinguished them would be silently mis-decoded without it.
        if (rg) rom_addr |= 0x800;
        uint8_t data = char_rom[rom_addr & 0xFFF];

        if (data & ABC802_ATE) {
            int value = (data & ABC802_ATD) ? 1 : 0;
            switch (data & 0x03) {
                case ABC802_ATTR_ROW_GRAPHIC: rg = value; break;
                case ABC802_ATTR_ROW_FLASH:   rf = value; break;
                case ABC802_ATTR_ROW_CLEAR:   rc = value; break;
                default: break; // 0x03 is undefined
            }
            continue;   // an attribute cell draws nothing at all
        }

        if (n >= max) break;
        cells[n].code = code;
        cells[n].column = column;
        cells[n].graphic = rg != 0;
        cells[n].blanked = (flash_on && rf) || rc;
        n++;
    }
    return n;
}

#define ABC802_FLASH_PHASE_TSTATES 1980000

bool abc802_flash_phase(long long cycles) {
    if (cycles < 0) cycles = 0;
    return ((cycles / ABC802_FLASH_PHASE_TSTATES) & 1) != 0;
}
