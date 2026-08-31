// abc802/emu/src/chargen.h - character generator ROM decode: the ABC802's
// screen as actual pixels, rather than as which character codes are where.
//
// Deliberately pure. Everything it needs arrives in Abc802Screen, so the
// same code renders a live machine (main.c passes the running CRTC/char
// RAM state) and a synthetic test pattern (bin/abc802-chargen-dump passes
// a hand-built one), and neither path can drift from the other. The same
// split abc80/emu/src/chargen.c and its own dump tool already use.

#ifndef ABC802_CHARGEN_H
#define ABC802_CHARGEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A character cell is 6 pixels wide and, as the ROM programs R9 (= 9,
// meaning "scanlines per row minus one"), 10 scanlines tall.
#define ABC802_CHAR_WIDTH  6
#define ABC802_CHAR_HEIGHT 10

// A generous upper bound for a caller-provided pixel buffer: the CRTC
// could in principle be programmed larger than the 80x24 the ROM asks
// for, so this leaves room rather than assuming that geometry. 128
// columns by 32 rows of 6x10 cells.
#define ABC802_MAX_PIXELS (128 * ABC802_CHAR_WIDTH * 32 * ABC802_CHAR_HEIGHT)

// Everything the decode needs. Nothing here is read from a global, which
// is what keeps this testable without a running CPU.
typedef struct {
    const uint8_t *char_rom;  // 4K, as loaded by memory.c
    const uint8_t *char_ram;  // 2K
    int cols;                 // CRTC R1, horizontal displayed (80)
    int rows;                 // CRTC R6, vertical displayed (24)
    int start;                // CRTC R12/R13, display start address
    bool eighty_column;       // the 80/40 mux (DIP S3, via the DART)
    int cursor_addr;          // character-RAM address, or -1 for no cursor
    bool flash_on;            // the FLSH clock's current phase
} Abc802Screen;

// Pixel dimensions of a given screen. Width is cols * 6 in *both* column
// modes - 40-column mode does not halve the pixel count, it draws each
// character twice as wide and skips the cell between (which is why the ROM
// lays text out in the even cells).
static inline int abc802_pixel_width(const Abc802Screen *s) {
    return s->cols * ABC802_CHAR_WIDTH;
}
static inline int abc802_pixel_height(const Abc802Screen *s) {
    return s->rows * ABC802_CHAR_HEIGHT;
}

// One cell as the row's attribute walk resolved it. Attribute codes draw
// nothing and never appear here - only cells that put something on screen.
//
// This exists so the terminal renderer stops being a second, simpler
// decode that disagrees with the pixel one. It used to print one glyph per
// character code and know nothing about the row attributes at all, so a
// Row Graphic screen read correctly as a PNG and misleadingly in a
// terminal.
typedef struct {
    uint8_t code;      // the character-RAM byte, inverse bit included
    int column;        // its column in the row
    bool attribute;    // an attribute command: occupies a cell, draws nothing
    bool graphic;      // Row Graphic in force: draw from the mosaic font
    bool blanked;      // Row Flash (phase on) or Row Clear: draws nothing
} Abc802Cell;

// Resolve one row of character codes into its drawn cells. `codes` is
// `count` bytes read straight out of character RAM; `flash_on` is the
// flash clock's current phase.
//
// The walk reads scanline 0 of the character ROM to decide whether a code
// is an attribute command, where the pixel renderer re-reads whichever
// scanline it is drawing. For this font that is the same answer every
// time: over the ten scanned rows an attribute code's byte is identical,
// and on the two substituted rows (blank and cursor) only bits the decode
// ignores differ - ATE, ATD and the attribute-select bits are unchanged.
// Both facts are checked by the chargen-attribute-invariant test rather
// than assumed, because a font where they did not hold would make these
// two renderers disagree silently.
// In 40-column mode a drawn character is double width and consumes the
// cell after it, exactly as the pixel loop does - an attribute cell does
// not. So the cells returned are the visual positions in order, and a
// caller should walk them rather than indexing by column.
int abc802_decode_row(const uint8_t *char_rom, const uint8_t *codes, int count,
                      bool eighty_column, bool flash_on, Abc802Cell *cells, int max);

// Render to one byte per pixel, row-major, 0 = background and 1 = lit.
// `capacity` guards the caller's buffer; returns false if it is too small
// or the screen geometry is not programmed yet.
bool abc802_render_pixels(const Abc802Screen *screen, uint8_t *pixels, size_t capacity);

// The FLSH clock's phase at a given point in emulated time. Real hardware
// counts vertical syncs: abc802_state::vs_w() increments a counter on each
// one and toggles the clock when it reaches 0x20, i.e. every 33 fields.
// This emulator has no vsync, so the same rate is derived from T-states
// instead - see chargen.c for the arithmetic.
bool abc802_flash_phase(long long cycles);

#endif // ABC802_CHARGEN_H
