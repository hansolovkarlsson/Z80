#include <stdio.h>
#include <string.h>
#include "../src/cpu.h"
#include "../src/cart.h"
#include "../src/ppu.h"
#include "../src/timer.h"
#include "../src/joypad.h"
#include "../src/apu.h"
#include "../src/savestate.h"

// Direct round-trip test for savestate.c: build every struct
// gb_savestate_save()/gb_savestate_load() touch (CPU, memory[], PPU,
// timer, joypad, APU, cart banking/RTC/battery-RAM) with a distinctive,
// non-zero/non-default value in every single field, save, deliberately
// stomp everything to a different set of values, load, and check every
// field came back exactly as saved. Complements
// GAMEBOY_SAVESTATE_TEST's real-ROM/real-driver round-trip (Makefile) -
// this one pins down every individual field directly, which a whole-ROM
// output comparison can't do (a field that's saved/restored wrong but
// happens not to affect dmg-acid2's own two frames would slip past that
// test silently).
//
// Uses a real GBCart struct literal (no gb_cart_load() file I/O),
// same minimal-dependency approach test_cart.c/test_cpu.c already use.
// MBC3 is chosen (not GB_MBC_NONE) specifically so the RTC and
// ram_bank/banking_mode fields are exercised too, not just MBC-agnostic
// ones.

static int failures = 0;

