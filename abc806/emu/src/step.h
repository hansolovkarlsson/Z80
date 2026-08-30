// abc806/emu/src/step.h - one ABC806 instruction, with the per-instruction
// machine concerns that go with it.
//
// Shared between bin/abc806 and bin/abc806-gtk so neither has its own copy
// of the sequence, on exactly the terms abc80/emu/src/step.h already
// established for the other target: the front-ends differ in where input
// comes from and where pixels go, never in how the machine steps.
//
// Small, but not trivially so. The M1 notification in particular has to
// happen *before* the instruction executes, because it is that
// instruction's own data reads that consult it - get the order wrong and
// ROM code running in the character-RAM window reads character RAM
// instead of its own bytes. See memory.c's header comment for why the M1
// line is modeled this way at all.

#ifndef ABC806_STEP_H
#define ABC806_STEP_H

#include <stdint.h>

#include "../../../z80core/z80.h"

// Runs exactly one instruction. `*total_cycles` accumulates T-states, and
// is what the CTC's own timing and the caller's real-time pacing are both
// driven from. Returns the T-states the instruction took (matching
// z80_execute()), or a negative value on an unimplemented opcode - callers
// should stop stepping once they see one.
int abc806_step(Z80 *cpu, uint8_t *ram, long long *total_cycles);

#endif // ABC806_STEP_H
