#ifndef ABC80_ABCBUS_H
#define ABC80_ABCBUS_H

#include <stdbool.h>
#include <stdint.h>

#include "../../../z80core/z80.h"

// abc80/emu/src/abcbus.h - this machine's side of the ABC bus.
//
// Milestone 12 retired what this file used to hold: a PC-address trap on
// the two block-I/O entry points inside the real ABC-DOS ROM
// (0x6068/0x60A1), which serviced whole sector transfers in C and forged
// the routine's own RET. The card itself now lives in abcbus/disk.c and
// is shared with the ABC802 - see that file's own comment for why one
// synthetic controller serves both machines.
//
// What stays here is only what is genuinely ABC80-specific: loading the
// DOS ROM into this machine's expansion window, and decoding this
// machine's own I/O port map onto the card's bus registers.

// The real, committed DOS ROM this machine's disk expansion card carries.
// Both images in abc80/resources/rom/ are real and unmodified; ABC-DOS is
// the default because it is the one this project has ground truth for.
#define ABC80_DEFAULT_DOS_ROM "ABCDOS80.bin"

// Loads `dos_rom` (a filename inside rom_dir, or NULL for the default
// above) at its real base address 0x6000 into `ram`, and attaches
// `disk_path` as the floppy image the ABC-bus card serves. Returns true
// on success; on failure prints a real error to stderr and returns false,
// leaving abc80_abcbus_rom_loaded() false.
//
// The ROM being swappable is not a convenience feature - it is the
// evidence that retiring the PC trap was worth doing. UFD80V20.bin is a
// different DOS, with a different bus driver and a different device-select
// scheme, and nothing in this emulator was ever written for it; it drives
// the same synthetic card correctly because the card implements the bus
// rather than one ROM's routines.
bool abc80_abcbus_init(const char *rom_dir, const char *dos_rom, const char *disk_path,
                       uint8_t *ram);

// True once abc80_abcbus_init() has succeeded - gates the bus_read_hook's
// 0x6000-0x6FFF passthrough (main.c's abc80_bus_read_hook()), so that
// window keeps floating like the rest of the expansion range when no DOS
// ROM was loaded into it.
bool abc80_abcbus_rom_loaded(void);

// The Z80 I/O hooks (z80core/z80.h), routing this machine's ABC-bus port
// numbers to the shared card and leaving every other port to the CPU
// core's own flat io_ports[] array.
uint8_t abc80_abcbus_io_in(Z80 *cpu, uint8_t port, uint8_t stored_value);
int abc80_abcbus_io_out(Z80 *cpu, uint8_t port, uint8_t value);

#endif