static void check(const char *name, int condition) {
    if (condition) {
        printf("OK   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        failures++;
    }
}

static const char *STATE_PATH = "/tmp/gb_test_savestate.state";

int main(void) {
    uint8_t rom[0x8000];
    for (size_t i = 0; i < sizeof(rom); i++) rom[i] = (uint8_t)(i * 37);
    uint8_t ram[0x2000];
    for (size_t i = 0; i < sizeof(ram); i++) ram[i] = (uint8_t)(i ^ 0xA5);

    GBCart cart = {0};
    cart.rom = rom;
    cart.rom_size = sizeof(rom);
    cart.rom_banks = 2;
    cart.ram = ram;
    cart.ram_size = sizeof(ram);
    cart.ram_banks = 1;
    cart.mbc_type = GB_MBC3;
    cart.has_ram = 1;
    cart.has_rtc = 1;
    cart.ram_enabled = 1;
    cart.rom_bank_lo = 0x05;
    cart.rom_bank_hi = 0x01;
    cart.ram_bank = 0x02;
    cart.banking_mode = 0x01;
    for (int i = 0; i < 5; i++) { cart.rtc[i] = (uint8_t)(0x10 + i); cart.rtc_latched[i] = (uint8_t)(0x20 + i); }
    cart.rtc_latch_step = 1;

    GBCpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    uint8_t memory[65536];
    for (size_t i = 0; i < sizeof(memory); i++) memory[i] = (uint8_t)(i * 91);
    cpu.memory = memory;
    cpu.cart = &cart;
    cpu.af = 0x1234; cpu.bc = 0x5678; cpu.de = 0x9ABC; cpu.hl = 0xDEF0;
    cpu.sp = 0xFFFE; cpu.pc = 0x0150;
    cpu.ime = 1; cpu.ime_pending = 1; cpu.ei_delay_active = 1;
    cpu.halted = 1; cpu.stopped = 0; cpu.halt_bug = 1;

    GBPpu ppu = {0};
    ppu.lcdc = 0x91; ppu.stat = 0x85; ppu.scy = 0x11; ppu.scx = 0x22;
    ppu.ly = 0x50; ppu.lyc = 0x50; ppu.dma = 0xC0;
    ppu.bgp = 0xE4; ppu.obp0 = 0xD3; ppu.obp1 = 0xC2;
    ppu.wy = 0x08; ppu.wx = 0x18;
    ppu.mode = 3; ppu.dots = 199; ppu.mode3_dots = 240; ppu.window_line = 12;
    ppu.frame_ready = 1;
    for (int y = 0; y < GB_SCREEN_HEIGHT; y++)
        for (int x = 0; x < GB_SCREEN_WIDTH; x++)
            ppu.framebuffer[y][x] = (uint8_t)((x + y) & 0x03);
    cpu.ppu = &ppu;

    GBTimer timer = {0};
    timer.sys_counter = 0xBEEF; timer.tima = 0x11; timer.tma = 0x22; timer.tac = 0x07;
    timer.overflow_delay = 3;
    cpu.timer = &timer;

    GBJoypad joypad = {0};
    joypad.select = 0x10; joypad.action_state = 0x0A; joypad.direction_state = 0x05;
    cpu.joypad = &joypad;

    GBApu apu = {0};
    gb_apu_reset(&apu, 44100, NULL, 0);
    apu.nr10 = 0x11; apu.nr11 = 0x22; apu.nr12 = 0x33; apu.nr13 = 0x44; apu.nr14 = 0x55;
    apu.nr21 = 0x66; apu.nr22 = 0x77; apu.nr23 = 0x88; apu.nr24 = 0x99;
    apu.nr30 = 0xAA; apu.nr31 = 0xBB; apu.nr32 = 0xCC; apu.nr33 = 0xDD; apu.nr34 = 0xEE;
    for (int i = 0; i < 16; i++) apu.wave_ram[i] = (uint8_t)(i * 17);
    apu.nr41 = 0x12; apu.nr42 = 0x34; apu.nr43 = 0x56; apu.nr44 = 0x78;
    apu.nr50 = 0x77; apu.nr51 = 0xF0;
    apu.enabled = 1;
    for (int i = 0; i < 4; i++) {
        GBApuChannel *c = &apu.ch[i];
        c->enabled = 1;
        c->period_timer = 100 + i;
        c->duty_step = i;
        c->wave_pos = 20 + i;
        c->wave_sample_buffer = (uint8_t)(0x0A + i);
        c->lfsr = (uint16_t)(0x7FFF - i);
        c->length_timer = 30 + i;
        c->volume = 15 - i;
        c->envelope_timer = 5 + i;
        c->envelope_going = 1;
        c->sweep_timer = 6 + i;
        c->sweep_enabled = 1;
        c->sweep_shadow = 200 + i;
    }
    apu.frame_seq_step = 5;
    apu.div_bit4_prev = 1;
    apu.sample_accum = 12.5;
    apu.hpf_capacitor_l = 0.125;
    apu.hpf_capacitor_r = -0.25;
    cpu.apu = &apu;

    check("save succeeds", gb_savestate_save(&cpu, STATE_PATH) == 0);

    // Stomp every field to a different value before loading, so a
    // field that's silently left untouched by gb_savestate_load()
    // (rather than genuinely restored) would be caught below instead of
    // accidentally passing because it was never disturbed.
    memset(memory, 0xFF, sizeof(memory));
    cpu.af = cpu.bc = cpu.de = cpu.hl = cpu.sp = cpu.pc = 0;
    cpu.ime = cpu.ime_pending = cpu.ei_delay_active = 0;
    cpu.halted = 0; cpu.stopped = 1; cpu.halt_bug = 0;
    memset(&ppu, 0, sizeof(ppu));
    memset(&timer, 0, sizeof(timer));
    memset(&joypad, 0, sizeof(joypad));
    GBApu blank_apu = {0};
    apu = blank_apu;
    cart.ram_enabled = 0; cart.rom_bank_lo = 0; cart.rom_bank_hi = 0;
    cart.ram_bank = 0; cart.banking_mode = 0;
    memset(cart.rtc, 0, sizeof(cart.rtc));
    memset(cart.rtc_latched, 0, sizeof(cart.rtc_latched));
    cart.rtc_latch_step = 0;
    memset(ram, 0, sizeof(ram));

    check("load succeeds", gb_savestate_load(&cpu, STATE_PATH) == 0);

    check("CPU: af/bc/de/hl", cpu.af == 0x1234 && cpu.bc == 0x5678 && cpu.de == 0x9ABC && cpu.hl == 0xDEF0);
    check("CPU: sp/pc", cpu.sp == 0xFFFE && cpu.pc == 0x0150);
    check("CPU: ime/ime_pending/ei_delay_active", cpu.ime == 1 && cpu.ime_pending == 1 && cpu.ei_delay_active == 1);
    check("CPU: halted/stopped/halt_bug", cpu.halted == 1 && cpu.stopped == 0 && cpu.halt_bug == 1);

    int mem_ok = 1;
    for (size_t i = 0; i < sizeof(memory); i++) {
        if (memory[i] != (uint8_t)(i * 91)) { mem_ok = 0; break; }
    }
    check("CPU: memory[] restored byte-for-byte", mem_ok);

    check("PPU: registers", ppu.lcdc == 0x91 && ppu.stat == 0x85 && ppu.scy == 0x11 && ppu.scx == 0x22 &&
                             ppu.ly == 0x50 && ppu.lyc == 0x50 && ppu.dma == 0xC0 &&
                             ppu.bgp == 0xE4 && ppu.obp0 == 0xD3 && ppu.obp1 == 0xC2 &&
                             ppu.wy == 0x08 && ppu.wx == 0x18);
    check("PPU: mode/dots/mode3_dots/window_line/frame_ready",
          ppu.mode == 3 && ppu.dots == 199 && ppu.mode3_dots == 240 &&
          ppu.window_line == 12 && ppu.frame_ready == 1);
    int fb_ok = 1;
    for (int y = 0; y < GB_SCREEN_HEIGHT && fb_ok; y++)
        for (int x = 0; x < GB_SCREEN_WIDTH; x++)
            if (ppu.framebuffer[y][x] != (uint8_t)((x + y) & 0x03)) { fb_ok = 0; break; }
    check("PPU: framebuffer restored byte-for-byte", fb_ok);

    check("Timer", timer.sys_counter == 0xBEEF && timer.tima == 0x11 && timer.tma == 0x22 &&
                    timer.tac == 0x07 && timer.overflow_delay == 3);

    check("Joypad", joypad.select == 0x10 && joypad.action_state == 0x0A && joypad.direction_state == 0x05);

    check("APU: NR registers", apu.nr10 == 0x11 && apu.nr11 == 0x22 && apu.nr12 == 0x33 &&
                                apu.nr13 == 0x44 && apu.nr14 == 0x55 &&
                                apu.nr21 == 0x66 && apu.nr22 == 0x77 && apu.nr23 == 0x88 && apu.nr24 == 0x99 &&
                                apu.nr30 == 0xAA && apu.nr31 == 0xBB && apu.nr32 == 0xCC &&
                                apu.nr33 == 0xDD && apu.nr34 == 0xEE &&
                                apu.nr41 == 0x12 && apu.nr42 == 0x34 && apu.nr43 == 0x56 && apu.nr44 == 0x78 &&
                                apu.nr50 == 0x77 && apu.nr51 == 0xF0 && apu.enabled == 1);
    int wave_ok = 1;
    for (int i = 0; i < 16; i++) if (apu.wave_ram[i] != (uint8_t)(i * 17)) wave_ok = 0;
    check("APU: wave RAM restored byte-for-byte", wave_ok);
    int ch_ok = 1;
    for (int i = 0; i < 4; i++) {
        GBApuChannel *c = &apu.ch[i];
        if (c->enabled != 1 || c->period_timer != 100 + i || c->duty_step != i ||
            c->wave_pos != 20 + i || c->wave_sample_buffer != (uint8_t)(0x0A + i) ||
            c->lfsr != (uint16_t)(0x7FFF - i) || c->length_timer != 30 + i ||
            c->volume != 15 - i || c->envelope_timer != 5 + i || c->envelope_going != 1 ||
            c->sweep_timer != 6 + i || c->sweep_enabled != 1 || c->sweep_shadow != 200 + i) {
            ch_ok = 0;
        }
    }
    check("APU: all 4 channels' internal state", ch_ok);
    check("APU: frame sequencer/DIV-edge/output-filter state",
          apu.frame_seq_step == 5 && apu.div_bit4_prev == 1 &&
          apu.sample_accum == 12.5 && apu.hpf_capacitor_l == 0.125 && apu.hpf_capacitor_r == -0.25);

    check("Cart: banking registers", cart.ram_enabled == 1 && cart.rom_bank_lo == 0x05 &&
                                      cart.rom_bank_hi == 0x01 && cart.ram_bank == 0x02 &&
                                      cart.banking_mode == 0x01);
    int rtc_ok = 1;
    for (int i = 0; i < 5; i++) {
        if (cart.rtc[i] != (uint8_t)(0x10 + i) || cart.rtc_latched[i] != (uint8_t)(0x20 + i)) rtc_ok = 0;
    }
    check("Cart: RTC registers", rtc_ok && cart.rtc_latch_step == 1);
    int ram_ok = 1;
    for (size_t i = 0; i < sizeof(ram); i++) if (ram[i] != (uint8_t)(i ^ 0xA5)) ram_ok = 0;
    check("Cart: battery RAM restored byte-for-byte", ram_ok);

    // A ROM mismatch must be refused, not silently loaded onto the
    // wrong cartridge - flip one byte deep in the ROM (changing its
    // fnv1a hash) and confirm gb_savestate_load() rejects the file
    // rather than restoring anything on top of it.
    rom[0x4000] ^= 0xFF;
    uint16_t pc_before = cpu.pc;
    check("load refuses a ROM that no longer matches the save state",
          gb_savestate_load(&cpu, STATE_PATH) != 0);
    check("a refused load doesn't clobber existing state", cpu.pc == pc_before);

    printf("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
