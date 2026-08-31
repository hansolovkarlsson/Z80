// abc80/emu/src/abcbus.c - this machine's side of the ABC bus.
//
// This file used to be abc80/emu/src/disk.c, a PC-address trap: it
// intercepted 0x6068 and 0x60A1 inside the real ABC-DOS ROM, did the
// sector's host file I/O in C, wrote the bytes straight into the ROM's
// own RAM buffer, and forged the trapped routine's RET. That worked, and
// it was carefully derived - but it only ever worked for the two entry
// points it knew, in the one ROM it was derived against. Milestone 12
// retired it in favour of the real ABC-bus protocol, which the ABC802
// target had to implement anyway because its own DOS ROM offers nothing
// to trap.
//
// The card is abcbus/disk.c, shared. What is ABC80-specific, and so
// stays here, is two things:
//
//   1. The DOS ROM. Unlike the ABC802, whose DOS ROM is part of the 32K
//      onboard ROM, this machine's DOS lives on the expansion card
//      itself, so --disk has to load ABCDOS80.bin into the bus window at
//      0x6000 before anything can talk to a disk at all.
//   2. The port decode. Both machines put the card's registers at bus
//      addresses 0..7, but they mirror them differently: the ABC80
//      hardware decodes only bits {0,1,2,4} of the port number (global
//      mask 0x17), where the ABC802 uses its own range decode. Neither
//      belongs in the shared card.
//
// One real asymmetry is worth recording, because it is the ROM's and not
// this emulator's: ABCDOS80.bin talks to exactly one kind of drive. It
// issues `LD A,2Dh / OUT (01h),A` at 0x60F1 - the ABC830 select code,
// hardcoded, the only OUT to the CS port anywhere in the image - where
// the ABC802's ROM scans the bus and the alternate UFD-DOS ROM
// (UFD80V20.bin, 0x61C1) reads its select from a variable. So a 640K
// ABC832 image attaches fine and is then never addressed by this ROM,
// which is why abc80_abcbus_init() says so out loud rather than leaving
// the user with a silently dead drive.

#include <stdio.h>
#include <string.h>

#include "abcbus.h"
#include "../../../abcbus/disk.h"

static bool rom_loaded = false;

bool abc80_abcbus_rom_loaded(void) {
    return rom_loaded;
}

// Global port mask 0x17 (MAME's own map.global_mask(0x17) for this
// machine): only bits {0,1,2,4} of the port number are decoded, so every
// port repeats every 0x18. Real ROM code exploits this - the keyboard is
// addressed as `IN A,(38h)` - so the mask has to be applied before the
// port number means anything.
#define ABC80_PORT_MASK 0x17

// Which bus register a port number lands on, or false for a port that is
// not the ABC bus at all. Post-mask 0x06 is the SN76477 sound register
// and 0x10-0x17 is the Z80 PIO; both are left to the CPU core's flat
// io_ports[] array, which is where the rest of this target already reads
// and writes them.
static bool bus_register(uint8_t port, int *index) {
    uint8_t p = (uint8_t)(port & ABC80_PORT_MASK);
    if (p <= 0x05) {          // 0 = INP/OUT, 1 = STAT/CS, 2..5 = C1..C4
        *index = p;
        return true;
    }
    if (p == 0x07) {          // RST - neither DOS ROM uses it; reads float
        *index = 7;
        return true;
    }
    return false;
}

uint8_t abc80_abcbus_io_in(Z80 *cpu, uint8_t port, uint8_t stored_value) {
    (void)cpu;
    int index;
    if (!bus_register(port, &index)) return stored_value;
    // With no card fitted this returns 0xFF - a floating bus, the same
    // thing this target's bus_read_hook already reports for the unpopulated
    // parts of the memory-side expansion range.
    return abcbus_disk_in(index);
}

int abc80_abcbus_io_out(Z80 *cpu, uint8_t port, uint8_t value) {
    (void)cpu;
    int index;
    if (!bus_register(port, &index)) return 0;  // fall through to io_ports[]
    if (index == 1) {
        // CS: which expansion card listens. UFD-DOS masks the select to
        // six bits itself (AND 3Fh at 0x61C1); ABC-DOS writes a bare
        // 0x2D, which the mask leaves alone. Masking here keeps the card
        // honest either way.
        abcbus_disk_select((uint8_t)(value & 0x3F));
    } else {
        abcbus_disk_out(index, value);
    }
    return 1;
}

// --disk: load the real ABC-DOS ROM at its real base address 0x6000 and
// attach the host file the card serves sectors from. Callers load this
// after their own floating-bus fill, not before, so this ROM content
// survives it (see main.c's own abc80_bus_read_hook()).
bool abc80_abcbus_init(const char *rom_dir, const char *dos_rom,
                       const char *const *disk_args, int disk_count,
                       uint8_t *ram) {
    char dos_rom_path[1024];
    snprintf(dos_rom_path, sizeof(dos_rom_path), "%s/%s", rom_dir,
             dos_rom ? dos_rom : ABC80_DEFAULT_DOS_ROM);
    FILE *dos_rom_f = fopen(dos_rom_path, "rb");
    if (!dos_rom_f) {
        fprintf(stderr, "Failed to open DOS ROM '%s': ", dos_rom_path);
        perror(NULL);
        return false;
    }
    size_t dos_rom_read = fread(&ram[0x6000], 1, 4096, dos_rom_f);
    fclose(dos_rom_f);
    if (dos_rom_read != 4096) {
        fprintf(stderr, "DOS ROM '%s' is not exactly 4096 bytes\n", dos_rom_path);
        return false;
    }
    // In command-line order, so two plain --disk arguments become the
    // ROM's own DR0: and DR1: without a second flag. The card has served
    // eight units since it was shared with the ABC802; only this
    // machine's CLI ever limited it to one.
    for (int d = 0; d < disk_count; d++) {
        if (!abcbus_disk_attach_arg(disk_args[d])) return false;
    }
    rom_loaded = true;
    printf("Loaded DOS ROM '%s' at 0x6000; ABC-bus: %s floppy controller, "
           "%d disk image%s, interleave %u\n",
           dos_rom_path, abcbus_disk_type_name(),
           abcbus_disk_attached_count(),
           abcbus_disk_attached_count() == 1 ? "" : "s",
           abcbus_disk_interleave());
    if (strcmp(abcbus_disk_type_name(), "mo") != 0) {
        fprintf(stderr,
                "Warning: ABCDOS80.bin only ever selects the ABC830 (device "
                "0x2D), so it will not see this drive.\n");
    }
    return true;
}
