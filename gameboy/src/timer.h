#ifndef _GB_TIMER_H
#define _GB_TIMER_H

#include <stdint.h>

struct GBCpu;

// DIV/TIMA/TMA/TAC (0xFF04-0xFF07). Grounded against pandocs'
// Timer_and_Divider_Registers.md and Timer_Obscure_Behaviour.md
// (fetched during this phase - see gameboy/docs/GAMEBOY_ROADMAP.md), including
// the real "system counter" model (DIV is just the visible upper byte
// of a free-running 16-bit counter) rather than a naive independent
// periodic counter - this is what makes the DIV-write/TAC-write
// "spurious tick" quirks and the 1-M-cycle-delayed overflow reload
// fall out correctly instead of needing to be special-cased.
typedef struct GBTimer {
    uint16_t sys_counter;
    uint8_t tima, tma, tac;
    int overflow_delay; // >0: T-states left until a scheduled TIMA reload+interrupt fires
} GBTimer;

void gb_timer_reset(GBTimer *timer);

// Advances the timer by `cycles` T-states - called once per
// gb_cpu_step(), same clock/call-site reasoning as gb_ppu_step().
void gb_timer_step(GBTimer *timer, struct GBCpu *cpu, int cycles);

uint8_t gb_timer_read(GBTimer *timer, uint16_t addr);
void gb_timer_write(GBTimer *timer, struct GBCpu *cpu, uint16_t addr, uint8_t val);

// Shared with cpu.c's STOP handler: pandocs' Timer_and_Divider_Registers.md
// says STOP resets the system counter exactly like a DIV write does -
// same function, not a separate code path.
void gb_timer_reset_div(GBTimer *timer, struct GBCpu *cpu);

#endif
