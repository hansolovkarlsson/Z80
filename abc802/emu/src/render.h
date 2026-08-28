// abc802/emu/src/render.h - text-screen rendering from character RAM.

#ifndef ABC802_RENDER_H
#define ABC802_RENDER_H

#include <stddef.h>
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

// Convert a host UTF-8 string into the machine's own character bytes,
// writing at most out_size of them and returning how many. Newline becomes
// CR (0x0D), the byte a real Return key produces; a codepoint the machine
// has no character for is dropped.
//
// This exists because --type used to feed its argument to the keyboard as
// raw bytes: a shell argument containing Å reached BASIC as the two bytes
// of its UTF-8 encoding, which the ROM answered with a syntax error. The
// interactive keyboard paths always decoded properly, so the two disagreed
// about what typing the same text meant.
size_t abc802_utf8_to_chars(const char *utf8, uint8_t *out, size_t out_size);

// The character-RAM address the CRTC's cursor is currently on, or -1 when
// R10's cursor-mode bits say it is not displayed - which on this machine
// is also how the ROM blinks it (see abc802_render_frame above).
int abc802_cursor_address(void);

#endif // ABC802_RENDER_H
