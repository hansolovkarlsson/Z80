#ifndef _GB_CART_H
#define _GB_CART_H

#include <stdint.h>
#include <stddef.h>

// Phase 2: real cartridge loading and Memory Bank Controller emulation,
// replacing Phase 1's "whole ROM is one flat, unbanked block" stand-in.
// Scope per docs/GAMEBOY_ROADMAP.md: no-MBC (plain 32KB ROM), MBC1,
// MBC3 (with its RTC), and MBC5 - covering the overwhelming majority of
// real cartridges. Every register layout/banking quirk below is
// grounded against pandocs' MBC1.md/MBC3.md/MBC5.md/nombc.md and
// The_Cartridge_Header.md (fetched during this phase), not guessed -
// see gameboy/src/cart.c's own comments for the specific citations.
typedef enum {
    GB_MBC_NONE,
    GB_MBC1,
    GB_MBC3,
    GB_MBC5,
} GBMbcType;

typedef struct GBCart {
    uint8_t *rom;
    size_t rom_size;
    int rom_banks; // always a power of 2 per the real ROM-size header codes

    uint8_t *ram; // NULL if the cartridge has no external RAM
    size_t ram_size;
    int ram_banks;

    GBMbcType mbc_type;
    int has_ram;
    int has_battery; // not yet used (no save-to-disk persistence this phase)
    int has_rtc;      // MBC3 only

    // The post-boot-ROM F register's H/C bits depend on whether this
    // exact header byte is zero, per pandocs' Power_Up_Sequence.md
    // footnote - see gb_cpu_reset()'s own comment. Distinct from
    // whether the checksum actually *validates* (see header_checksum_valid
    // below) - a real Game Boy would refuse to boot a cartridge with an
    // invalid checksum at all, so by the time code is running the two
    // almost always agree, but they're not the same question.
    uint8_t header_checksum_byte;
    uint8_t header_checksum_valid;

    // MBC1/3/5 banking state - raw register contents, not yet resolved
    // into an actual bank number (that happens at read time in cart.c,
    // since e.g. MBC1/MBC3's "register reads as 0 -> use bank 1 instead"
    // quirk is a read-time translation, not something to bake into the
    // stored value).
    uint8_t ram_enabled;
    uint8_t rom_bank_lo; // MBC1: 5 bits; MBC3: 7 bits; MBC5: low 8 bits
    uint8_t rom_bank_hi; // MBC5's 9th bit only
    uint8_t ram_bank;    // MBC1's secondary 2-bit register (RAM bank or ROM bank extension,
                          // depending on cartridge size - see cart.c); MBC3's RAM-bank-or-
                          // RTC-register selector; MBC5's 4-bit RAM bank
    uint8_t banking_mode; // MBC1 only: 0=simple, 1=advanced

    // MBC3 real-time clock. rtc holds the live, ticking values (not
    // actually advanced by wall-clock time yet - see cart.c); rtc_latched
    // is the snapshot the CPU actually reads/writes, updated only by the
    // 0x00-then-0x01 write sequence to 0x6000-0x7FFF.
    uint8_t rtc[5];         // S, M, H, DL, DH
    uint8_t rtc_latched[5];
    uint8_t rtc_latch_step;
} GBCart;

// Loads a .gb/.gbc file, parses its header (cartridge type, ROM/RAM size
// codes), and allocates rom/ram buffers accordingly. Returns 0 on
// success, -1 on a file error or a cartridge type/size this phase
// doesn't support (printed to stderr - never silently guessed).
int gb_cart_load(GBCart *cart, const char *path);
void gb_cart_free(GBCart *cart);

uint8_t gb_cart_read(GBCart *cart, uint16_t addr);              // 0x0000-0x7FFF
void gb_cart_write_ctrl(GBCart *cart, uint16_t addr, uint8_t val); // 0x0000-0x7FFF (MBC control registers - ROM itself is read-only)
uint8_t gb_cart_read_ram(GBCart *cart, uint16_t addr);          // 0xA000-0xBFFF
void gb_cart_write_ram(GBCart *cart, uint16_t addr, uint8_t val);

#endif
