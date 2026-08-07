#include "savestate.h"
#include "mmu.h"
#include "cart.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"
#include "apu.h"
#include <stdio.h>
#include <string.h>

#define SAVESTATE_MAGIC "GBSS"
#define SAVESTATE_VERSION 1u

// Explicit little-endian primitives - the same reasoning main.c's own
// write_u32le()/write_u16le() (its WAV writer) already applies: on-disk
// formats need a defined byte order, not whatever memcpy of a struct
// would happen to produce on this build's host endianness.
static void w8(FILE *f, uint8_t v) { fputc(v, f); }
static void w16(FILE *f, uint16_t v) { w8(f, (uint8_t)v); w8(f, (uint8_t)(v >> 8)); }
static void w32(FILE *f, uint32_t v) { w16(f, (uint16_t)v); w16(f, (uint16_t)(v >> 16)); }

// Each read accumulates failure into *err rather than returning a
// per-call status - with ~60 fields to restore, checking one flag once
// at the end is far less error-prone than a return-code check after
// every single call (and a missed check would silently proceed with
// whatever garbage was left in an uninitialized destination).
static void r8(FILE *f, uint8_t *v, int *err) {
    int c = fgetc(f);
    if (c == EOF) { *err = 1; *v = 0; return; }
    *v = (uint8_t)c;
}
static void r16(FILE *f, uint16_t *v, int *err) {
    uint8_t lo = 0, hi = 0;
    r8(f, &lo, err); r8(f, &hi, err);
    *v = (uint16_t)(lo | (hi << 8));
}
static void r32(FILE *f, uint32_t *v, int *err) {
    uint16_t lo = 0, hi = 0;
    r16(f, &lo, err); r16(f, &hi, err);
    *v = (uint32_t)lo | ((uint32_t)hi << 16);
}

// FNV-1a, 32-bit - a real, well-known non-cryptographic hash (not
// invented here), used only to fingerprint "does this save state belong
// to the ROM currently loaded", not for security. Chosen over CRC32 to
// avoid needing a 256-entry table (or a zlib dependency this project
// doesn't otherwise have) for what's fundamentally a corruption/mismatch
// check, not a format this needs to interoperate with anything else.
static uint32_t fnv1a(const uint8_t *data, size_t len) {
    uint32_t h = 0x811c9dc5u;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 0x01000193u;
    }
    return h;
}

