#ifndef ABC80_DISK_H
#define ABC80_DISK_H

#include <stdbool.h>
#include <stdint.h>

#include "../../../z80core/z80.h"

// Milestone 6's floppy/DOS bypass, extracted out of main.c (Milestone 11)
// so both bin/abc80 (--interactive) and bin/abc80-gtk can share it without
// duplicating this carefully-derived logic - see
// abc80/docs/ABC80_ROADMAP.md's Floppy/DOS controller sub-step for the
// full disassembly-grounded derivation.

// Loads the real, committed ABC-DOS ROM (rom_dir/ABCDOS80.bin, unmodified)
// at its real base address 0x6000 into `ram`, and opens/creates
// `disk_path` as the host file backing the virtual disk. Returns true on
// success; on failure, prints a real error to stderr (matching this
// project's existing --disk error messages) and returns false, leaving
// abc80_disk_enabled() false.
bool abc80_disk_init(const char *rom_dir, const char *disk_path, uint8_t *ram);

// True once abc80_disk_init() has succeeded - gates the bus_read_hook's
// 0x6000-0x6FFF passthrough (main.c's abc80_bus_read_hook()) and whether
// abc80_step() dispatches into abc80_disk_trap() at all.
bool abc80_disk_enabled(void);

// Replaces a full call to the real L6068 (read)/L60A1 (write) routine -
// see abc80/docs/ABC80_ROADMAP.md's own instruction-by-instruction
// derivation of this calling convention. Reads the caller's channel/
// block-number parameters, performs the real host file I/O, writes the
// result into the same fixed per-channel buffer the real routine would
// have used, and manually pops the return address to resume execution
// exactly where the real routine's own RET would have - the same "trap
// and replace" pattern this project's own --quickload already uses at a
// different PC address, just replacing a whole subroutine's body instead
// of pre-loading data ahead of normal execution.
int abc80_disk_trap(Z80 *cpu, uint8_t *ram, bool is_read);

#endif
