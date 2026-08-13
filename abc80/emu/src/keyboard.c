// abc80/emu/src/keyboard.c - PIO Port A keyboard state (Milestone 3, see
// abc80/docs/ABC80_ROADMAP.md). Bit layout grounded against MAME's
// abc80_state::pio_pa_r() (src/mame/luxor/abc80.cpp):
//
//   bit     description
//   0-6     keyboard data
//   7       keyboard strobe
//
// Confirmed against the real ROM's own disassembly (bin/z80dasm on the
// committed BASIC ROM images), not just MAME's comment: the steady-state
// busy-loop this emulator's Milestone 1 run reached (PC around 0x02F1-
// 0x0300) is exactly:
//
//   L02F1: BIT 7,(IX+2)
//          JR NZ,L0312
//          IN A,(38h)      ; port 0x38, masked to 0x10 by hardware's
//                          ; 0x17 global address mask (see
//                          ; video_timing.c's port-map comment) - this IS
//                          ; PIO Port A
//          ADD A,A         ; shift bit 7 (strobe) into carry
//          JR C,L0302      ; carry set (strobe=1, a key is ready) -> go
//                          ; process it
//          LD (IX+4),46h
//          JR L02F1        ; else keep polling
//
// i.e. this is real, ROM-confirmed evidence the machine is simply polling
// PIO Port A for a keystroke here, not waiting on an interrupt or
// something else - so a real Z80 PIO interrupt/mode implementation isn't
// needed to unblock it, just this port's read value.
//
// MAME itself doesn't emulate the real hardware scan-matrix PROM
// (abc80-keyboard.bin/N82S141) for this either - its abc80_common()
// machine config wires a generic host-ASCII-keyboard device straight to
// kbd_w(), which does exactly the byte-plus-strobe forwarding this file
// implements. Followed here as a well-precedented simplification (this
// is literally what the most widely used ABC80 emulation does), not an
// invented shortcut.

#include "keyboard.h"

static uint8_t key_data = 0;
static int strobe_countdown = 0; // 0 = no pending strobe

void abc80_keyboard_press(uint8_t ascii) {
    key_data = ascii & 0x7F;
    strobe_countdown = ABC80_KEY_STROBE_HOLD_INSTRUCTIONS;
}

uint8_t abc80_keyboard_port_a(void) {
    uint8_t strobe = (strobe_countdown > 0) ? 1 : 0;
    return (uint8_t)(key_data | (strobe << 7));
}

void abc80_keyboard_tick(void) {
    if (strobe_countdown > 0) strobe_countdown--;
}

int abc80_keyboard_ready_for_next(void) {
    return strobe_countdown == 0;
}
