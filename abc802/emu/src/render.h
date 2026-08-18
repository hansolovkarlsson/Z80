// abc802/emu/src/render.h - text-screen rendering from character RAM.

#ifndef ABC802_RENDER_H
#define ABC802_RENDER_H

#include <stdio.h>

// Print the 80x24 text screen as UTF-8, decoding the ABC802's character
// codes to the equivalent Unicode. Used by --screen and by the tests.
void abc802_render_text_screen(FILE *out);

#endif // ABC802_RENDER_H
