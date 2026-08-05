#ifndef _GB_JOYPAD_H
#define _GB_JOYPAD_H

#include <stdint.h>

struct GBCpu;

// P1/JOYP (0xFF00). Grounded against pandocs' Joypad_Input.md (fetched
// during this phase): two 4-button groups (action: A/B/Select/Start,
// direction: Right/Left/Up/Down) multiplexed onto the same 4 read bits
// by the CPU-written select bits, with the Game Boy's own inverted
// "0 = pressed" polarity throughout.
typedef struct GBJoypad {
    uint8_t select;          // raw bits 4-5 from the last CPU write (0 = that group selected)
    uint8_t action_state;    // bits 0-3: A, B, Select, Start (1 = released)
    uint8_t direction_state; // bits 0-3: Right, Left, Up, Down (1 = released)
} GBJoypad;

void gb_joypad_reset(GBJoypad *joypad);
uint8_t gb_joypad_read(GBJoypad *joypad);
void gb_joypad_write(GBJoypad *joypad, uint8_t val);

// Bit positions within both action_state and direction_state - the
// same 4 bit positions, just meaning different buttons depending on
// which group they're applied to (pandocs' own P1 bit layout).
#define GB_BUTTON_A      0
#define GB_BUTTON_B      1
#define GB_BUTTON_SELECT 2
#define GB_BUTTON_START  3
#define GB_BUTTON_RIGHT  0
#define GB_BUTTON_LEFT   1
#define GB_BUTTON_UP     2
#define GB_BUTTON_DOWN   3

// No real input source exists yet (a GUI front-end is Phase 7) - these
// are the API a future front-end or test harness will call. Requests a
// joypad interrupt on a high-to-low (release-to-press) transition of a
// bit that's currently selected and therefore visible to the CPU - IF
// only reflects a change the game could actually have observed.
void gb_joypad_set_action(GBJoypad *joypad, struct GBCpu *cpu, int button, int pressed);
void gb_joypad_set_direction(GBJoypad *joypad, struct GBCpu *cpu, int button, int pressed);

#endif
