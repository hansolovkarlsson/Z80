#ifndef ABC80_KEYBOARD_H
#define ABC80_KEYBOARD_H

#include <stdint.h>

// Real hardware pulses the PIO strobe for ~50ms per keystroke (MAME's own
// abc80_state::kbd_w()/m_keyboard_clear_timer). This emulator has no
// wall-clock/scanline-tick model yet, so the strobe is instead held for a
// fixed number of emulated instructions.
//
// This can't be "just longer than one poll-loop iteration" (~6
// instructions) the way it first looked - the ROM doesn't reach its
// keyboard-poll loop at all until well after reset, since RAM-size
// detection (the loop main.c's own trace shows running immediately after
// boot) runs first. Measured empirically (abc80/docs/ABC80_ROADMAP.md's
// Milestone 3 section has the full story): a hold of 200,000 instructions
// was still too short for a keystroke fed at reset to survive to the
// first poll - it isn't reached until roughly 500,000 instructions in.
// 1,000,000 gives a real, measured 2x margin over that, confirmed by
// rerunning with this exact value (not just inferred): a keystroke fed at
// reset is picked up, moving the ROM's own execution (and the rendered
// cursor position) exactly as expected. Still short enough that it
// reliably clears well before a *second*, later, unrelated poll could
// misread it as a spurious repeat.
#define ABC80_KEY_STROBE_HOLD_INSTRUCTIONS 1000000

// Registers a keypress: `ascii` becomes the next PIO Port A key-data value,
// with the strobe bit held for ABC80_KEY_STROBE_HOLD_INSTRUCTIONS calls to
// abc80_keyboard_tick().
void abc80_keyboard_press(uint8_t ascii);

// Current PIO Port A byte (abc80_state::pio_pa_r() in MAME's abc80.cpp):
// bits 0-6 = last key's ASCII code, bit 7 = strobe.
uint8_t abc80_keyboard_port_a(void);

// True once the previous keypress's strobe has fully aged out - callers
// feeding host input should wait for this before calling
// abc80_keyboard_press() again, or a second buffered byte (e.g. from
// piped stdin, where many bytes are available at once) could overwrite
// the first before the ROM's own polling loop ever sees it.
int abc80_keyboard_ready_for_next(void);

// Ages out the strobe pulse - call exactly once per emulated instruction.
void abc80_keyboard_tick(void);

#endif
