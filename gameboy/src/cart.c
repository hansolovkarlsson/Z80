#include "cart.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// ROM size code -> bank count: pandocs' The_Cartridge_Header.md lists
// 0x00-0x08 as 32KiB * 2^code (2 banks * 2^code, since a bank is 16KiB).
// The 0x52/0x53/0x54 "weird" codes are explicitly documented there as
// unofficial and unconfirmed by any known real cartridge or ROM file -
// rejected rather than guessed at.
static int rom_banks_for_code(uint8_t code) {
    if (code > 0x08) return -1;
    return 2 << code;
}

// RAM size code -> bank count (8KiB each), same page. 0x01 is officially
// "Unused" (pandocs' The_Cartridge_Header.md, "0149 - RAM size" table and
// its own footnote: "Listed in various unofficial docs as 2 KiB. However,
// a 2 KiB RAM chip was never used in a cartridge. The source of this
// value is unknown.") - but pandocs also documents real ROMs using it
// anyway: "Various 'PD' ROMs... are known to use the $01 RAM Size tag,
// but this is believed to have been a mistake with early homebrew tools,
// and the PD ROMs often don't use cartridge RAM at all." Confirmed via a
// real one - Phase 6's 2048-gb (cart_type 0x03/MBC1+RAM+BATT, ram_code
// 0x01, see gameboy/test_roms/2048-gb/) failed to load at all before this
// fix. Treated as 0 banks (no cartridge RAM), matching pandocs'
// "often don't use cartridge RAM at all" and letting has_ram/ram_size's
// existing zero-size handling (gb_cart_read/write_ram() below) take over
// rather than rejecting a real, distributed ROM outright.
static int ram_banks_for_code(uint8_t code) {
    switch (code) {
        case 0x00: return 0;
        case 0x01: return 0;
        case 0x02: return 1;
        case 0x03: return 4;
        case 0x04: return 16;
        case 0x05: return 8;
        default: return -1;
    }
}

int gb_cart_load(GBCart *cart, const char *path) {
    memset(cart, 0, sizeof(*cart));

    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Couldn't open '%s'\n", path);
        return -1;
    }

    fseek(f, 0, SEEK_END);
    long file_size = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (file_size < 0x150) {
        fprintf(stderr, "'%s' is too small (%ld bytes) to contain a real cartridge header\n", path, file_size);
        fclose(f);
        return -1;
    }

    uint8_t *rom = malloc((size_t)file_size);
    size_t n = fread(rom, 1, (size_t)file_size, f);
    fclose(f);
    if (n != (size_t)file_size) {
        fprintf(stderr, "Short read on '%s'\n", path);
        free(rom);
        return -1;
    }

    uint8_t cart_type = rom[0x0147];
    uint8_t rom_code = rom[0x0148];
    uint8_t ram_code = rom[0x0149];

    int rom_banks = rom_banks_for_code(rom_code);
    if (rom_banks < 0) {
        fprintf(stderr, "'%s': unrecognized ROM size code 0x%02X\n", path, rom_code);
        free(rom);
        return -1;
    }
    int ram_banks = ram_banks_for_code(ram_code);
    if (ram_banks < 0) {
        fprintf(stderr, "'%s': unrecognized RAM size code 0x%02X\n", path, ram_code);
        free(rom);
        return -1;
    }

    size_t expected_rom_size = (size_t)rom_banks * 0x4000;
    if ((size_t)file_size < expected_rom_size) {
        fprintf(stderr, "'%s': header claims %d ROM bank(s) (%zu bytes) but the file is only %ld bytes\n",
                path, rom_banks, expected_rom_size, file_size);
        free(rom);
        return -1;
    }

    GBMbcType mbc_type;
    int has_ram = 0, has_battery = 0, has_rtc = 0;
    switch (cart_type) {
        case 0x00: mbc_type = GB_MBC_NONE; break;
        case 0x08: mbc_type = GB_MBC_NONE; has_ram = 1; break;
        case 0x09: mbc_type = GB_MBC_NONE; has_ram = 1; has_battery = 1; break;
        case 0x01: mbc_type = GB_MBC1; break;
        case 0x02: mbc_type = GB_MBC1; has_ram = 1; break;
        case 0x03: mbc_type = GB_MBC1; has_ram = 1; has_battery = 1; break;
        case 0x0F: mbc_type = GB_MBC3; has_rtc = 1; has_battery = 1; break;
        case 0x10: mbc_type = GB_MBC3; has_rtc = 1; has_ram = 1; has_battery = 1; break;
        case 0x11: mbc_type = GB_MBC3; break;
        case 0x12: mbc_type = GB_MBC3; has_ram = 1; break;
        case 0x13: mbc_type = GB_MBC3; has_ram = 1; has_battery = 1; break;
        case 0x19: mbc_type = GB_MBC5; break;
        case 0x1A: mbc_type = GB_MBC5; has_ram = 1; break;
        case 0x1B: mbc_type = GB_MBC5; has_ram = 1; has_battery = 1; break;
        // +RUMBLE variants: the rumble motor itself isn't modeled (no
        // host feedback channel to drive), but the MBC5 memory/banking
        // behavior is otherwise identical, so these load and run fine.
        case 0x1C: mbc_type = GB_MBC5; break;
        case 0x1D: mbc_type = GB_MBC5; has_ram = 1; break;
        case 0x1E: mbc_type = GB_MBC5; has_ram = 1; has_battery = 1; break;
        default:
            fprintf(stderr,
                    "'%s': unsupported cartridge type 0x%02X - only MBC-less/MBC1/MBC3/MBC5 "
                    "are implemented so far (see gameboy/docs/GAMEBOY_ROADMAP.md)\n",
                    path, cart_type);
            free(rom);
            return -1;
    }

    cart->rom = rom;
    cart->rom_size = (size_t)file_size;
    cart->rom_banks = rom_banks;
    cart->mbc_type = mbc_type;
    cart->has_ram = has_ram;
    cart->has_battery = has_battery;
    cart->has_rtc = has_rtc;

    if (has_ram && ram_banks > 0) {
        cart->ram_banks = ram_banks;
        cart->ram_size = (size_t)ram_banks * 0x2000;
        cart->ram = calloc(1, cart->ram_size);
    }

    // Real DMG boot ROM header-checksum algorithm, grounded against
    // pandocs' The_Cartridge_Header.md (fetched during this phase) -
    // real hardware locks up and never runs the cartridge at all if
    // this doesn't match the stored byte at 0x014D. header_checksum_byte
    // (not whether it validates) is what gb_cpu_reset() actually needs -
    // see cart.h's own comment on why those are different questions.
    uint8_t checksum = 0;
    for (uint16_t addr = 0x0134; addr <= 0x014C; addr++) {
        checksum = (uint8_t)(checksum - rom[addr] - 1);
    }
    cart->header_checksum_byte = rom[0x014D];
    cart->header_checksum_valid = (checksum == rom[0x014D]);
    if (!cart->header_checksum_valid) {
        fprintf(stderr,
                "'%s': header checksum mismatch (computed 0x%02X, stored 0x%02X) - "
                "a real Game Boy would refuse to run this; continuing anyway\n",
                path, checksum, rom[0x014D]);
    }

    fprintf(stderr, "Loaded '%s': %d ROM bank(s), %d RAM bank(s), mbc=%d%s%s\n",
            path, rom_banks, ram_banks, (int)mbc_type,
            has_battery ? " +battery" : "", has_rtc ? " +rtc" : "");

    return 0;
}

