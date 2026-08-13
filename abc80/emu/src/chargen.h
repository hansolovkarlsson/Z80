#ifndef ABC80_CHARGEN_H
#define ABC80_CHARGEN_H

#include <stdint.h>
#include <stddef.h>

// Real ABC80 character-generator ROM size (SN74S263N, board position "H2") -
// see chargen.c's own top comment for the address formula this backs.
#define ABC80_CHARGEN_ROM_SIZE 2560
#define ABC80_CHARGEN_CHAR_WIDTH 6
// Rows 0-8 hold real glyph data; row 9 is always blank (inter-row spacing) -
// see abc80_chargen_row()'s own comment.
#define ABC80_CHARGEN_CHAR_HEIGHT 10

// Loads the raw 2560-byte ROM image from `path` into `rom_out` (caller-
// owned, ABC80_CHARGEN_ROM_SIZE bytes). Returns 1 on success, 0 on any
// error (wrong size, unreadable file) - errors are reported to stderr.
int abc80_chargen_load(const char *path, uint8_t *rom_out);

// Returns the 8-bit row pattern for `character`'s scanline `row`, exactly
// matching MAME's sn74s262_device::read() (src/devices/video/sn74s262.cpp) -
// see chargen.c for the grounding and the bit-layout this implies.
uint8_t abc80_chargen_row(const uint8_t *rom, uint8_t character, uint8_t row);

#endif
