// abc802/emu/src/ports.h - ABC802 I/O port decoding and the devices behind
// it. See abc802/docs/ABC802_REFERENCE.md for the port map.

#ifndef ABC802_PORTS_H
#define ABC802_PORTS_H

#include <stdbool.h>
#include <stdint.h>

#include "../../../z80core/z80.h"

// Install the I/O hooks. Call after abc802_memory_attach().
void abc802_ports_attach(Z80 *cpu);

// Advance the time-driven devices (CTC counters, CRTC vertical sync) by
// `cycles` T-states and deliver any interrupt that becomes due. Called
// once per instruction from the step loop.
void abc802_ports_tick(Z80 *cpu, int cycles);

// MC6845 register file, for the renderer.
uint8_t abc802_crtc_reg(int index);

// True once the ROM has programmed the CRTC with a plausible display
// (nonzero horizontal/vertical character counts), i.e. video is live.
bool abc802_crtc_programmed(void);

// 80/40-column mux, driven by the DART's RTS-B output.
bool abc802_80_column(void);

// Deliver one keyboard byte. The ABC802's keyboard is a *serial* device on
// DART channel B (an ABC55/ABC77 with its own microcontroller), not a
// scanned matrix like the ABC80's - so a keypress arrives as a received
// character plus a receive interrupt, which is what this raises.
void abc802_keyboard_send(uint8_t code);

// True while a previously sent byte is still waiting to be read. The DART
// holds exactly one receive byte, so a caller must not send another until
// this goes false or the earlier keystroke is simply overwritten.
bool abc802_keyboard_busy(void);

// Configuration DIP switches the ROM reads through the DART: characters
// per line (S3) and frame frequency. Defaults are 40 columns / 50 Hz,
// matching MAME's own defaults for this machine.
void abc802_set_config(bool eighty_columns, bool fifty_hz);

// True once the DART has been programmed to interrupt on received
// characters, i.e. the ROM is ready to be typed at.
bool abc802_keyboard_ready(void);

#endif // ABC802_PORTS_H
