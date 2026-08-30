// abc806/emu/src/memory.c - the ABC806's memory map.
//
// The ABC806 is the first machine in this repository whose memory map is
// decided by a programmable logic device rather than by address decode.
// A PAL16L8 (marked "ABC P4-1", 60 90240-01) takes the address's top bits,
// the M1 line, the EME and KEYDTR control lines and one bit out of a
// 16-entry page map, and returns which of ROM, RAM, high-resolution video
// RAM or character RAM answers the access.
//
// ## What this file implements, and what it does not - yet
//
// It implements the decode *behaviourally*, following MAME's own
// abc806_state::read_pal_p4() (src/mame/luxor/abc80x.cpp, BSD-3-Clause,
// Curt Coder), which is worth reading before changing anything here. Note
// that MAME has its actual PAL lookup commented out in favour of exactly
// this behavioural form, and carries "abc806 30K banking" as an open TODO
// immediately beside it.
//
// This repository has the real fuse map. `ABC-P4-1.bin` is a well-formed
// JEDEC dump - 64 product lines of 32 fuses, 2048 in total, exactly a
// PAL16L8 - so evaluating it directly is possible and would decide the
// cases the behavioural form approximates. That is deliberately *not* done
// here: getting the machine to execute at all comes first, and a PAL
// evaluator built before anything boots would have nothing to check itself
// against. See ABC806_SCOPING.md, milestone 1.
//
// ## Why ROM lives in the flat array
//
// The same constraint the ABC802 target documents: z80core's fetch_byte()
// indexes the flat `ram` array directly and deliberately bypasses
// bus_read_hook, so a read hook alone cannot feed the CPU its instruction
// stream. The 32K of firmware is therefore physically resident at
// 0x0000-0x7FFF, and the hooks handle everything else.
//
// That is sufficient because the machine's *reset* state is the simple
// one: ROM low, RAM high, EME off. The page map only diverts accesses once
// the ROM enables it, and the paths it diverts (video RAM, banked RAM) are
// data accesses rather than instruction fetches. If ABC806 firmware turns
// out to execute out of a mapped page, this arrangement will not survive
// and the shared core's fetch path becomes the question - which
// ABC806_SCOPING.md flags as the risk most likely to force a core change.

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "memory.h"

static uint8_t rom[ABC806_ROM_SIZE];
static uint8_t char_rom[ABC806_CHAR_ROM_SIZE];
static uint8_t rad_prom[ABC806_RAD_PROM_SIZE];
static uint8_t hru2_prom[ABC806_HRU2_PROM_SIZE];
static uint8_t char_ram[ABC806_CHAR_RAM_SIZE];
static uint8_t attr_ram[ABC806_ATTR_RAM_SIZE];
static uint8_t video_ram[ABC806_VIDEO_RAM_SIZE];

// The page map: one byte per 4K of address space. Bit 7 is ENL.
static uint8_t page_map[16];

static bool eme = false;
// KEYDTR resets high. With it low the low 32K reads video RAM, which is
// how the ROM gets at the high-resolution plane.
static bool keydtr = true;
static bool trace_writes = false;

static uint8_t attr_latch = 0;
static uint8_t hrs = 0;

// The PC of the instruction currently executing, so a data read inside the
// character-RAM window can be told from the ROM fetching its own bytes.
static uint16_t current_fetch_pc = 0;

static bool load_rom(const char *dir, const char *name, uint8_t *dest,
                     size_t size) {
    char path[1024];
    snprintf(path, sizeof path, "%s/%s", dir, name);
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Cannot open ROM image '%s'\n", path);
        return false;
    }
    size_t got = fread(dest, 1, size, f);
    long extra = 0;
    if (got == size) {
        int c = fgetc(f);
        if (c != EOF) extra = 1;
    }
    fclose(f);
    if (got != size || extra) {
        fprintf(stderr, "ROM image '%s' is not exactly %zu bytes\n", path, size);
        return false;
    }
    return true;
}

