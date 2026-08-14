#ifndef ABC80_STEP_H
#define ABC80_STEP_H

#include <stdint.h>

#include "../../../z80core/z80.h"
#include "sound.h"

// Real ABC80 hardware ties the Z80 PIO's Port A ASTB (strobe) pin to the
// video scanline clock (MAME's abc80_state::scanline_tick(), toggled once
// per scanline) rather than to a real keyboard handshake signal - a
// hardware trick that turns Port A's normal "input-mode strobe rising edge
// -> interrupt" behavior into a genuine periodic timer interrupt, entirely
// independent of actual keystrokes. Derived, not guessed: real screen
// timing (MAME's abc80.h: ABC80_HTOTAL=384 pixels, pixel clock
// XTAL(11'980'800)/2 = 5,990,400Hz) gives a 15,600Hz line rate
// (5,990,400/384); at the real 2,995,200Hz CPU clock (ABC80_CLOCK_HZ in
// main.c), that's exactly 192 T-states per scanline. Since scanline_tick()
// *toggles* the strobe on every call rather than pulsing it once per line,
// only every *other* call is a rising edge - the actual interrupt-
// triggering event (MAME's z80pio_device::pio_port::strobe(), MODE_INPUT
// case) - so real interrupts arrive every 384 T-states (2 scanlines), a
// 7800Hz rate.
//
// Confirmed against this ROM's own real disassembly (bin/z80dasm), not
// just MAME's driver comment: boot init (0x0068-0x00C5) sets IM 2, I=0
// (`LD I,A` with A=0 at 0x008C), and writes exactly one Z80 PIO Port A
// control sequence via `OUT (39h),A` three times (0x39 & 0x17 == 0x11,
// Port A control under the 0x17 hardware address mask - see
// video_timing.c's port-map comment): 0x34 (interrupt vector - MAME's
// z80pio.cpp control_write() treats any control-port byte with bit0=0 as
// a vector load, regardless of mode), 0xB7 (interrupt control word: D7
// enable=1, D4 mask-follows=1), 0x7F (the mask byte the mask-follows bit
// above requires next). With I=0 and vector=0x34, the real IM2 vector-
// table entry is at 0x0034; this ROM's own bytes there (0x1E 0x03,
// little-endian) point to 0x031E - a real interrupt handler, confirmed by
// its own RETI at 0x0336. It reads Port A directly, checks for a Ctrl-C-
// style break combo (0x83), and - regardless of that check -
// unconditionally reloads a fixed value (0x46) into 0xFDF7 (IX+4 in the
// keyboard poll loop's own IX=0xFDF3 base, confirmed at 0x02A5) before
// EI/RETI: exactly the debounce-counter refresh Milestone 3's own
// keyboard.h comment already identified from indirect evidence (a
// periodic refresh this emulator couldn't yet supply), now grounded
// directly by finding and reading the real handler. Milestone 3's own
// PC==0x0316 strobe-consumption hook (abc80_step()'s own `about_to_
// consume_key`) is unaffected by this and still needed regardless: it
// tracks *this emulator's own* host-keystroke queue, not the ROM's
// internal debounce state, and 0x0316 remains the single real address
// where the ROM's poll loop - via either its interrupt-driven fast path
// or its direct-polling decrement fallback - genuinely finishes consuming
// a key, confirmed by both paths in the disassembly funneling through it.
#define ABC80_PIO_INTERRUPT_PERIOD_TSTATES 384
#define ABC80_PIO_INTERRUPT_VECTOR 0x34

// Runs exactly one Z80 instruction (or the floppy/DOS bypass trap in its
// place - see disk.h), handling every ABC80-specific per-instruction
// concern this project's own regression testing has needed real,
// carefully-derived fixes for: keyboard strobe consumption (Milestone 3),
// the interrupt-interception hazard (Milestone 7), sound-register write
// logging (Milestone 5), the floppy/DOS bypass (Milestone 6), and periodic
// PIO interrupt scheduling (Milestone 7). Shared between bin/abc80
// (--interactive) and bin/abc80-gtk so this logic isn't duplicated - see
// abc80/docs/ABC80_ROADMAP.md's Milestone 11 section.
//
// Callers are responsible for calling abc80_keyboard_press() themselves
// *before* this, from whatever real input source they have (a terminal
// byte, a GDK key event) - this function only decides when a *pending*
// keypress's strobe should be released (by PC address, not by who called
// it), not where keys come from.
//
// `*total_cycles` accumulates T-states; `*next_pio_interrupt_at` is the
// caller-owned schedule for the next periodic PIO interrupt (the caller
// should initialize it to ABC80_PIO_INTERRUPT_PERIOD_TSTATES before the
// first call - this function advances it by the same period each time a
// request fires). Returns the real cycle count for the step executed
// (matching z80_execute()'s own return value), or a negative value on an
// unimplemented opcode (halt) - callers should stop calling this once
// they see a negative return.
int abc80_step(Z80 *cpu, uint8_t *ram, Abc80SoundLog *sound_log,
               uint64_t *total_cycles, uint64_t *next_pio_interrupt_at);

#endif