void gb_cart_free(GBCart *cart) {
    free(cart->rom);
    free(cart->ram);
    cart->rom = NULL;
    cart->ram = NULL;
}

uint8_t gb_cart_read(GBCart *cart, uint16_t addr) {
    int bank;

    if (cart->mbc_type == GB_MBC_NONE) {
        return (addr < cart->rom_size) ? cart->rom[addr] : 0xFF;
    }

    if (cart->mbc_type == GB_MBC1) {
        // pandocs' MBC1.md: 0000-3FFF is bank 0 except in "advanced"
        // mode on large-ROM (>512KiB) carts, where the secondary 2-bit
        // register also applies here (letting banks 0x20/0x40/0x60 be
        // reached at all, since 4000-7FFF's own 0->1 quirk below can
        // never land exactly on one of those otherwise).
        if (addr < 0x4000) {
            bank = 0;
            if (cart->banking_mode == 1 && cart->rom_banks > 32) {
                bank = (cart->ram_bank & 0x03) << 5;
            }
        } else {
            int lo = cart->rom_bank_lo & 0x1F;
            if (lo == 0) lo = 1; // "cannot duplicate bank 0" quirk
            bank = lo;
            if (cart->rom_banks > 32) {
                bank |= (cart->ram_bank & 0x03) << 5;
            }
        }
    } else if (cart->mbc_type == GB_MBC3) {
        // pandocs' MBC3.md: same 0->1 quirk as MBC1 on the 7-bit
        // register, but (unlike MBC1) banks 0x20/0x40/0x60 are ordinary,
        // directly reachable banks - no secondary register/mode games
        // needed to reach them.
        if (addr < 0x4000) {
            bank = 0;
        } else {
            bank = cart->rom_bank_lo & 0x7F;
            if (bank == 0) bank = 1;
        }
    } else { // GB_MBC5
        // pandocs' MBC5.md: explicitly "bank 0 is actually bank 0" -
        // no 0->1 quirk here, the one real difference from MBC1/MBC3's
        // otherwise-similar ROM banking.
        bank = (addr < 0x4000) ? 0 : (((cart->rom_bank_hi & 0x01) << 8) | cart->rom_bank_lo);
    }

    bank &= (cart->rom_banks - 1); // rom_banks is always a power of 2
    size_t offset = (size_t)bank * 0x4000 + (addr & 0x3FFF);
    return (offset < cart->rom_size) ? cart->rom[offset] : 0xFF;
}

