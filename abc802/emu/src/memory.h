// abc802/emu/src/memory.h - ABC802 memory map, ROM loading, and the
// LRS/character-RAM banking. See abc802/docs/ABC802_REFERENCE.md for the
// hardware this models and where each fact came from.

#ifndef ABC802_MEMORY_H
#define ABC802_MEMORY_H

#include <stdbool.h>
#include <stdint.h>

#include "../../../z80core/z80.h"

#define ABC802_ROM_SIZE       0x8000  // 32K: four 8K EPROMs at 0x0000-0x7FFF
#define ABC802_CHAR_RAM_SIZE  0x800   // 2K, overlaid at 0x7800-0x7FFF
#define ABC802_CHAR_ROM_SIZE  0x1000  // 4K used by the video hardware

// Load the four BASIC/DOS EPROM images plus the character generator out of
// rom_dir. Returns false (with a message on stderr) if any is missing or
// the wrong size.
bool abc802_memory_init(Z80 *cpu, const char *rom_dir, const char *dos_rom_name);

// Install the read/write hooks on cpu. Separate from _init so a caller can
// load ROMs without committing the CPU to this banking model.
void abc802_memory_attach(Z80 *cpu);

// LRS ("Low RAM Select"), driven by the DART's DTR-B output. False selects
// ROM in the low 32K, true selects RAM there. The machine powers up with
// ROM selected, which is what makes the reset vector at 0x0000 fetch ROM.
void abc802_set_lrs(bool ram_selected);
bool abc802_get_lrs(void);

// Called by the step loop with each instruction's own PC before it
// executes. Stands in for the real M1 line: it is what lets a data read at
// 0x7800-0x7FFF tell "ROM code reading its own bytes" from "anything else
// reading character RAM". See memory.c's header comment for why this is
// done here rather than inside z80core.
void abc802_note_instruction_fetch(uint16_t pc);

// The 2K character RAM the video hardware scans. Exposed for the renderer.
const uint8_t *abc802_char_ram(void);
const uint8_t *abc802_char_rom(void);

#endif // ABC802_MEMORY_H
