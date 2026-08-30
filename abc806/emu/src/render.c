// abc806/emu/src/render.c - the live machine's screen.
//
// The half that reaches for the machine: it assembles an Abc806Screen from
// the CRTC, the 74ALS259 and the two RAM planes, and hands it to text.c,
// which does the actual drawing and knows nothing about any of that. The
// split is what lets bin/abc806-chargen-dump pin the drawing down with a
// fixture (see text.h).

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "chargen.h"
#include "memory.h"
#include "ports.h"
#include "render.h"
#include "text.h"

void abc806_current_screen(Abc806Screen *out, bool flash_on) {
    out->char_ram   = abc806_char_ram();
    out->attr_ram   = abc806_attr_ram();
    out->char_rom   = abc806_char_rom();
    out->rad_prom   = abc806_rad_prom();
    out->columns    = abc806_80_column() ? 80 : 40;
    out->rows       = abc806_crtc_reg(6);
    out->scanlines  = (abc806_crtc_reg(9) & 0x1F) + 1;
    out->start_addr = (uint16_t)(((abc806_crtc_reg(12) << 8) |
                                   abc806_crtc_reg(13)) & 0x7FF);
    out->cursor_addr = abc806_cursor_address();
    out->flash_on   = flash_on;
    out->forty      = !abc806_80_column();
    out->video_ram  = abc806_video_ram();
    out->hrc        = abc806_hrc();
    out->hrs        = abc806_get_hrs();
}

void abc806_render_text_screen(FILE *out) {
    if (!abc806_crtc_programmed()) {
        fprintf(out, "(CRTC not programmed - no display to render)\n");
        return;
    }
    Abc806Screen s;
    abc806_current_screen(&s, false);
    abc806_text_screen(out, &s);
}

void abc806_render_frame(FILE *out, bool flash_on) {
    fputs("\x1b[H\x1b[2J", out);   // home + clear, so frames overwrite in place
    if (!abc806_crtc_programmed()) {
        fputs("(CRTC not programmed - no display yet)\n", out);
        fflush(out);
        return;
    }
    Abc806Screen s;
    abc806_current_screen(&s, flash_on);
    abc806_ansi_frame(out, &s);
    fflush(out);
}
