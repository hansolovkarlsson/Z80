// abc806/emu/src/png.h - minimal PNG output for rendered screens.

#ifndef ABC806_PNG_H
#define ABC806_PNG_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

// Write a 1-byte-per-pixel bitmap as an RGB PNG, each byte being an index
// into `palette`. The ABC802's equivalent took a foreground and a
// background because that machine has one phosphor; this one has eight
// pens, so it takes the whole palette. Returns false on any allocation or
// I/O failure. See png.c for why this writes its own PNG rather than
// linking a library.
bool abc806_write_png(const char *path, const uint8_t *pixels, int width, int height,
                      const uint32_t *palette, int palette_size);

#endif // ABC806_PNG_H
