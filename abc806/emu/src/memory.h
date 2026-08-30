// abc806/emu/src/memory.h - ABC806 memory map, ROM loading, and the page
// map. See abc806/docs/ABC806_SCOPING.md for how this target was costed and
// abc806/docs/ABC806_REFERENCE.md for the hardware being modeled.

#ifndef ABC806_MEMORY_H
#define ABC806_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#include "../../../z80core/z80.h"

// 32K of firmware, in the same shape the ABC802 uses: six 4K BASIC PROMs
// at 0x0000-0x5FFF, one 4K DOS PROM at 0x6000, one 4K option PROM at
// 0x7000. Both DOS PROMs this repository carries are the same UFD-DOS
// v.19/v.20 pair the ABC802 has, to the day.
#define ABC806_ROM_SIZE       0x8000
#define ABC806_CHAR_RAM_SIZE  0x800   // 2K, overlaid at 0x7800-0x7FFF
#define ABC806_ATTR_RAM_SIZE  0x800   // 2K, written alongside character RAM
#define ABC806_CHAR_ROM_SIZE  0x1000  // 4K character generator

// Load the eight PROM images out of rom_dir. `dos_rom_name` selects which
// DOS PROM occupies 0x6000. Returns false, with a message on stderr, if
// any file is missing or the wrong size.
bool abc806_memory_init(Z80 *cpu, const char *rom_dir, const char *dos_rom_name);

// Install the read/write hooks. Separate from _init so a caller can load
// ROMs without committing the CPU to this banking model.
void abc806_memory_attach(Z80 *cpu);

// Called by the step loop with each instruction's own PC before it
// executes, standing in for the real M1 line - the same mechanism, and for
// the same reason, as the ABC802's. See memory.c's header comment.
void abc806_note_instruction_fetch(uint16_t pc);

// --- The control lines that steer the map ---------------------------
//
// EME ("extended memory enable") and KEYDTR both come off latches rather
// than out of the address decode, so ports.c drives them.

void abc806_set_eme(bool enabled);
bool abc806_get_eme(void);

// KEYDTR is the DART's DTR-B output. With it low and EME off, the low 32K
// reads high-resolution video RAM instead of ROM.
void abc806_set_keydtr(bool state);

// One entry of the 16-entry page map, indexed by address bits 15:12. Bit 7
// is ENL; the low bits are the physical page. Written through the MAI/MAO
// ports.
void abc806_set_map(int page, uint8_t value);
uint8_t abc806_get_map(int page);

// --- Video memory ---------------------------------------------------

// The 2K character RAM the CRTC scans, and the 2K attribute plane beside
// it. The attribute byte is not addressed directly: a write to character
// RAM also stores whatever was last written to the MAO latch, and a read
// of character RAM latches the attribute byte for reading back. ports.c
// owns that latch.
const uint8_t *abc806_char_ram(void);
const uint8_t *abc806_attr_ram(void);

void abc806_set_attr_latch(uint8_t value);
uint8_t abc806_get_attr_latch(void);

// The high-resolution video RAM, banked 16 ways by the HRS register.
#define ABC806_VIDEO_RAM_SIZE 0x20000
uint8_t abc806_videoram_read(uint32_t addr);
void abc806_videoram_write(uint32_t addr, uint8_t value);
void abc806_set_hrs(uint8_t value);
uint8_t abc806_get_hrs(void);

// The character generator, for the renderer.
const uint8_t *abc806_char_rom(void);

#endif // ABC806_MEMORY_H