bool abc806_memory_init(Z80 *cpu, const char *rom_dir, const char *dos_rom_name) {
    // Six 4K BASIC PROMs, then DOS, then the option PROM: 32K in the same
    // arrangement the ABC802 uses, just built from eight chips instead of
    // four.
    static const char *basic[] = {
        "ABC806-basic.06-11.bin", "ABC806-basic.16-11.bin",
        "ABC806-basic.26-11.bin", "ABC806-basic.36-11.bin",
        "ABC806-basic.46-11.bin", "ABC806-basic.56-11.bin",
    };
    for (int i = 0; i < 6; i++)
        if (!load_rom(rom_dir, basic[i], rom + i * 0x1000, 0x1000)) return false;
    if (!load_rom(rom_dir, dos_rom_name, rom + 0x6000, 0x1000)) return false;
    if (!load_rom(rom_dir, "ABC806-option.76-11.6490238-02.bin",
                  rom + 0x7000, 0x1000)) return false;
    if (!load_rom(rom_dir, "ABC806-char.6490243-01.bin",
                  char_rom, ABC806_CHAR_ROM_SIZE)) return false;
    // The RAD PROM turns attribute bits into a scanline address; without it
    // underline, flash and double height have nothing to consult.
    if (!load_rom(rom_dir, "RAD.bin", rad_prom, ABC806_RAD_PROM_SIZE))
        return false;
    // HRU II shares port 0x37 with the RTC's data line: the PROM supplies
    // the low nibble, the clock bit 7.
    if (!load_rom(rom_dir, "HRU-II.bin", hru2_prom, ABC806_HRU2_PROM_SIZE))
        return false;

    memset(char_ram, 0, sizeof char_ram);
    memset(attr_ram, 0, sizeof attr_ram);
    memset(video_ram, 0, sizeof video_ram);
    memset(page_map, 0, sizeof page_map);
    eme = false;
    keydtr = true;
    trace_writes = getenv("ABC806_TRACE_WRITES") != NULL;

    // The firmware is physically resident, for the reason in the header.
    memcpy(cpu->memory, rom, ABC806_ROM_SIZE);
    return true;
}

// True when this access falls in the character-RAM window *and* is a data
// access rather than the ROM fetching its own instructions. MAME expresses
// the same thing as `vr`, cleared only when the window is addressed with
// M1 high.
static bool is_char_ram_access(uint16_t addr) {
    if ((addr & 0xF800) != 0x7800) return false;
    // An instruction being fetched from this window reads ROM. The step
    // loop tells us the PC of the instruction now executing; anything
    // reading within its own few bytes is that fetch.
    if (current_fetch_pc >= 0x7800 && current_fetch_pc <= 0x7FFF &&
        addr >= current_fetch_pc && addr < (uint16_t)(current_fetch_pc + 4))
        return false;
    return true;
}

static uint8_t bus_read(Z80 *cpu, uint16_t addr, uint8_t stored_value) {
    (void)cpu;

    // EME diverts a page to the high-resolution plane through the map.
    //
    // The map entry is stored *inverted*: MAME reads it as
    // `m_map[page] ^ 0xff` before testing ENL in bit 7, so a raw entry of
    // zero - which is what the map holds at reset - means ENL asserted and
    // no diversion. Getting this backwards diverts every access the moment
    // the ROM enables EME, and the machine dies a few thousand
    // instructions later on an illegal opcode rather than anywhere near
    // the cause.
    uint8_t entry = (uint8_t)(page_map[addr >> 12] ^ 0xFF);
    if (eme && !(entry & 0x80)) {
        uint32_t phys = ((uint32_t)(entry & 0x7F) << 12) | (addr & 0x0FFF);
        return video_ram[phys & (ABC806_VIDEO_RAM_SIZE - 1)];
    }

    // With KEYDTR low the low 32K is the video plane rather than ROM.
    // HRS carries two independent 4-bit bank numbers: bits 0-3 (VM15-VM18)
    // pick the area the CRTC displays, bits 4-7 (F15-F18) the area the CPU
    // sees here. Using the wrong nibble would work right up until the ROM
    // draws into one bank while showing another.
    if (!keydtr && addr < 0x8000) {
        uint32_t phys = ((uint32_t)((hrs >> 4) & 0x0F) << 15) | (addr & 0x7FFF);
        return video_ram[phys & (ABC806_VIDEO_RAM_SIZE - 1)];
    }

    if (is_char_ram_access(addr)) {
        // Reading character RAM latches that cell's attribute byte, which
        // is then readable through the AMI port. Not a side effect worth
        // suppressing: it is how the ROM reads attributes at all.
        attr_latch = attr_ram[addr & 0x7FF];
        return char_ram[addr & 0x7FF];
    }

    return stored_value;
}

