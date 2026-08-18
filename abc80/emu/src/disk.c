// abc80/emu/src/disk.c - Milestone 6's floppy/DOS bypass, extracted out of
// main.c (Milestone 11) so both bin/abc80 (--interactive) and the future
// bin/abc80-gtk can share it. See abc80/docs/ABC80_ROADMAP.md's Floppy/DOS
// controller sub-step for the full disassembly-grounded derivation of
// everything below - this file only moved the code, it didn't change it.

#include <stdio.h>
#include <string.h>

#include "disk.h"
#include "../../../z80core/alu.h"

static bool disk_enabled = false;
static FILE *disk_file = NULL;

bool abc80_disk_enabled(void) {
    return disk_enabled;
}

// 256 bytes/block: strong circumstantial evidence from the real ROM's own
// transfer-count logic (a classic `LD B,0` / `DJNZ` idiom, where a loaded
// 0 wraps to 256 iterations - see the ROADMAP write-up's own derivation),
// not independently confirmed against a real disk image.
#define ABC80_DISK_BLOCK_SIZE 256

// A plausible T-state cost standing in for the real (bypassed) routine's
// own execution time, so this project's existing interrupt-scheduling and
// --interactive real-time-pacing arithmetic (both driven by accumulated
// T-states) aren't thrown off by treating real disk I/O as instantaneous/
// free - not meant to be a precise real-hardware timing match.
#define ABC80_DISK_TRAP_TSTATE_COST 200

// The real DOS workspace pointer this ROM's own init sets to 0xF500 (see
// the ROADMAP write-up) - read live from RAM rather than hardcoded, in
// case some future ROM variant or code path changes it.
#define ABC80_DOS_BUFPTR_ADDR 0xFD12

// Real ABC830 ("mo") media is sector-interleaved within each 16-sector
// track (interleave factor 7) - a real, physical disk-formatting detail,
// not an artifact of this project's own bypass. The logical sector number
// L6068/L60A1's calling convention computes (see abc80_disk_trap() below)
// is NOT the same as a raw .img file's own on-disk byte offset: real
// abc80.net-archived dumps (this project's own disk003.img among them)
// store sectors in physical order, so a logical sector must be mapped to
// its physical position before indexing the file. Confirmed against a
// real, independent open-source ABC80 emulator (andersrcarlsson-stack/
// abc80-pico-public, src/disk_controller.cpp's own phys_sector(), itself
// citing abc80sim's "mo" interleave parameters ilfac=7/ilmsk=15) rather
// than guessed - see ABC80_ROADMAP.md's Milestone 6 section for the
// empirical verification (every one of disk003.img's real directory
// entries resolves to a clean, consistent per-file header once this
// mapping is applied, where every one of them read as blank/garbled
// without it). Track-boundary sectors (multiples of 16) map to
// themselves, which is why every previous investigation round's tests
// that happened to land on one (the boot-time scan, the base directory
// at sector 16) worked "by accident" without this fix.
static uint16_t abc80_disk_phys_sector(uint16_t logical) {
    const uint16_t il_mask = 15;
    const uint16_t il_fac = 7;
    return (uint16_t)((logical & ~il_mask) | ((logical * il_fac) & il_mask));
}

static bool abc80_disk_read_block(uint16_t block, uint8_t *dest) {
    if (!disk_file) {
        memset(dest, 0, ABC80_DISK_BLOCK_SIZE);
        return false;
    }
    long offset = (long)abc80_disk_phys_sector(block) * ABC80_DISK_BLOCK_SIZE;
    if (fseek(disk_file, offset, SEEK_SET) != 0) {
        memset(dest, 0, ABC80_DISK_BLOCK_SIZE);
        return false;
    }
    size_t n = fread(dest, 1, ABC80_DISK_BLOCK_SIZE, disk_file);
    if (n < ABC80_DISK_BLOCK_SIZE) {
        // Reading at or past the real end of the image file - zero-fill
        // the partial/nonexistent block for a predictable buffer, but
        // report real failure (matching a real controller's own sector-
        // not-found response for an out-of-range block) rather than a
        // silent zero-filled success, which real ROM boot code observably
        // relies on to tell a real block apart from empty media (see
        // ABC80_ROADMAP.md's Milestone 6 section).
        memset(dest + n, 0, ABC80_DISK_BLOCK_SIZE - n);
        return false;
    }
    return true;
}

static bool abc80_disk_write_block(uint16_t block, const uint8_t *src) {
    if (!disk_file) return false;
    long offset = (long)abc80_disk_phys_sector(block) * ABC80_DISK_BLOCK_SIZE;
    if (fseek(disk_file, offset, SEEK_SET) != 0) return false;
    size_t n = fwrite(src, 1, ABC80_DISK_BLOCK_SIZE, disk_file);
    fflush(disk_file);
    return n == ABC80_DISK_BLOCK_SIZE;
}

