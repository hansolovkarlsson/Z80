#ifndef ABC80_CHARSET_H
#define ABC80_CHARSET_H

#include <stdint.h>

// Maps an ABC80 7-bit TEXT-mode character code to its Unicode codepoint.
// See charset.c's own top comment for how this was grounded.
uint32_t abc80_charset_codepoint(uint8_t character);

#endif