void gb_cart_write_ctrl(GBCart *cart, uint16_t addr, uint8_t val) {
    if (cart->mbc_type == GB_MBC_NONE) {
        return; // plain ROM, no registers to write
    }

    if (cart->mbc_type == GB_MBC1) {
        if (addr < 0x2000) {
            cart->ram_enabled = ((val & 0x0F) == 0x0A);
        } else if (addr < 0x4000) {
            cart->rom_bank_lo = val & 0x1F;
        } else if (addr < 0x6000) {
            cart->ram_bank = val & 0x03; // "secondary register": RAM bank, or ROM bank bits 5-6
        } else {
            cart->banking_mode = val & 0x01;
        }
        return;
    }

    if (cart->mbc_type == GB_MBC3) {
        if (addr < 0x2000) {
            cart->ram_enabled = ((val & 0x0F) == 0x0A); // also gates RTC register access
        } else if (addr < 0x4000) {
            cart->rom_bank_lo = val & 0x7F;
        } else if (addr < 0x6000) {
            cart->ram_bank = val; // 0x00-0x07 RAM bank, 0x08-0x0C RTC register select
        } else {
            // Latch sequence: writing 0x00 then 0x01 snapshots the live
            // RTC into rtc_latched, which is what A000-BFFF actually
            // exposes - lets a program read a stable set of registers
            // while the clock keeps ticking underneath.
            if (cart->rtc_latch_step == 0 && val == 0x00) {
                cart->rtc_latch_step = 1;
            } else if (cart->rtc_latch_step == 1 && val == 0x01) {
                memcpy(cart->rtc_latched, cart->rtc, sizeof(cart->rtc));
                cart->rtc_latch_step = 0;
            } else {
                cart->rtc_latch_step = 0;
            }
        }
        return;
    }

    // GB_MBC5
    if (addr < 0x2000) {
        cart->ram_enabled = ((val & 0x0F) == 0x0A);
    } else if (addr < 0x3000) {
        cart->rom_bank_lo = val; // low 8 bits - writing 0 really does mean bank 0 here
    } else if (addr < 0x4000) {
        cart->rom_bank_hi = val & 0x01; // the 9th bank bit
    } else if (addr < 0x6000) {
        cart->ram_bank = val & 0x0F; // bit 3 doubles as the rumble-motor line on +RUMBLE carts - not modeled
    }
    // 0x6000-0x7FFF: MBC5 has no register here
}

uint8_t gb_cart_read_ram(GBCart *cart, uint16_t addr) {
    if (cart->mbc_type == GB_MBC3 && cart->ram_bank >= 0x08 && cart->ram_bank <= 0x0C) {
        if (!cart->ram_enabled) return 0xFF;
        return cart->rtc_latched[cart->ram_bank - 0x08];
    }
    if (!cart->has_ram || !cart->ram_enabled || cart->ram_size == 0) return 0xFF;

    int bank = 0;
    if (cart->mbc_type == GB_MBC1) {
        if (cart->banking_mode == 1 && cart->ram_banks > 1) bank = cart->ram_bank & 0x03;
    } else { // MBC3 / MBC5
        bank = cart->ram_bank;
    }
    bank %= cart->ram_banks;
    size_t offset = (size_t)bank * 0x2000 + (addr - 0xA000);
    return (offset < cart->ram_size) ? cart->ram[offset] : 0xFF;
}

void gb_cart_write_ram(GBCart *cart, uint16_t addr, uint8_t val) {
    if (cart->mbc_type == GB_MBC3 && cart->ram_bank >= 0x08 && cart->ram_bank <= 0x0C) {
        if (cart->ram_enabled) cart->rtc[cart->ram_bank - 0x08] = val;
        return;
    }
    if (!cart->has_ram || !cart->ram_enabled || cart->ram_size == 0) return;

    int bank = 0;
    if (cart->mbc_type == GB_MBC1) {
        if (cart->banking_mode == 1 && cart->ram_banks > 1) bank = cart->ram_bank & 0x03;
    } else { // MBC3 / MBC5
        bank = cart->ram_bank;
    }
    bank %= cart->ram_banks;
    size_t offset = (size_t)bank * 0x2000 + (addr - 0xA000);
    if (offset < cart->ram_size) cart->ram[offset] = val;
}
