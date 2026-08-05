#ifndef _GB_MMU_H
#define _GB_MMU_H

#include "cpu.h"

#define GB_MEM_SIZE 65536

// Phase 1's temporary flat-memory harness - just enough address-space
// behavior (echo RAM, the "not usable" region, a serial-port capture
// hook) for the CPU core to run real ROMs against. No cartridge/MBC
// logic at all (0x0000-0x7FFF is one flat, writable-by-loader-only
// block) - that's Phase 2's job per docs/GAMEBOY_ROADMAP.md, and this
// file will be substantially reworked when it lands.

// Set by a test harness (see main.c) to observe Blargg-style serial
// test output: Blargg's cpu_instrs.gb and friends emit one character at
// a time by writing it to SB (0xFF01) and then writing 0x81 to SC
// (0xFF02) to start an internal-clock transfer, exactly as if a real
// link-cable peer were about to receive it. No real serial hardware is
// modeled - this hook just observes the byte before the write completes.
extern void (*gb_serial_output_hook)(uint8_t byte);

#endif