int gb_savestate_save(GBCpu *cpu, const char *path) {
    FILE *f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "Couldn't open '%s' for writing\n", path);
        return -1;
    }

    GBCart *cart = cpu->cart;
    GBPpu *ppu = cpu->ppu;
    GBTimer *timer = cpu->timer;
    GBJoypad *joypad = cpu->joypad;
    GBApu *apu = cpu->apu;

    fwrite(SAVESTATE_MAGIC, 1, 4, f);
    w32(f, SAVESTATE_VERSION);

    // ROM fingerprint - checked by gb_savestate_load() before anything
    // else is restored (see savestate.h's own comment on why).
    w32(f, (uint32_t)cart->rom_size);
    w32(f, fnv1a(cart->rom, cart->rom_size));

    // CPU registers + full memory[] (VRAM/WRAM/OAM/I-O-regs/HRAM - see
    // mmu.c for exactly what that does and doesn't cover; PPU/timer/
    // joypad/APU registers live in their own structs below instead).
    w16(f, cpu->af); w16(f, cpu->bc); w16(f, cpu->de); w16(f, cpu->hl);
    w16(f, cpu->sp); w16(f, cpu->pc);
    w8(f, cpu->ime); w8(f, cpu->ime_pending); w8(f, cpu->ei_delay_active);
    w8(f, cpu->halted); w8(f, cpu->stopped); w8(f, cpu->halt_bug);
    fwrite(cpu->memory, 1, GB_MEM_SIZE, f);

    // PPU
    w8(f, ppu->lcdc); w8(f, ppu->stat); w8(f, ppu->scy); w8(f, ppu->scx);
    w8(f, ppu->ly); w8(f, ppu->lyc); w8(f, ppu->dma);
    w8(f, ppu->bgp); w8(f, ppu->obp0); w8(f, ppu->obp1);
    w8(f, ppu->wy); w8(f, ppu->wx);
    w32(f, (uint32_t)ppu->mode);
    w32(f, (uint32_t)ppu->dots);
    w32(f, (uint32_t)ppu->mode3_dots);
    w32(f, (uint32_t)ppu->window_line);
    w8(f, (uint8_t)ppu->frame_ready);
    fwrite(ppu->framebuffer, 1, sizeof(ppu->framebuffer), f);

    // Timer
    w16(f, timer->sys_counter);
    w8(f, timer->tima); w8(f, timer->tma); w8(f, timer->tac);
    w32(f, (uint32_t)timer->overflow_delay);

    // Joypad
    w8(f, joypad->select); w8(f, joypad->action_state); w8(f, joypad->direction_state);

    // APU
    w8(f, apu->nr10); w8(f, apu->nr11); w8(f, apu->nr12); w8(f, apu->nr13); w8(f, apu->nr14);
    w8(f, apu->nr21); w8(f, apu->nr22); w8(f, apu->nr23); w8(f, apu->nr24);
    w8(f, apu->nr30); w8(f, apu->nr31); w8(f, apu->nr32); w8(f, apu->nr33); w8(f, apu->nr34);
    fwrite(apu->wave_ram, 1, sizeof(apu->wave_ram), f);
    w8(f, apu->nr41); w8(f, apu->nr42); w8(f, apu->nr43); w8(f, apu->nr44);
    w8(f, apu->nr50); w8(f, apu->nr51);
    w8(f, (uint8_t)apu->enabled);
    for (int i = 0; i < 4; i++) {
        GBApuChannel *c = &apu->ch[i];
        w8(f, (uint8_t)c->enabled);
        w32(f, (uint32_t)c->period_timer);
        w32(f, (uint32_t)c->duty_step);
        w32(f, (uint32_t)c->wave_pos);
        w8(f, c->wave_sample_buffer);
        w16(f, c->lfsr);
        w32(f, (uint32_t)c->length_timer);
        w32(f, (uint32_t)c->volume);
        w32(f, (uint32_t)c->envelope_timer);
        w8(f, (uint8_t)c->envelope_going);
        w32(f, (uint32_t)c->sweep_timer);
        w8(f, (uint8_t)c->sweep_enabled);
        w32(f, (uint32_t)c->sweep_shadow);
    }
    w32(f, (uint32_t)apu->frame_seq_step);
    w8(f, (uint8_t)apu->div_bit4_prev);
    // Raw 8-byte doubles, not converted to a fixed-point format - this
    // project only ever reads back a save state on the same build it was
    // written on (never exchanged across machines/architectures), so
    // there's no real portability requirement to design around here.
    fwrite(&apu->sample_accum, sizeof(double), 1, f);
    fwrite(&apu->hpf_capacitor_l, sizeof(double), 1, f);
    fwrite(&apu->hpf_capacitor_r, sizeof(double), 1, f);

    // Cartridge: banking registers, RTC, and battery RAM (the actual
    // save data a real cartridge's battery would have preserved) - NOT
    // the ROM itself, which the fingerprint above already assumes is
    // identical to whatever's currently loaded.
    w8(f, cart->ram_enabled);
    w8(f, cart->rom_bank_lo); w8(f, cart->rom_bank_hi);
    w8(f, cart->ram_bank); w8(f, cart->banking_mode);
    fwrite(cart->rtc, 1, sizeof(cart->rtc), f);
    fwrite(cart->rtc_latched, 1, sizeof(cart->rtc_latched), f);
    w8(f, cart->rtc_latch_step);
    w32(f, (uint32_t)cart->ram_size);
    if (cart->ram_size > 0) fwrite(cart->ram, 1, cart->ram_size, f);

    int ok = !ferror(f);
    fclose(f);
    if (!ok) {
        fprintf(stderr, "Error writing save state to '%s'\n", path);
        return -1;
    }
    fprintf(stderr, "Saved state to '%s'\n", path);
    return 0;
}

