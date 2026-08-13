// abc80/emu/src/render.c - renders ABC80 video RAM to a terminal as UTF-8
// text: the terminal-based display backend chosen for Milestone 2 (see
// abc80/docs/ABC80_ROADMAP.md).
//
// Deliberately simpler than a real pixel framebuffer: rather than
// stepping through all the individual scanlines the way MAME's
// draw_scanline()/draw_character() do (necessary for a real CRT, which
// draws 6x10 individual pixels per character), this renders one whole
// Unicode glyph per character cell directly - correct and much simpler
// for a text terminal, which can't address individual pixels anyway. The
// per-character MODE state machine (TEXT/GRAPHICS row-attribute toggle)
// and per-character attribute lookup are still ported faithfully from
// MAME's draw_scanline()/draw_character() (src/mame/luxor/abc80_v.cpp),
// since those determine WHICH glyph to print, not how it's drawn pixel by
// pixel:
//
//   if (!j && k) m_mode = 0;
//   if (j && !k) m_mode = 1;
//   if (j && k) m_mode = !m_mode;
//   if (m_mode & versal) { graphics mode } else { text mode }
//
// reset to m_mode=0 at the start of every row (real hardware's K5_LINE_END
// reset, see video_timing.c), and looked up once per character using the
// "active display" half of the attribute PROM address space (see
// video_timing.c's own attr-polarity comment) - not the border half,
// since every character in this renderer's 24x40 grid is by definition
// on-screen.
//
// GRAPHICS mode block characters: ABC80's 2(col)x3(row) block-mosaic
// system (6 independently-settable sub-cells per character - videoram
// bits {0,2,4} for the left column's top/mid/bottom and {1,3,6} for the
// right column's, ported from draw_character()'s own c0..c5 derivation;
// bit 5 is unused, bit 7 is the cursor flag) maps directly onto Unicode's
// "Symbols for Legacy Computing" sextant block (U+1FB00-U+1FB3B, Unicode
// 13+): its BLOCK SEXTANT-<cells> characters number the six sub-cells
// 1=top-left, 2=top-right, 3=mid-left, 4=mid-right, 5=bottom-left,
// 6=bottom-right - the identical reading-order numbering and 2x3 layout
// ABC80's own hardware uses. abc80_sextant_codepoint()'s formula below was
// derived from, and cross-checked against, Unicode's own published
// codepoint table (compart.com's U+1FB00 block listing) at multiple
// points spanning its full range (both ends and two interior values), not
// assumed from the bit pattern alone. Four of the 64 combinations
// (all-blank, all-filled, left-column-only, right-column-only) reuse
// pre-existing characters (SPACE, FULL BLOCK, LEFT/RIGHT HALF BLOCK)
// instead of a dedicated sextant codepoint, which is why only 60 (not 64)
// codepoints exist in the U+1FB00 range and why the formula has to
// special-case those four bitmasks.
//
// Rendering correctness for either mode depends on a terminal font with
// sextant-glyph coverage (not all do, as of this writing) - a font that
// falls back to tofu/replacement boxes for those codepoints is a font
// limitation, not a bug in this decode.

#include <stdint.h>
#include <stdio.h>

#include "render.h"
#include "video_timing.h"
#include "charset.h"

// See this file's own top comment for the derivation and cross-check
// against Unicode's published U+1FB00 sextant table.
static uint32_t abc80_sextant_codepoint(uint8_t cells) {
    // cells: bit0=top-left(1), bit1=top-right(2), bit2=mid-left(3),
    // bit3=mid-right(4), bit4=bottom-left(5), bit5=bottom-right(6).
    if (cells == 0x00) return 0x20;    // SPACE
    if (cells == 0x3F) return 0x2588;  // FULL BLOCK
    if (cells == 0x15) return 0x258C;  // LEFT HALF BLOCK (cells 1,3,5)
    if (cells == 0x2A) return 0x2590;  // RIGHT HALF BLOCK (cells 2,4,6)
    int index = (int)cells - 1;
    if (cells > 0x15) index--;
    if (cells > 0x2A) index--;
    return 0x1FB00u + (uint32_t)index;
}

// Minimal UTF-8 encoder for the codepoint ranges this file actually
// produces (ASCII, Latin-1 Swedish letters, U+2500-block box characters,
// and the U+1FBxx sextants) - not a general-purpose one.
static void put_utf8(FILE *out, uint32_t cp) {
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

void abc80_render_frame(FILE *out, const uint8_t *videoram, const uint8_t *attr_rom, int blink_phase) {
    fputs("\x1b[H\x1b[2J", out); // ANSI home cursor + clear screen

    for (int row = 0; row < ABC80_SCREEN_ROWS; row++) {
        int mode = 0; // K5_LINE_END resets this at the start of every row
        for (int col = 0; col < ABC80_SCREEN_COLS; col++) {
            uint16_t addr = abc80_videoram_addr((uint8_t)row, (uint8_t)col);
            uint8_t data = videoram[addr];
            uint8_t character = data & 0x7F;
            int cursor = (data & 0x80) != 0;

            uint8_t attr = abc80_attr_lookup(attr_rom, character, 1);
            int blank = (attr & ABC80_J3_BLANK) != 0;
            int j = (attr & ABC80_J3_TEXT) != 0;
            int k = (attr & ABC80_J3_GRAPHICS) != 0;
            int versal = (attr & ABC80_J3_VERSAL) != 0;

            if (!j && k) mode = 0;
            if (j && !k) mode = 1;
            if (j && k) mode = !mode;

            uint32_t cp;
            if (!blank) {
                cp = 0x20;
            } else if (mode && versal) {
                uint8_t cells = (uint8_t)(
                    ((data >> 0) & 1) |
                    (((data >> 1) & 1) << 1) |
                    (((data >> 2) & 1) << 2) |
                    (((data >> 3) & 1) << 3) |
                    (((data >> 4) & 1) << 4) |
                    (((data >> 6) & 1) << 5));
                cp = abc80_sextant_codepoint(cells);
            } else {
                cp = abc80_charset_codepoint(character);
            }

            if (cursor && blink_phase) {
                fputs("\x1b[7m", out);
                put_utf8(out, cp);
                fputs("\x1b[0m", out);
            } else {
                put_utf8(out, cp);
            }
        }
        fputc('\n', out);
    }
    fflush(out);
}
