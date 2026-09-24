// debug/src/debug.h - the Z80 debugger shared by every machine target.
//
// Machine-agnostic, like asm/ and disasm/, but deliberately not part of
// z80core/: it needs disasm/src/decode.c to show instructions, and the core
// should not depend on the disassembler. Each CLI links debug.o and
// decode.o and calls two hooks around its own per-instruction step, which
// is the whole integration - the step wrappers themselves are untouched,
// so a debugger that is not enabled cannot change how a machine runs.
//
// See docs/DEBUGGER.md for the command reference.
#ifndef Z80_DEBUG_H
#define Z80_DEBUG_H

#include <stdbool.h>
#include <stdio.h>

#include "../../z80core/z80.h"

typedef struct Z80Debugger Z80Debugger;

// What the run loop should do after z80dbg_before_step().
enum { Z80DBG_RUN = 0, Z80DBG_QUIT = 1 };

// Consumes one debugger option at argv[*i] (--debug, --break ADDR,
// --debug-script FILE), creating *dbg on first use. Returns true if it was
// one, advancing *i past any value it took. Exits with a message on a bad
// value, the way each CLI already treats a malformed option.
bool z80dbg_parse_option(Z80Debugger **dbg, int argc, char **argv, int *i);

// The option lines for a CLI's own usage text.
void z80dbg_print_usage(FILE *out);

// Call once, after the machine is set up and before the first step. Opens
// the command source (the script, or /dev/tty) and installs the Ctrl-C
// handler. Returns false, with a message, if there is nowhere to read
// commands from.
bool z80dbg_start(Z80Debugger *dbg);

// Call before every instruction. Stops at the prompt when a breakpoint,
// watchpoint, finished step count or Ctrl-C says to, and returns
// Z80DBG_QUIT if the user ended the run there.
int z80dbg_before_step(Z80Debugger *dbg, Z80 *cpu);

// Call after every instruction: reports it while stepping, and checks
// watchpoints.
void z80dbg_after_step(Z80Debugger *dbg, Z80 *cpu);

#endif