int gb_savestate_load(GBCpu *cpu, const char *path) {
    FILE *f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "Couldn't open '%s' for reading\n", path);
        return -1;
    }

    char magic[4];
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, SAVESTATE_MAGIC, 4) != 0) {
        fprintf(stderr, "'%s' is not a Game Boy save state file\n", path);
        fclose(f);
        return -1;
    }

    int err = 0;
    uint32_t version;
    r32(f, &version, &err);
    if (err || version != SAVESTATE_VERSION) {
        fprintf(stderr, "'%s': unsupported save state version\n", path);
        fclose(f);
        return -1;
    }

    GBCart *cart = cpu->cart;
    uint32_t rom_size, rom_hash;
    r32(f, &rom_size, &err);
    r32(f, &rom_hash, &err);
    if (err) {
        fprintf(stderr, "'%s': truncated save state file\n", path);
        fclose(f);
        return -1;
    }
    if (rom_size != (uint32_t)cart->rom_size || rom_hash != fnv1a(cart->rom, cart->rom_size)) {
        fprintf(stderr, "'%s': save state doesn't match the currently loaded ROM\n", path);
        fclose(f);
        return -1;
    }

    GBPpu *ppu = cpu->ppu;
    GBTimer *timer = cpu->timer;
    GBJoypad *joypad = cpu->joypad;
    GBApu *apu = cpu->apu;
    uint32_t u32v;
    uint8_t u8v;

    // CPU
    r16(f, &cpu->af, &err); r16(f, &cpu->bc, &err); r16(f, &cpu->de, &err); r16(f, &cpu->hl, &err);
    r16(f, &cpu->sp, &err); r16(f, &cpu->pc, &err);
    r8(f, &cpu->ime, &err); r8(f, &cpu->ime_pending, &err); r8(f, &cpu->ei_delay_active, &err);
    r8(f, &cpu->halted, &err); r8(f, &cpu->stopped, &err); r8(f, &cpu->halt_bug, &err);
    if (!err && fread(cpu->memory, 1, GB_MEM_SIZE, f) != GB_MEM_SIZE) err = 1;

    // PPU
    r8(f, &ppu->lcdc, &err); r8(f, &ppu->stat, &err); r8(f, &ppu->scy, &err); r8(f, &ppu->scx, &err);
    r8(f, &ppu->ly, &err); r8(f, &ppu->lyc, &err); r8(f, &ppu->dma, &err);
    r8(f, &ppu->bgp, &err); r8(f, &ppu->obp0, &err); r8(f, &ppu->obp1, &err);
    r8(f, &ppu->wy, &err); r8(f, &ppu->wx, &err);
    r32(f, &u32v, &err); ppu->mode = (int)u32v;
    r32(f, &u32v, &err); ppu->dots = (int)u32v;
    r32(f, &u32v, &err); ppu->mode3_dots = (int)u32v;
    r32(f, &u32v, &err); ppu->window_line = (int)u32v;
    r8(f, &u8v, &err); ppu->frame_ready = u8v;
    if (!err && fread(ppu->framebuffer, 1, sizeof(ppu->framebuffer), f) != sizeof(ppu->framebuffer)) err = 1;

    // Timer
    r16(f, &timer->sys_counter, &err);
    r8(f, &timer->tima, &err); r8(f, &timer->tma, &err); r8(f, &timer->tac, &err);
    r32(f, &u32v, &err); timer->overflow_delay = (int)u32v;

    // Joypad
    r8(f, &joypad->select, &err); r8(f, &joypad->action_state, &err); r8(f, &joypad->direction_state, &err);

    // APU
    r8(f, &apu->nr10, &err); r8(f, &apu->nr11, &err); r8(f, &apu->nr12, &err); r8(f, &apu->nr13, &err); r8(f, &apu->nr14, &err);
    r8(f, &apu->nr21, &err); r8(f, &apu->nr22, &err); r8(f, &apu->nr23, &err); r8(f, &apu->nr24, &err);
    r8(f, &apu->nr30, &err); r8(f, &apu->nr31, &err); r8(f, &apu->nr32, &err); r8(f, &apu->nr33, &err); r8(f, &apu->nr34, &err);
    if (!err && fread(apu->wave_ram, 1, sizeof(apu->wave_ram), f) != sizeof(apu->wave_ram)) err = 1;
    r8(f, &apu->nr41, &err); r8(f, &apu->nr42, &err); r8(f, &apu->nr43, &err); r8(f, &apu->nr44, &err);
    r8(f, &apu->nr50, &err); r8(f, &apu->nr51, &err);
    r8(f, &u8v, &err); apu->enabled = u8v;
    for (int i = 0; i < 4; i++) {
        GBApuChannel *c = &apu->ch[i];
        r8(f, &u8v, &err); c->enabled = u8v;
        r32(f, &u32v, &err); c->period_timer = (int)u32v;
        r32(f, &u32v, &err); c->duty_step = (int)u32v;
        r32(f, &u32v, &err); c->wave_pos = (int)u32v;
        r8(f, &c->wave_sample_buffer, &err);
        r16(f, &c->lfsr, &err);
        r32(f, &u32v, &err); c->length_timer = (int)u32v;
        r32(f, &u32v, &err); c->volume = (int)u32v;
        r32(f, &u32v, &err); c->envelope_timer = (int)u32v;
        r8(f, &u8v, &err); c->envelope_going = u8v;
        r32(f, &u32v, &err); c->sweep_timer = (int)u32v;
        r8(f, &u8v, &err); c->sweep_enabled = u8v;
        r32(f, &u32v, &err); c->sweep_shadow = (int)u32v;
    }
    r32(f, &u32v, &err); apu->frame_seq_step = (int)u32v;
    r8(f, &u8v, &err); apu->div_bit4_prev = u8v;
    if (!err && fread(&apu->sample_accum, sizeof(double), 1, f) != 1) err = 1;
    if (!err && fread(&apu->hpf_capacitor_l, sizeof(double), 1, f) != 1) err = 1;
    if (!err && fread(&apu->hpf_capacitor_r, sizeof(double), 1, f) != 1) err = 1;

    // Cartridge
    r8(f, &cart->ram_enabled, &err);
    r8(f, &cart->rom_bank_lo, &err); r8(f, &cart->rom_bank_hi, &err);
    r8(f, &cart->ram_bank, &err); r8(f, &cart->banking_mode, &err);
    if (!err && fread(cart->rtc, 1, sizeof(cart->rtc), f) != sizeof(cart->rtc)) err = 1;
    if (!err && fread(cart->rtc_latched, 1, sizeof(cart->rtc_latched), f) != sizeof(cart->rtc_latched)) err = 1;
    r8(f, &cart->rtc_latch_step, &err);
    uint32_t ram_size;
    r32(f, &ram_size, &err);
    if (!err && ram_size != (uint32_t)cart->ram_size) err = 1;
    if (!err && ram_size > 0 && fread(cart->ram, 1, ram_size, f) != ram_size) err = 1;

    fclose(f);
    if (err) {
        fprintf(stderr, "'%s': truncated or corrupt save state file\n", path);
        return -1;
    }
    fprintf(stderr, "Loaded state from '%s'\n", path);
    return 0;
}
