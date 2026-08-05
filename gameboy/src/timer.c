#include "timer.h"
#include "cpu.h"

// Bit of the 16-bit system counter whose *falling edge* increments
// TIMA, indexed by TAC's 2-bit clock-select field. Derived from
// pandocs' documented Hz rates (Timer_and_Divider_Registers.md): rate R
// corresponds to a period of 4194304/R T-states between edges, and a
// bit at position N completes one full up/down cycle (one falling
// edge) every 2^(N+1) T-states, so N = log2(period) - 1. These
// particular values (9, 3, 5, 7) are also the same ones used by every
// independent GB emulator implementation - a useful cross-check that
// this derivation is right, not just internally consistent.
static const int tac_bit[4] = {9, 3, 5, 7};

static void request_timer_interrupt(struct GBCpu *cpu) {
    gb_write_byte(cpu, 0xFF0F, (uint8_t)(gb_read_byte(cpu, 0xFF0F) | 0x04));
}

static int effective_signal(const GBTimer *timer) {
    if (!(timer->tac & 0x04)) return 0; // timer disabled: signal is always low
    return (timer->sys_counter >> tac_bit[timer->tac & 0x03]) & 1;
}

// Shared by the per-T-state stepping loop and the DIV/TAC-write
// handlers below, since a falling edge increments TIMA identically no
// matter what caused it (normal counting, or the system counter/TAC
// changing abruptly) - see pandocs' Timer_Obscure_Behaviour.md for why
// both DIV writes and TAC writes can cause a "spurious tick" this way.
static void tick_tima_on_falling_edge(GBTimer *timer, int old_signal, int new_signal) {
    if (old_signal && !new_signal) {
        timer->tima++;
        if (timer->tima == 0) {
            // The TMA reload and interrupt request happen one M-cycle
            // (4 T-states) later, not immediately - TIMA genuinely
            // reads $00 during that window (pandocs' "Timer overflow
            // behavior" section, cited precisely since it's a common
            // point real emulators get subtly wrong).
            timer->overflow_delay = 4;
        }
    }
}

void gb_timer_reset(GBTimer *timer) {
    timer->sys_counter = 0;
    timer->tima = 0;
    timer->tma = 0;
    timer->tac = 0;
    timer->overflow_delay = 0;
}

void gb_timer_step(GBTimer *timer, struct GBCpu *cpu, int cycles) {
    for (int i = 0; i < cycles; i++) {
        if (timer->overflow_delay > 0) {
            timer->overflow_delay--;
            if (timer->overflow_delay == 0) {
                timer->tima = timer->tma;
                request_timer_interrupt(cpu);
            }
        }

        int old_signal = effective_signal(timer);
        timer->sys_counter++;
        int new_signal = effective_signal(timer);
        tick_tima_on_falling_edge(timer, old_signal, new_signal);
    }
}

uint8_t gb_timer_read(GBTimer *timer, uint16_t addr) {
    switch (addr) {
        case 0xFF04: return (uint8_t)(timer->sys_counter >> 8);
        case 0xFF05: return timer->tima;
        case 0xFF06: return timer->tma;
        case 0xFF07: return (uint8_t)(0xF8 | timer->tac); // bits 3-7 unused, read as 1
        default: return 0xFF;
    }
}

void gb_timer_reset_div(GBTimer *timer, struct GBCpu *cpu) {
    int old_signal = effective_signal(timer);
    timer->sys_counter = 0;
    int new_signal = effective_signal(timer);
    tick_tima_on_falling_edge(timer, old_signal, new_signal);
    (void)cpu; // any resulting interrupt is requested later, from gb_timer_step's own overflow_delay countdown - kept for signature symmetry with gb_timer_write()
}

void gb_timer_write(GBTimer *timer, struct GBCpu *cpu, uint16_t addr, uint8_t val) {
    switch (addr) {
        case 0xFF04:
            gb_timer_reset_div(timer, cpu);
            break;
        case 0xFF05:
            timer->tima = val;
            break;
        case 0xFF06:
            timer->tma = val;
            break;
        case 0xFF07: {
            int old_signal = effective_signal(timer);
            timer->tac = val & 0x07;
            int new_signal = effective_signal(timer);
            tick_tima_on_falling_edge(timer, old_signal, new_signal);
            break;
        }
        default: break;
    }
}
