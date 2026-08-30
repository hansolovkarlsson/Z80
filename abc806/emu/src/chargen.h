// abc806/emu/src/chargen.h - turning the ABC806's character and attribute
// planes into pixels.
//
// Deliberately pure: everything the decode needs arrives in an Abc806Screen
// and pixels go out to a caller-owned buffer, with no CPU core, no ROM
// loading and no globals involved. That is what lets bin/abc806-chargen-dump
// exercise the whole attribute path against a synthetic screen - which
// matters here for exactly the reason it mattered on the ABC802, and the
// postmortem that says so is docs/postmortems/2026-08-28-boot-screen-cannot-
// validate.md: the ROM's own boot screen uses almost none of these
// attributes, so a screenshot of it would look perfect with the decode
// badly broken.

#ifndef ABC806_CHARGEN_H
#define ABC806_CHARGEN_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

// A character cell is 6 pixels wide (ABC800_CHAR_WIDTH) and up to 16
// scanlines tall; the ROM programs 10.
#define ABC806_CHAR_WIDTH   6
#define ABC806_MAX_COLUMNS  80
#define ABC806_MAX_ROWS     25
#define ABC806_MAX_SCANLINES 16
#define ABC806_MAX_PIXELS \
    (ABC806_MAX_COLUMNS * ABC806_CHAR_WIDTH * 2 * \
     ABC806_MAX_ROWS * ABC806_MAX_SCANLINES)

// Everything the decode reads. Passing it in rather than reaching for
// globals is what keeps this testable without a machine.
typedef struct {
    const uint8_t *char_ram;   // 2K, what the CRTC scans
    const uint8_t *attr_ram;   // 2K, the parallel attribute plane
    const uint8_t *char_rom;   // 4K character generator
    const uint8_t *rad_prom;   // 512-byte RAD, the character line address

    int columns;               // 40 or 80
    int rows;                  // CRTC R6
    int scanlines;             // CRTC R9 + 1
    uint16_t start_addr;       // CRTC R12/R13
    int cursor_addr;           // -1 when the cursor is off screen
    bool flash_on;             // the flash clock's current phase
    bool forty;                // the 74ALS259's 40-column line
} Abc806Screen;

// Pixel dimensions the given screen will produce.
int abc806_pixel_width(const Abc806Screen *s);
int abc806_pixel_height(const Abc806Screen *s);

// Decode into `pixels`, one byte per pixel holding a palette index 0-7.
// Returns false if the screen is not renderable (CRTC unprogrammed) or the
// buffer is too small.
bool abc806_render_pixels(const Abc806Screen *s, uint8_t *pixels, size_t size);

// The machine's eight colours, as 0xRRGGBB.
uint32_t abc806_palette(int index);

#endif // ABC806_CHARGEN_H