int abc80_disk_trap(Z80 *cpu, uint8_t *ram, bool is_read) {
    // Confirmed, not just "not yet decoded": disassembling the real
    // ABC-DOS ROM's own L6106 (`LD A,B / AND 70h / RRCA x4`, both
    // L6068/read's and L60A1/write's shared channel-decode routine) shows
    // this is the ONLY place either real code path ever reads the
    // caller's original B - bits 0-3 and 7 are masked out by `AND 70h`
    // and never referenced anywhere else in either routine (confirmed by
    // reading through L606B/L607D/L608F for read and L60A4/L60B4/L60C1
    // for write in full). See ABC80_ROADMAP.md's Milestone 6 section for
    // the fuller writeup - this was a real, closed investigation, not an
    // assumption.
    uint8_t channel = (uint8_t)((cpu->b >> 4) & 0x07);
    // Real ABC830-class ("mo") controller sector addressing is NOT a flat
    // 16-bit (D<<8)|E block number - it's packed as (D<<3) + (E>>5) +
    // (E&31), confirmed against a real, independent open-source ABC80
    // emulator's own controller implementation (sasq64/abc80sim, src/
    // disk.c's cur_sector(), non-"new"/non-hard-disk case) rather than
    // guessed - see ABC80_ROADMAP.md's Milestone 6 section. Using the
    // flat interpretation silently sent every read/write to the wrong
    // real sector.
    uint16_t block = (uint16_t)(((uint16_t)cpu->d << 3) + (cpu->e >> 5) + (cpu->e & 0x1F));
    // ABC80_DOS_BUFPTR_ADDR's low byte is dual-purpose in the real ROM: the
    // low byte of the (always 256-aligned) buffer base is 0x00 by
    // construction, so the real ROM also reuses that same RAM cell as a
    // live byte-transfer progress counter in the real per-byte transfer
    // routines this bypass replaces wholesale (confirmed in
    // abcdos80_dasm.txt: several addresses outside L6068/L60A1 write only
    // this byte, e.g. 0x61C0's `LD (0xFD12),A`). Since this bypass never
    // runs that real routine, that counter can be left mid-count (observed
    // live: 0x03) by earlier, un-trapped ROM code, silently shifting every
    // subsequent buffer address by that leftover count and corrupting
    // whatever real data was already there - see ABC80_ROADMAP.md's
    // Milestone 6 section for the real SAVE this was found from. Only the
    // high byte is trustworthy to read live (DOS init sets it, and a
    // future ROM/config might legitimately relocate it); the low byte is
    // always forced to 0 rather than trusted.
    uint16_t buf_base = (uint16_t)(ram[ABC80_DOS_BUFPTR_ADDR + 1] << 8);
    uint16_t buf_addr = (uint16_t)(buf_base + (uint16_t)channel * ABC80_DISK_BLOCK_SIZE);

    // The real ROM's own failure handling (L608F for read, the inline
    // equivalent at 0x60C1-0x60D1 for write - both disassembled directly
    // from resources/rom/ABCDOS80.bin, see ABC80_ROADMAP.md's Milestone 6
    // section) is genuinely richer than this bypass's single collapsed
    // `ok` boolean: both real paths poll a live hardware status byte
    // (port 0, cached at RAM 0xFD15) and a 5-attempt retry counter (RAM
    // 0xFD18) before finally returning Carry set with either A=0 (read
    // path only, when the polled status byte is exactly 0) or A=the raw
    // status byte (both paths, when its bit 7 is set). Deliberately not
    // reproduced here: there's no real per-attempt hardware condition in
    // this bypass for a retry to plausibly recover from (a host
    // fread/fwrite failure is either a genuine out-of-range block or a
    // real disk-full condition, not a transient one), and doing so would
    // mean inventing a mapping from "which host failure" to "which of the
    // real ROM's status-port bit patterns" with no principled way to
    // derive it short of modeling port 0/1 hardware this bypass
    // intentionally doesn't. No real software failure has been observed
    // from this simplified single A=0x01 signal - documented as a known,
    // deliberate simplification, not solved, matching this project's own
    // precedent elsewhere (e.g. no SN76477 external-voltage-input modes,
    // no real keyboard scan-matrix PROM) for gaps found real but not
    // acted on absent a concrete failing case.
    bool ok = is_read ? abc80_disk_read_block(block, &ram[buf_addr])
                       : abc80_disk_write_block(block, (const uint8_t *)&ram[buf_addr]);

    uint16_t ret_addr = (uint16_t)(ram[cpu->sp] | (ram[(uint16_t)(cpu->sp + 1)] << 8));
    cpu->sp = (uint16_t)(cpu->sp + 2);
    cpu->pc = ret_addr;
    cpu->a = ok ? 0x00 : 0x01;
    if (ok) {
        cpu->f = (uint8_t)(cpu->f & ~FLAG_C);
    } else {
        cpu->f = (uint8_t)(cpu->f | FLAG_C);
    }
    return ABC80_DISK_TRAP_TSTATE_COST;
}

// --disk: load the real ABC-DOS ROM at its real base address 0x6000 and
// open/create the host file backing its virtual disk. Callers load this
// after their own floating-bus fill, not before, so this ROM content
// survives it (see main.c's own abc80_bus_read_hook()).
bool abc80_disk_init(const char *rom_dir, const char *disk_path, uint8_t *ram) {
    char dos_rom_path[1024];
    snprintf(dos_rom_path, sizeof(dos_rom_path), "%s/ABCDOS80.bin", rom_dir);
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
    disk_file = fopen(disk_path, "r+b");
    if (!disk_file) {
        disk_file = fopen(disk_path, "w+b");
    }
    if (!disk_file) {
        fprintf(stderr, "Failed to open disk image '%s': ", disk_path);
        perror(NULL);
        return false;
    }
    disk_enabled = true;
    printf("Loaded DOS ROM '%s' at 0x6000, disk image '%s'\n", dos_rom_path, disk_path);
    return true;
}
