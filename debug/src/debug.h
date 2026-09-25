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
#include <stdint.h>
#include <stdio.h>

#include "../../z80core/z80.h"

typedef struct Z80Debugger Z80Debugger;

// What the run loop should do after z80dbg_before_step(). Z80DBG_STOPPED
// comes only in async mode (below): the machine is at the prompt, and must
// not step until z80dbg_poll_input() says it has resumed.
enum { Z80DBG_RUN = 0, Z80DBG_QUIT = 1, Z80DBG_STOPPED = 2 };

// Consumes one debugger option at argv[*i] (--debug, --break ADDR,
// --debug-script FILE), creating *dbg on first use. Returns true if it was
// one, advancing *i past any value it took. Exits with a message on a bad
// value, the way each CLI already treats a malformed option.
bool z80dbg_parse_option(Z80Debugger **dbg, int argc, char **argv, int *i);

// The option lines for a CLI's own usage text.
void z80dbg_print_usage(FILE *out);

// A memory the machine keeps outside the flat 64K array, where the bus
// hooks divert accesses to it (character RAM, a video plane). peek and poke
// must reach the storage directly, with none of the side effects a CPU
// access through the hook would have. `m`, `e` and `w` then take
// NAME:OFFSET, with the offset counted from the start of this memory.
typedef struct {
    const char *name;   // what is typed before the colon, e.g. "chr"
    const char *what;   // one line for the help text
    uint32_t size;
    uint8_t (*peek)(uint32_t offset);
    void (*poke)(uint32_t offset, uint8_t value);
} Z80DbgSpace;

// Registers a space. May be called with dbg NULL (no debugger option was
// given), which does nothing, so a CLI can register unconditionally.
void z80dbg_add_space(Z80Debugger *dbg, const Z80DbgSpace *space);

// The key that stops a machine in the debugger under --interactive, where
// Ctrl-C belongs to the machine: Ctrl-]. A CLI in raw mode makes it the
// terminal's interrupt character (VINTR), so it raises the SIGINT the
// debugger already stops on, whatever the program is doing; telnet uses the
// same key to reach its own prompt.
#define Z80DBG_BREAK_CHAR 0x1D

// F12 stops too, as a second key (Fn-F12 where the top row sends media
// keys). A Swedish layout reaches Ctrl-] as Ctrl on the key right of Å,
// which a terminal sends as 0x1D; only the GTK windows needed more (see
// their is_debug_break_key()). A terminal sends F12 as the sequence
// ESC [ 2 4 ~, which cannot be an interrupt character,
// so each CLI's key decoder recognises these parameters and asks for the
// stop with z80dbg_request_stop(); a window matches GDK_KEY_F12 directly.
#define Z80DBG_BREAK_CSI_PARAMS "24"

// Wall-clock seconds spent at the prompt so far, the current stop included
// (0 with dbg NULL). A loop pacing execution against real time subtracts
// it, or the machine would run flat out on resuming to make up for time it
// spent stopped; so real time minus this never goes backwards.
double z80dbg_seconds_stopped(const Z80Debugger *dbg);

// Async mode, for a program with its own main loop (the GTK windows), which
// must not block while the machine is stopped. Call before z80dbg_start().
// A stop then prints the prompt and returns Z80DBG_STOPPED instead of
// reading commands. The program watches z80dbg_input_fd() and calls
// z80dbg_poll_input() when it is readable; that runs every complete line
// waiting and returns Z80DBG_STOPPED while the prompt is still open,
// Z80DBG_RUN once a command resumed the machine, or Z80DBG_QUIT for `q`.
// Commands are the same as the blocking prompt's, run by the same code.
void z80dbg_set_async(Z80Debugger *dbg, bool async);
int z80dbg_input_fd(const Z80Debugger *dbg);
int z80dbg_poll_input(Z80Debugger *dbg, Z80 *cpu);
bool z80dbg_is_stopped(const Z80Debugger *dbg);

// Stops before the next instruction, as Ctrl-C does: for a key in a window.
void z80dbg_request_stop(Z80Debugger *dbg);

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
