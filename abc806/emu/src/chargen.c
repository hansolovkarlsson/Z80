// abc806/emu/src/chargen.c - the ABC806 text decode.
//
// This is where the ABC806 stops resembling the ABC802. That machine
// carries its attributes *inside the character generator's output byte*,
// so the font decides which codes are attribute commands. The ABC806 has a
// parallel 2K attribute plane instead, one byte per cell, and a bipolar
// PROM ("RAD", 60 90241-01) that turns the attribute bits into a scanline
// address.
//
// Reimplemented from MAME's abc806_state::abc806_update_row()
// (src/mame/luxor/abc80x_v.cpp, BSD-3-Clause, Curt Coder). Four things
// about it are worth knowing before changing anything here, because none
// is guessable from a memory map:
//
// 1. **An attribute byte whose foreground and background match is not a
//    colour at all - it is a command.** `(attr & 7) == ((attr >> 3) & 7)`
//    selects a command in bits 7:6: keep the previous attributes, reserved,
//    blank, or double width. Only when they differ is the byte read as
//    colours. So black-on-black is unreachable as an ordinary attribute,
//    which is what makes the encoding work.
//
// 2. **Underline, flash and double height are not drawn.** They are fed to
//    the RAD PROM, which answers with a *scanline address*, and the
//    character generator is then addressed with that instead of the real
//    row. Same idea as the ABC802's attribute mechanism, different
//    mechanism - substitute the row, do not post-process pixels. The
//    cursor is the same trick: scanline 0x0F, which the font holds as a
//    solid bar.
//
// 3. **Double width is described by the cell before it.** A command-3
//    attribute takes its own e5/e6 from bits 1:0 and then reads the *next*
//    cell's attribute byte for the actual colours, and the renderer skips
//    a column afterwards.
//
// 4. **The glyph is six bits, taken from the top of the ROM byte after a
//    two-place left shift.** The low two bits of each font byte are not
//    pixels.

#include <string.h>

#include "chargen.h"

// The eight pens, from MAME's abc806_palette(). Plain saturated RGB - this
// machine drives a colour monitor rather than a single-phosphor tube, so
// unlike the ABC802's amber there is nothing to sample.
static const uint32_t palette[8] = {
    0x000000,  // black
    0xFF0000,  // red
    0x00FF00,  // green
    0xFFFF00,  // yellow
    0x0000FF,  // blue
    0xFF00FF,  // magenta
    0x00FFFF,  // cyan
    0xFFFFFF,  // white
};

uint32_t abc806_palette(int index) { return palette[index & 7]; }

int abc806_pixel_width(const Abc806Screen *s) {
    return s->columns * ABC806_CHAR_WIDTH * (s->forty ? 2 : 1);
}

int abc806_pixel_height(const Abc806Screen *s) {
    return s->rows * s->scanlines;
}

int abc806_decode_row(const Abc806Screen *s, int row, Abc806Cell *cells, int max) {
    if (!s || !cells || row < 0 || row >= s->rows) return 0;

    // Attributes persist across a row until something changes them:
    // command 0 means "use previously selected". They reset per row, which
    // is why this function's unit is a row rather than a screen.
    int fg = 7, bg = 0, underline = 0, flash = 0;
    int e5 = s->forty, e6 = s->forty;
    int n = 0;

    for (int column = 0; column < s->columns && n < max; column++) {
        uint16_t ma = (uint16_t)(s->start_addr + row * s->columns + column);
        uint8_t data = s->char_ram[ma & 0x7FF];
        uint8_t attr = s->attr_ram[ma & 0x7FF];

        if ((attr & 0x07) == ((attr >> 3) & 0x07)) {
            switch (attr >> 6) {
                case 0:
                    break;                       // keep what is current
                case 1:
                    break;                       // reserved
                case 2:
                    fg = bg = 0; underline = 0; flash = 0;
                    break;                       // blank
                case 3: {                        // double width
                    e5 = attr & 0x01;
                    e6 = (attr >> 1) & 0x01;
                    uint16_t next = (uint16_t)((ma + 1) & 0x7FF);
                    uint8_t a2 = s->attr_ram[next];
                    if (a2 != 0x00) {
                        fg = a2 & 0x07;
                        bg = (a2 >> 3) & 0x07;
                        underline = (a2 >> 6) & 1;
                        flash = (a2 >> 7) & 1;
                    }
                    break;
                }
            }
        } else {
            fg = attr & 0x07;
            bg = (attr >> 3) & 0x07;
            underline = (attr >> 6) & 1;
            flash = (attr >> 7) & 1;
            e5 = s->forty;
            e6 = s->forty;
        }

        cells[n].code      = data;
        cells[n].ma        = (uint16_t)(ma & 0x7FF);
        cells[n].fg        = fg;
        cells[n].bg        = bg;
        cells[n].underline = underline;
        cells[n].flash     = flash;
        cells[n].e5        = e5;
        cells[n].e6        = e6;
        cells[n].cursor    = s->cursor_addr >= 0 &&
                             (int)(ma & 0x7FF) == s->cursor_addr;
        n++;

        // A double-width cell consumed the next column's attribute byte
        // and occupies its space too.
        if (e5 || e6) {
            if (!s->forty) column++;
        }
    }
    return n;
}


