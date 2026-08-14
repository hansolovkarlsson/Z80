#ifndef ABC80_KEYBOARD_H
#define ABC80_KEYBOARD_H

#include <stdint.h>

#include "../../../z80core/z80.h"

// Real hardware pulses the PIO strobe for ~50ms per keystroke (MAME's own
// abc80_state::kbd_w()/m_keyboard_clear_timer) - long enough for the ROM
// to notice on its next poll, but gone well before any *later* poll
// (checking for the *next* key) could misread it as a repeat.
//
// This emulator has no wall-clock model, and a fixed-instruction-count
// hold (the first approach tried here) turned out unable to satisfy both
// halves of that at once: it must survive an arbitrarily long wait before
// the ROM's poll loop is first reached at all (~500,000 instructions after
// reset - RAM-size detection runs first), but the ROM's own multi-
// character line-read loop re-polls for the *next* key almost immediately
// (tens of instructions later) once it's already running - so a hold long
// enough for the first case caused the ROM to read the *same* still-
// asserted strobe multiple times for later characters, silently repeating
// keystrokes and overflowing its input line (`ERR 11`) - a real bug this
// project's own regression testing caught, not a hypothetical one (see
// abc80/docs/ABC80_ROADMAP.md's Milestone 3 section for the actual
// corrupted screen output that exposed it).
//
// The obvious fix - clear the strobe as soon as *any* `IN A,(n)` reads
// PIO Port A - turned out to be wrong too, for a genuinely interesting
// reason: this ROM's key-read routine is a debounce loop requiring the
// strobe to stay asserted across *several consecutive polls* (decrementing
// a counter this ROM normally refreshes via a real periodic interrupt -
// see abc80/docs/ABC80_ROADMAP.md's Milestone 3 section) before it
// actually accepts the key. Clearing on the very first poll cut that
// debounce off after a single iteration, so it never got read at all.
// Fixed by clearing only where the ROM's disassembly shows the debounce
// actually converges and the key is genuinely consumed (main.c checks
// `PC == 0x0316`, not a generic port match) - the strobe is held for
// however many instructions that debounce genuinely takes, with no
// instruction-count guess involved anywhere.

// Registers a keypress: `ascii` becomes the next PIO Port A key-data value,
// with the strobe held until abc80_keyboard_consumed() reports it read.
void abc80_keyboard_press(uint8_t ascii);

// Current PIO Port A byte (abc80_state::pio_pa_r() in MAME's abc80.cpp):
// bits 0-6 = last key's ASCII code, bit 7 = strobe.
uint8_t abc80_keyboard_port_a(void);

// True once the previous keypress's strobe has been consumed (or none is
// pending) - callers feeding host input should wait for this before
// calling abc80_keyboard_press() again, or a second buffered byte (e.g.
// from piped stdin, where many bytes are available at once) could
// overwrite the first before it's ever read.
int abc80_keyboard_ready_for_next(void);

// Call once the caller has determined the ROM's own key-read debounce has
// genuinely converged (main.c does this by PC address - see its own
// comment) - clears the strobe, edge-triggered, rather than aging it out
// on a guessed instruction count.
void abc80_keyboard_consumed(void);

// Every I/O port address that aliases PIO Port A's data register under
// ABC80's real hardware address decoding (MAME's `map.global_mask(0x17)` -
// see video_timing.c's port-map comment): only bits 0,1,2,4 of the port
// address are actually wired to anything, so any port P with
// (P & 0x17) == 0x10 reads/writes the identical register - real software
// (the BASIC ROM included, via `IN A,(38h)`) can and does address it
// through more than one of these. z80_io_in()/z80_io_out() (z80core/z80.c)
// are a plain flat 256-entry array with no device logic of their own - by
// design - so this machine layer has to keep every alias in sync itself
// rather than the CPU core knowing anything about the mask. Callers should
// call abc80_keyboard_init_port_aliases() once at startup; abc80_step()
// (step.h) calls abc80_sync_pio_port_a() itself before every instruction,
// so callers of abc80_step() don't need to call it separately.
void abc80_keyboard_init_port_aliases(void);
void abc80_sync_pio_port_a(Z80 *cpu);

#endif