// Returns 1 when the write was fully handled and must not also land in
// the flat array, 0 to let the normal store proceed.
static int bus_write(Z80 *cpu, uint16_t addr, uint8_t value) {
    // Every CPU write, with the three bits that decide where it lands.
    // Exists because the high-resolution investigation needed to know not
    // just that the graphics code runs but *where its writes go* - and the
    // answer (ordinary high RAM, never the plane) is what the open
    // question in ABC806_ROADMAP.md rests on.
    if (trace_writes)
        fprintf(stderr, "[w] %04X <- %02X eme=%d keydtr=%d hrs=%02X\n",
                addr, value, (int)eme, (int)keydtr, hrs);
    uint8_t entry = (uint8_t)(page_map[addr >> 12] ^ 0xFF);
    if (eme && !(entry & 0x80)) {
        uint32_t phys = ((uint32_t)(entry & 0x7F) << 12) | (addr & 0x0FFF);
        video_ram[phys & (ABC806_VIDEO_RAM_SIZE - 1)] = value;
        return 1;
    }

    if (!keydtr && addr < 0x8000) {
        uint32_t phys = ((uint32_t)((hrs >> 4) & 0x0F) << 15) | (addr & 0x7FFF);
        video_ram[phys & (ABC806_VIDEO_RAM_SIZE - 1)] = value;
        return 1;
    }

    if ((addr & 0xF800) == 0x7800) {
        // A write here always reaches character RAM - an EPROM has nothing
        // to write to, so unlike a read there is no ambiguity - and carries
        // the attribute latch into the parallel plane with it.
        char_ram[addr & 0x7FF] = value;
        attr_ram[addr & 0x7FF] = attr_latch;
        return 1;
    }

    if (addr < 0x8000) {
        // Dropped, and known to be wrong - deliberately left that way
        // rather than half-fixed. See ABC806_ROADMAP.md's milestone 5
        // section for the evidence; the short version is that the
        // high-resolution framebuffer is CPU-addressed at 0x0000-0x77FF
        // (128-byte pitch, 240 rows, ending exactly where character RAM
        // begins), and both the ROM's own clear and the line plotter
        // read *and* write there.
        //
        // Routing writes here into the plane was tried. It makes the
        // writes land, and it is almost certainly half of the answer -
        // but the clear at 0x7CB2 is `LD (HL),0` followed by `LDIR`
        // propagating that byte forward, and the plot at 0x7E31 is a
        // masked read-modify-write. Both need their *reads* to come from
        // the plane too, and making reads symmetric breaks the ROM
        // instead: its interrupt vectors and data tables live down here
        // as well. What distinguishes a ROM data read from a plane data
        // read at the same address has not been established, so no half
        // of the model is committed.
        if (trace_writes)
            fprintf(stderr, "[drop] %04X <- %02X pc=%04X\n",
                    addr, value, cpu ? cpu->pc : 0);
        return 1;
    }

    (void)cpu;
    return 0;                      // ordinary RAM: let the core store it
}

void abc806_memory_attach(Z80 *cpu) {
    cpu->bus_read_hook = bus_read;
    cpu->bus_write_hook = bus_write;
}

void abc806_note_instruction_fetch(uint16_t pc) { current_fetch_pc = pc; }

void abc806_set_eme(bool enabled) { eme = enabled; }
bool abc806_get_eme(void) { return eme; }
void abc806_set_keydtr(bool state) { keydtr = state; }

void abc806_set_map(int page, uint8_t value) { page_map[page & 0x0F] = value; }
uint8_t abc806_get_map(int page) { return page_map[page & 0x0F]; }

const uint8_t *abc806_char_ram(void) { return char_ram; }
const uint8_t *abc806_attr_ram(void) { return attr_ram; }
const uint8_t *abc806_char_rom(void) { return char_rom; }
const uint8_t *abc806_rad_prom(void) { return rad_prom; }
const uint8_t *abc806_hru2_prom(void) { return hru2_prom; }

void abc806_set_attr_latch(uint8_t value) { attr_latch = value; }
uint8_t abc806_get_attr_latch(void) { return attr_latch; }

uint8_t abc806_videoram_read(uint32_t addr) {
    return video_ram[addr & (ABC806_VIDEO_RAM_SIZE - 1)];
}
void abc806_videoram_write(uint32_t addr, uint8_t value) {
    video_ram[addr & (ABC806_VIDEO_RAM_SIZE - 1)] = value;
}
void abc806_set_hrs(uint8_t value) { hrs = value; }
uint8_t abc806_get_hrs(void) { return hrs; }
