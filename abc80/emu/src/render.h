#ifndef ABC80_RENDER_H
#define ABC80_RENDER_H

#include <stdint.h>
#include <stdio.h>

#define ABC80_SCREEN_ROWS 24
#define ABC80_SCREEN_COLS 40
// Real video RAM size (0x7C00-0x7FFF) - see abc80/docs/ABC80_ROADMAP.md's
// memory map.
#define ABC80_VIDEORAM_SIZE 1024

// Renders the current video RAM (ABC80_VIDEORAM_SIZE bytes, addressed via
// abc80_videoram_addr()) to `out` as a full-screen redraw: ANSI home+clear
// followed by ABC80_SCREEN_ROWS lines of UTF-8 text. See render.c's own
// top comment for the mode/attribute logic this ports and the text/
// graphics character mapping. `blink_phase` selects whether cursor cells
// (bit 7 of the videoram byte) currently show reversed - real hardware
// blinks the cursor, so callers wanting that effect should alternate this
// across successive calls.
void abc80_render_frame(FILE *out, const uint8_t *videoram, const uint8_t *attr_rom, int blink_phase);

#endif
