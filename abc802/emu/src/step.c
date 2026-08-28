// abc802/emu/src/step.c - see step.h for why this is shared rather than
// duplicated in each front-end.

#include "step.h"
#include "memory.h"
#include "ports.h"

int abc802_step(Z80 *cpu, uint8_t *ram, long long *total_cycles) {
    // Stands in for the real M1 line. Must happen before the instruction
    // runs, since it is that instruction's own data reads that consult it.
    abc802_note_instruction_fetch(cpu->pc);

    int taken = z80_execute(cpu, ram);
    if (taken < 0) return taken;

    *total_cycles += taken;

    // The time-driven devices (CTC counters, and the interrupt delivery
    // that hangs off them) advance by the instruction's own cycle count,
    // not by wall-clock time - so a paced front-end and a free-running one
    // see identical machine behavior.
    abc802_ports_tick(cpu, taken);
    return taken;
}
