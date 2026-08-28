// abc802/emu/src/render.h - text-screen rendering from character RAM.

#ifndef ABC802_RENDER_H
#define ABC802_RENDER_H

#include <stdint.h>
#include <stdio.h>

// Print the 80x24 text screen as UTF-8, decoding the ABC802's character
// codes to the equivalent Unicode. Used by --screen and by the tests.
void abc802_render_text_screen(FILE *out);

// Draw one live frame for --interactive: the same character decode as
// abc802_render_text_screen(), but preceded by an ANSI home+clear so
// successive frames overwrite each other in place rather than scrolling,
// and with two things the static dump deliberately leaves out because
// they only mean anything on a moving display - per-character inverse
// video (bit 7 of the character code) and the CRTC's own cursor.
//
// There is no blink_phase parameter, unlike abc80_render_frame()'s. The
// ABC802 ROM blinks its cursor *itself*, in software, by toggling the
// MC6845's R10 cursor-mode bits between "non-blink" (0x09) and
// "non-display" (0x29) from its own 93.75 Hz clock interrupt - confirmed
// by tracing the real ROM's CRTC writes, not assumed. So honoring R10 is
// all that is needed, and the blink rate is whatever the real firmware
// does rather than a constant this emulator has to supply.
void abc802_render_frame(FILE *out);

// Inverse of the character decode above: given a Unicode codepoint, the
// ABC802 character code that produces it, or -1 if the machine has no
// such character. Only the ten Swedish/Finnish ISO 646 letters need this
// - plain ASCII is its own code - and it exists so --interactive's
// keyboard can type every letter the screen can show. Shares one table
// with the decode so the two directions cannot drift.
int abc802_charset_byte_for_codepoint(uint32_t codepoint);

#endif // ABC802_RENDER_H
