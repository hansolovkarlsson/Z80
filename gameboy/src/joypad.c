#include "joypad.h"
#include "cpu.h"

void gb_joypad_reset(GBJoypad *joypad) {
    joypad->select = 0x30; // neither group selected
    joypad->action_state = 0x0F;
    joypad->direction_state = 0x0F;
}

uint8_t gb_joypad_read(GBJoypad *joypad) {
    uint8_t nibble = 0x0F;
    if (!(joypad->select & 0x20)) nibble &= joypad->action_state;
    if (!(joypad->select & 0x10)) nibble &= joypad->direction_state;
    return (uint8_t)(0xC0 | (joypad->select & 0x30) | nibble); // bits 6-7 unused, read as 1
}

void gb_joypad_write(GBJoypad *joypad, uint8_t val) {
    joypad->select = val & 0x30;
}

static void request_joypad_interrupt(struct GBCpu *cpu) {
    gb_write_byte(cpu, 0xFF0F, (uint8_t)(gb_read_byte(cpu, 0xFF0F) | 0x10));
}

static void set_button(GBJoypad *joypad, struct GBCpu *cpu, uint8_t *state,
                        uint8_t select_bit, int button, int pressed) {
    uint8_t bit = (uint8_t)(1 << button);
    int was_visible_and_released = !(joypad->select & select_bit) && (*state & bit);

    if (pressed) *state &= (uint8_t)~bit; else *state |= bit;

    if (was_visible_and_released && pressed) {
        request_joypad_interrupt(cpu);
    }
}

void gb_joypad_set_action(GBJoypad *joypad, struct GBCpu *cpu, int button, int pressed) {
    set_button(joypad, cpu, &joypad->action_state, 0x20, button, pressed);
}

void gb_joypad_set_direction(GBJoypad *joypad, struct GBCpu *cpu, int button, int pressed) {
    set_button(joypad, cpu, &joypad->direction_state, 0x10, button, pressed);
}
