// abc802/emu/src/png.h - minimal PNG output for rendered screens.

#ifndef ABC802_PNG_H
#define ABC802_PNG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Write a 1-byte-per-pixel bitmap (0 = background, nonzero = lit) as an
// RGB PNG, mapping the two states to the given colors. Returns false on
// any allocation or I/O failure. See png.c for why this writes its own
// PNG rather than linking a library.
bool abc802_write_png(const char *path, const uint8_t *pixels, int width, int height,
                      const uint8_t fg[3], const uint8_t bg[3]);

#endif // ABC802_PNG_H
