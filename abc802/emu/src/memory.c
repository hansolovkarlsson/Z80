// abc802/emu/src/memory.c - the ABC802's memory map.
//
// Unlike the ABC80 (abc80/emu/src/main.c), where ROM and RAM occupy fixed,
// disjoint address ranges, the ABC802 overlays both of its 32K halves:
//
//   0x0000-0x7FFF   ROM when LRS selects ROM, RAM when it selects RAM
//   0x7800-0x7FFF   2K character RAM, hidden *underneath* the ROM and
//                   distinguished from it only by the M1 (opcode fetch)
//                   line: an instruction fetched here reads ROM, a data
//                   read here reads character RAM
//   0x8000-0xFFFF   always RAM
//
// Both facts come from MAME's own ABC802 implementation
// (src/mame/luxor/abc80x.cpp, BSD-3-Clause, Curt Coder) - specifically its
// abc802_state::read/write and abc800_state::m1_r - and are reimplemented
// here rather than copied. The M1 trick in particular is not something
// that could be guessed from a memory map alone: without it, the ROM's own
// code at 0x7800-0x7FFF would read back as character RAM and the machine
// would never get past reset.
//
// ## Why the ROM lives in the flat array rather than behind the read hook
//
// z80core's opcode fetch (`fetch_byte()` in z80.c) indexes the flat `ram`
// array *directly*, deliberately bypassing z80_read_byte() and therefore
// bypassing bus_read_hook - only operand/data access goes through the
// hook. So a read hook alone cannot feed the CPU its instruction stream:
// whatever the banking says, opcode fetches see the flat array and nothing
// else. Both existing machine targets get away with this because their ROM
// is physically loaded into that array at a fixed address, and this target
// does the same: the currently-selected 32K is kept resident in the array,
// and abc802_set_lrs() physically swaps ROM and low RAM in and out when
// the select line changes (saving the displaced RAM in low_ram[] so its
// contents survive the round trip).
//
// The alternative - routing fetch_byte() through z80_read_byte() - was
// considered and rejected for now: it changes the instruction-fetch path
// of a shared core that two working targets already depend on, in
// particular ABC80's floating-bus hook, which forces 0x4000-0x7BFF (and,
// without --ram32k, 0x8000-0xBFFF) to read 0xFF. That would want its own
// verification pass rather than being a side effect of adding a third
// machine.
//
// The M1 distinction at 0x7800-0x7FFF is then handled without any core
// change at all: abc802_note_instruction_fetch() is called by the step
// loop with each instruction's own PC before it executes, which is exactly
// the information MAME's m1_r latches, at exactly the same granularity.

#include <stdio.h>
#include <string.h>

#include "memory.h"

static uint8_t rom[ABC802_ROM_SIZE];
static uint8_t char_rom[ABC802_CHAR_ROM_SIZE];
static uint8_t char_ram[ABC802_CHAR_RAM_SIZE];

// The contents of RAM 0x0000-0x7FFF while ROM is the half currently
// resident in the CPU's flat array. Swapped back in by abc802_set_lrs().
static uint8_t low_ram[0x8000];

static uint8_t *cpu_ram = NULL;

// False = ROM visible in the low 32K. The real machine powers up this way,
// which is exactly why the reset vector at 0x0000 fetches ROM rather than
// uninitialized RAM.
static bool lrs_ram_selected = false;

// Mirrors MAME's m_fetch_charram: whether the instruction now executing
// was fetched from 0x7800-0x7FFF. Data reads in that window consult it, so
// ROM code running there reads its own bytes while code anywhere else sees
// character RAM.
static bool fetch_charram = false;

const uint8_t *abc802_char_ram(void) { return char_ram; }
const uint8_t *abc802_char_rom(void) { return char_rom; }
bool abc802_get_lrs(void) { return lrs_ram_selected; }

void abc802_note_instruction_fetch(uint16_t pc) {
    fetch_charram = (pc >= 0x7800 && pc < 0x8000);
}

