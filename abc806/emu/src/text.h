// abc806/emu/src/text.h - the character set, and the screen as characters.
//
// Deliberately pure, in the same sense and for the same reason chargen.h
// is: everything arrives in an Abc806Screen, nothing here reads the live
// machine, and so bin/abc806-chargen-dump can exercise all of it against a
// synthetic screen with no CPU core involved.
//
// That is not tidiness. The ABC806's boot screen is white on black and
// uses one attribute, so a live session renders it perfectly with the
// colour path completely broken - which is
// docs/postmortems/2026-08-28-boot-screen-cannot-validate.md's exact
// finding, on this machine's predecessor, about this machine's
// predecessor's attributes. render.c is the half that asks the live
// machine; this is the half a fixture can pin down.

#ifndef ABC806_TEXT_H
#define ABC806_TEXT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "chargen.h"

// The screen as plain characters, framed in a box. Double-width cells
// print as one character, not two: the second half of the pair is not
// something the machine is showing, it is the space the first one
// occupies. So the ROM's sign-on reads `ABC806` even though character RAM
// holds `AABBCC880066`.
void abc806_text_screen(FILE *out, const Abc806Screen *s);

// The screen as an ANSI frame: the same characters plus the eight colours
// from the attribute plane, underline, flash and the cursor.
//
// The palette orders black, red, green, yellow, blue, magenta, cyan, white
// - which is exactly ANSI's own order, so a pen is `30 + index` for
// foreground and `40 + index` for background with no mapping table in
// between. A genuine coincidence, worth knowing rather than rediscovering.
void abc806_ansi_frame(FILE *out, const Abc806Screen *s);

// The machine's character code for a Unicode codepoint, or -1 if it has no
// such character. Only the ten Swedish/Finnish ISO 646 letters need it.
int abc806_charset_byte_for_codepoint(uint32_t codepoint);

// Host UTF-8 into the machine's own character bytes, at most out_size of
// them, returning how many. Newline becomes CR (0x0D); a codepoint the
// machine has no character for is dropped.
size_t abc806_utf8_to_chars(const char *utf8, uint8_t *out, size_t out_size);

#endif // ABC806_TEXT_H
