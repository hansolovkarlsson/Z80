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
