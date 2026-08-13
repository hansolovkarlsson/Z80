// abc80/emu/src/render_demo.c - standalone verification tool for
// render.c: writes known text (including Swedish characters and a
// hand-picked block-graphics pattern) into a synthetic video RAM buffer
// via abc80_videoram_addr() - the same function the real renderer uses -
// then renders it, so the whole pipeline can be visually confirmed
// correct against known-good input before ever trusting it against real,
// CPU-generated video RAM (abc80/emu/src/main.c's job).
//
// Usage: abc80-render-demo [rom_dir]  (default: resources/rom, i.e. run
// from inside abc80/).

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "render.h"
#include "video_timing.h"

// Writes an ASCII string (already in ABC80 text-mode encoding - callers
// wanting Å/Ä/Ö etc. should poke the raw 0x40/0x5B/... codes directly)
// into `videoram` starting at character row/col, left to right.
static void poke_string(uint8_t *videoram, int row, int col, const char *s) {
    for (; *s; s++, col++) {
        uint16_t addr = abc80_videoram_addr((uint8_t)row, (uint8_t)col);
        videoram[addr] = (uint8_t)*s;
    }
}

int main(int argc, char *argv[]) {
    const char *rom_dir = (argc > 1) ? argv[1] : "resources/rom";
    char path[1024];

    static uint8_t attr[ABC80_ATTR_ROM_SIZE];
    snprintf(path, sizeof(path), "%s/attr.bin", rom_dir);
    if (!abc80_video_timing_load(path, attr, ABC80_ATTR_ROM_SIZE)) {
        return EXIT_FAILURE;
    }

    static uint8_t videoram[ABC80_VIDEORAM_SIZE];
    memset(videoram, ' ', sizeof(videoram));

    poke_string(videoram, 0, 0, "HELLO, ABC80 EMULATOR!");
    // Row 1: the nine Swedish-charset positions, raw codes 0x40/0x5B-0x5E/
    // 0x60/0x7B-0x7E - should render as E9 C4 D6 C5 DC E9 E4 F6 E5 FC (see
    // charset.c) i.e. E A O A U E A O A U with Swedish diacritics.
    poke_string(videoram, 1, 0, "\x40\x5B\x5C\x5D\x5E\x60\x7B\x7C\x7D\x7E");
    // Row 2: cursor bit set on one cell, to confirm reverse-video works.
    poke_string(videoram, 2, 0, "CURSOR->X<-");
    videoram[abc80_videoram_addr(2, 7)] |= 0x80;

    // Row 3: block-graphics mode. 0x11 is a real, ROM-relevant "mode
    // switch" control code (TEXT bit set in attr.bin's active-display
    // half - see abc80/docs/ABC80_ROADMAP.md); it's itself invisible
    // (BLANK=0) but sets the persistent row mode to graphics for every
    // character after it, until end of row. 0x35 is a visible
    // (BLANK=1/VERSAL=1) data byte whose own bits {0,2,4} (left column,
    // all three sub-rows) select LEFT HALF BLOCK (U+258C) via
    // abc80_sextant_codepoint() - five of them in a row should render as
    // a solid left-shaded bar, distinguishable from plain text.
    videoram[abc80_videoram_addr(3, 0)] = 0x11;
    for (int col = 1; col <= 5; col++) {
        videoram[abc80_videoram_addr(3, col)] = 0x35;
    }

    printf("Static (no-cursor-blink) frame:\n");
    abc80_render_frame(stdout, videoram, attr, 1);

    return EXIT_SUCCESS;
}
