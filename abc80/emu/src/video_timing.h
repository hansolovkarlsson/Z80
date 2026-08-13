#ifndef ABC80_VIDEO_TIMING_H
#define ABC80_VIDEO_TIMING_H

#include <stdint.h>

// The four TTL PROMs that generate ABC80 video timing/attributes, all at
// abc80/resources/rom/ - board positions and sizes grounded against MAME's
// src/mame/luxor/abc80.cpp ROM_REGION declarations (see video_timing.c's
// own top comment for full provenance/checksums).
#define ABC80_HSYNC_ROM_SIZE 256  // "K5", horizontal sync
#define ABC80_VSYNC_ROM_SIZE 512  // "K2", vertical sync
#define ABC80_ATTR_ROM_SIZE  256  // "J3", attribute
#define ABC80_LINE_ROM_SIZE  512  // "K1", chargen row address

// hsync PROM bit masks (ABC80_K5_* in MAME's abc80.h) - one byte per
// horizontal character-slot position (sx = 0..63).
#define ABC80_K5_HSYNC     0x01
#define ABC80_K5_DH        0x02 // "display horizontal" - active display area
#define ABC80_K5_LINE_END  0x04
#define ABC80_K5_ROW_START 0x08

// vsync PROM bit masks (ABC80_K2_*) - one byte per scanline (y = 0..312).
#define ABC80_K2_VSYNC       0x01
#define ABC80_K2_DV          0x02 // "display vertical" - active display area
#define ABC80_K2_FRAME_END   0x04
#define ABC80_K2_FRAME_RESET 0x08

// attr PROM bit masks (ABC80_J3_*) - see abc80_attr_lookup()'s own comment
// for the address this is looked up by.
#define ABC80_J3_BLANK    0x01
#define ABC80_J3_TEXT     0x02
#define ABC80_J3_GRAPHICS 0x04
#define ABC80_J3_VERSAL   0x08

// Generic "load an N-byte ROM image" helper shared by all four PROMs here -
// same shape of operation as abc80_chargen_load()/main.c's load_rom(), just
// parameterized on size since there are four different ones in this file
// rather than one. Returns 1 on success, 0 on any error (reported to
// stderr).
int abc80_video_timing_load(const char *path, uint8_t *rom_out, long expected_size);

// Video RAM address (0-0x3FF, i.e. offset from 0x7C00) for character row
// `row` (0-23) / column `col` (0-39). See video_timing.c for the exact
// primary-source formula this ports and how it was verified.
uint16_t abc80_videoram_addr(uint8_t row, uint8_t col);

// Attribute byte for `character` (7-bit code) at either the border
// (`active_display` = 0) or the active display area (`active_display` !=
// 0) - see video_timing.c's own comment for what "active display area"
// means here and why it matters for the ABC80_J3_BLANK bit specifically.
uint8_t abc80_attr_lookup(const uint8_t *attr_rom, uint8_t character, int active_display);

#endif