// The high-resolution plane, composited over the text layer.
//
// Reimplemented from MAME's abc806_state::hr_update(). The shape is not
// guessable and is worth stating outright:
//
// 1. **The displayed bank is HRS's *low* nibble** (VM15-VM18), while the
//    bank the CPU writes through is the *high* nibble (F15-F18). They are
//    independent on purpose - the machine can draw into one area while
//    showing another - so using the wrong nibble works perfectly until
//    something double-buffers.
//
// 2. **One byte becomes four pixels, via two lookups.** Each nibble
//    indexes hrc[], and each hrc entry is *itself* two pixels: four bits
//    each, of which bit 3 is "opaque" and bits 2:0 are the pen. So the
//    palette carries the horizontal resolution - program both halves of an
//    entry alike and you get 240 wide, differently and you get 480.
//
// 3. **The layer is not simply on top.** A high-resolution pixel is drawn
//    where its opaque bit is set, *or* where the text layer left black.
//    Text therefore punches through its own foreground, which is how the
//    two planes coexist without either needing a mask.
//
// With hrc all zeros - its state after the ROM clears it at boot - every
// dot is pen 0 with opaque clear, so nothing is drawn anywhere text is not
// already black. The layer disables itself, which is why this needs no
// enable flag.
static void render_hr(const Abc806Screen *s, uint8_t *pixels, int w, int h) {
    if (!s->video_ram || !s->hrc) return;

    uint32_t addr = (uint32_t)(s->hrs & 0x0F) << 15;

    for (int y = 0; y < ABC806_HR_ROWS && y < h; y++) {
        for (int sx = 0; sx < ABC806_HR_BYTES_PER_ROW; sx++) {
            uint8_t data = s->video_ram[addr++ & (ABC806_VIDEO_RAM_SIZE - 1)];
            uint16_t dot = (uint16_t)((s->hrc[data >> 4] << 8) |
                                       s->hrc[data & 0x0F]);

            for (int pixel = 0; pixel < ABC806_HR_PIXELS_PER_BYTE; pixel++) {
                int x = ABC806_HR_X_OFFSET +
                        sx * ABC806_HR_PIXELS_PER_BYTE + pixel;
                if (x >= 0 && x < w) {
                    bool opaque = (dot & 0x8000) != 0;
                    if (opaque || pixels[y * w + x] == 0)
                        pixels[y * w + x] = (uint8_t)((dot >> 12) & 0x07);
                }
                dot = (uint16_t)(dot << 4);
            }
        }
    }
}

bool abc806_render_pixels(const Abc806Screen *s, uint8_t *pixels, size_t size) {
    if (!s || s->columns <= 0 || s->rows <= 0 || s->scanlines <= 0) return false;

    int w = abc806_pixel_width(s);
    int h = abc806_pixel_height(s);
    if (w <= 0 || h <= 0 || (size_t)(w * h) > size) return false;
    memset(pixels, 0, (size_t)(w * h));

    for (int row = 0; row < s->rows; row++) {
        Abc806Cell cells[ABC806_MAX_COLUMNS];
        int count = abc806_decode_row(s, row, cells, ABC806_MAX_COLUMNS);

        // The pen advances by what was actually drawn, which is not
        // `column * width`: a double-width cell occupies two cells' worth
        // of pixels and then the paired cell is skipped, so deriving x
        // from the column index leaves a gap exactly the size of the cell
        // that was doubled.
        int pen_x = 0;

        for (int i = 0; i < count; i++) {
            const Abc806Cell *c = &cells[i];
            int th = 0;   // double height is not driven yet

            // Constant for the whole cell, so computed once rather than
            // per scanline. Pixel doubling follows e5/e6, *not* the
            // screen's own 40-column flag: the flag seeds e5/e6 at the
            // start of a row, but a double-width attribute sets them
            // mid-row on an otherwise 80-column screen.
            int rep_count = (c->e5 || c->e6) ? 2 : 1;

            for (int ra = 0; ra < s->scanlines; ra++) {
                int rad;
                if (c->cursor) {
                    // The cursor replaces the glyph rather than inverting
                    // it: scanline 0x0F is a solid bar in this font.
                    rad = 0x0F;
                } else {
                    uint16_t rad_addr = (uint16_t)((c->e6 << 8) | (c->e5 << 7) |
                                                   (c->flash << 6) |
                                                   ((s->flash_on ? 1 : 0) << 5) |
                                                   (c->underline << 4) |
                                                   (ra & 0x0F));
                    rad = s->rad_prom[rad_addr & 0x1FF] & 0x0F;
                }

                uint16_t caddr = (uint16_t)((th << 12) | (c->code << 4) | rad);
                // The glyph is the top six bits after shifting left two -
                // the font byte's low two bits are not pixels.
                uint8_t bits = (uint8_t)(s->char_rom[caddr & 0xFFF] << 2);

                int px = pen_x;
                int py = row * s->scanlines + ra;
                for (int bit = 0; bit < ABC806_CHAR_WIDTH; bit++) {
                    int colour = (bits & 0x80) ? c->fg : c->bg;
                    for (int rep = 0; rep < rep_count; rep++) {
                        if (px < w) pixels[py * w + px] = (uint8_t)colour;
                        px++;
                    }
                    bits = (uint8_t)(bits << 1);
                }
            }

            pen_x += ABC806_CHAR_WIDTH * rep_count;
        }
    }

    render_hr(s, pixels, w, h);
    return true;
}