void abc802_set_lrs(bool ram_selected) {
    if (ram_selected == lrs_ram_selected || !cpu_ram) {
        lrs_ram_selected = ram_selected;
        return;
    }
    lrs_ram_selected = ram_selected;

    if (ram_selected) {
        // ROM out, RAM in: the array currently holds ROM, which is
        // pristine and needs no saving.
        memcpy(cpu_ram, low_ram, 0x8000);
    } else {
        // RAM out, ROM in: preserve whatever the program left in low RAM
        // so switching back later doesn't lose it.
        memcpy(low_ram, cpu_ram, 0x8000);
        memcpy(cpu_ram, rom, 0x8000);
    }
}

static bool load_rom_part(const char *rom_dir, const char *name,
                          uint8_t *dest, size_t expected, size_t take) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", rom_dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Failed to open ROM '%s': ", path);
        perror(NULL);
        return false;
    }
    // Read the whole chip, then keep only the part the machine wires up:
    // the character generator is an 8K device of which the video hardware
    // only ever addresses the low 4K (see ABC802_REFERENCE.md).
    uint8_t buf[0x2000];
    size_t n = fread(buf, 1, sizeof(buf), f);
    fclose(f);
    if (n != expected) {
        fprintf(stderr, "ROM '%s' is %zu bytes, expected %zu\n", path, n, expected);
        return false;
    }
    memcpy(dest, buf, take);
    return true;
}

bool abc802_memory_init(Z80 *cpu, const char *rom_dir, const char *dos_rom_name) {
    memset(rom, 0xFF, sizeof(rom));
    memset(char_rom, 0, sizeof(char_rom));
    memset(char_ram, 0, sizeof(char_ram));
    memset(low_ram, 0, sizeof(low_ram));

    // The four 8K EPROMs, in board order. The first three are BASIC II
    // (v.01); the fourth is the DOS/option ROM, which is the one that
    // actually varies between machines - hence the caller-selectable name.
    if (!load_rom_part(rom_dir, "ABC802-basic.02-11.bin", rom + 0x0000, 0x2000, 0x2000)) return false;
    if (!load_rom_part(rom_dir, "ABC802-basic.12-11.bin", rom + 0x2000, 0x2000, 0x2000)) return false;
    if (!load_rom_part(rom_dir, "ABC802-basic.22-11.bin", rom + 0x4000, 0x2000, 0x2000)) return false;
    if (!load_rom_part(rom_dir, dos_rom_name,             rom + 0x6000, 0x2000, 0x2000)) return false;
    if (!load_rom_part(rom_dir, "ABC802-char.6490191-01.bin", char_rom, 0x2000, 0x1000)) return false;

    cpu_ram = cpu->memory;
    lrs_ram_selected = false;
    fetch_charram = false;

    // Make the power-on selection (ROM) physically resident, so the reset
    // vector at 0x0000 fetches ROM.
    memcpy(cpu_ram, rom, 0x8000);
    return true;
}

// The read side. stored_value is whatever the flat array already held, so
// every branch that wants the currently-resident half just returns it.
static uint8_t abc802_read_hook(Z80 *cpu, uint16_t address, uint8_t stored_value) {
    (void)cpu;
    if (address < 0x7800 || address >= 0x8000) return stored_value;
    if (lrs_ram_selected) return stored_value;   // low 32K is RAM: no overlay

    // In the overlay window with ROM selected: ROM for code reading its
    // own bytes, character RAM for everyone else.
    return fetch_charram ? rom[address] : char_ram[address & (ABC802_CHAR_RAM_SIZE - 1)];
}

static int abc802_write_hook(Z80 *cpu, uint16_t address, uint8_t value) {
    (void)cpu;
    if (address >= 0x8000) return 0;             // always RAM: let it store
    if (lrs_ram_selected) return 0;              // low 32K is RAM: let it store

    if (address >= 0x7800) {
        // Writes into the ROM window always land in character RAM,
        // regardless of M1 - an EPROM has nothing to write to, so there is
        // no ambiguity to resolve here.
        char_ram[address & (ABC802_CHAR_RAM_SIZE - 1)] = value;
        return 1;
    }
    // A write into ROM. Discarded, as on real hardware - and this hook is
    // exactly what keeps it from corrupting the resident ROM image.
    return 1;
}

void abc802_memory_attach(Z80 *cpu) {
    cpu->bus_read_hook = abc802_read_hook;
    cpu->bus_write_hook = abc802_write_hook;
}
