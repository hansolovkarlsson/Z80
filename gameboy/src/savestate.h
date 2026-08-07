#ifndef _GB_SAVESTATE_H
#define _GB_SAVESTATE_H

#include "cpu.h"

// Save/restore the emulator's complete run-time state (CPU registers,
// all of memory[] - VRAM/WRAM/OAM/I-O-registers/HRAM, see mmu.c's own
// routing comment for exactly what that does and doesn't include -
// PPU/timer/joypad/APU register and internal state, and the cartridge's
// banking registers, RTC, and battery RAM) to/from a single file.
// Reached entirely through `cpu` (which already carries cart/ppu/timer/
// joypad/apu pointers - see cpu.h), so no extra parameters are needed
// beyond the path, mirroring how main.c/gtk/src/main.c already wire
// those pointers together.
//
// Deliberately does NOT save/restore GBApu's sample_buffer/cap/len -
// that's a driver-owned output buffer (main.c's --wav capture, or the
// GTK front end's CoreAudio queue), not emulated hardware state; the
// caller is expected to already have a live buffer wired up via its own
// gb_apu_reset() call before gb_savestate_load() runs.
//
// Every multi-byte field is written/read explicitly little-endian byte
// by byte (see savestate.c's own w8/w16/w32/r8/r16/r32 helpers), not a
// raw struct memcpy - GBCpu/GBPpu/GBApu/GBCart all contain pointers
// (memory, cart, ppu, rom, ram, ...) that a memcpy would serialize as
// meaningless addresses, and struct padding isn't a portable on-disk
// format even for the pointer-free structs.
//
// gb_savestate_load() checks the saved ROM size and a content hash
// (see savestate.c's fnv1a()) against the currently-loaded cartridge
// before restoring anything, refusing (rather than silently loading
// mismatched banking/RTC state onto the wrong ROM) if they don't match -
// the same "never silently guessed" standard cart.c's own header
// validation already holds to.
//
// Returns 0 on success, -1 on any I/O error, bad magic/version, or ROM
// mismatch (always printed to stderr).
int gb_savestate_save(GBCpu *cpu, const char *path);
int gb_savestate_load(GBCpu *cpu, const char *path);

#endif
