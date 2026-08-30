// abc806/emu/src/render.h - the text screen, for a terminal.
//
// The ABC806's picture is properly made of pixels (chargen.c, --screenshot),
// and this file does not replace that. It exists because a live session in
// a terminal needs the screen as *characters*, and because a character
// dump is what a regression suite can assert on.
//
// Unlike the ABC802's equivalent, this one carries colour. That machine
// has one phosphor; this one has eight pens and a whole attribute plane,
// and dropping them in the live view would hide the thing that makes the
// ABC806 an ABC806. The palette orders black, red, green, yellow, blue,
// magenta, cyan, white - which is exactly ANSI's own order, so a pen index
// is `30 + index` for foreground and `40 + index` for background with no
// mapping table between them. That is a genuine coincidence and is worth
// knowing rather than rediscovering.
//
// Both renderers walk the screen through abc806_decode_row(), the same
// attribute state machine the pixel renderer uses, so what the terminal
// shows and what a screenshot shows cannot drift apart. They did once,
// over double width, and finding it cost two bugs.

#ifndef ABC806_RENDER_H
#define ABC806_RENDER_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "chargen.h"
#include "text.h"

// Print the text screen as UTF-8, framed in a box. Double-width cells
// print as one character, not two: the second half of the pair is not a
// character the machine is showing, it is the space the first one occupies.
// So the ROM's sign-on reads `ABC806` here even though character RAM
// holds `AABBCC880066`.
//
// No colour and no cursor - this is the static dump, meant to be read and
// diffed. abc806_render_frame() is the one that draws.
void abc806_render_text_screen(FILE *out);

// One live frame for --interactive: the same characters, plus everything
// that only means something on a moving display - the eight colours from
// the attribute plane, underline, and the CRTC's cursor. Preceded by an
// ANSI home+clear so frames overwrite in place rather than scrolling.
//
// `flash_on` is the flash clock's current phase, supplied by the caller
// rather than computed here: it belongs to the passage of time, which the
// renderer knows nothing about.
void abc806_render_frame(FILE *out, bool flash_on);

// Everything the decode needs, gathered from the live machine. Exposed
// because --screen, --screenshot and the live frame all need the same
// snapshot, and assembling it in three places invites them to disagree.
void abc806_current_screen(Abc806Screen *out, bool flash_on);

#endif // ABC806_RENDER_H
