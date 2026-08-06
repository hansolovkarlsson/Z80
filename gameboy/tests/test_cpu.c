#include <stdio.h>
#include <string.h>
#include "../src/cpu.h"
#include "../src/cart.h"

// Direct unit test for the "HALT immediately after EI" sub-case of the
// HALT bug - grounded against pandocs' halt.md: real hardware doesn't
// apply the generic double-fetch halt bug here. Instead HALT is
// effectively canceled outright (pc rewound back to HALT's own
// address), the already-pending interrupt dispatches completely
// normally on the *next* step (once EI's one-instruction delay has
// resolved to ime=1), using that unadvanced pc as its return address -
// so RETI naturally resumes execution back at the same HALT, which by
// then sees ime=1 for real and halts properly, per pandocs' own
// description: "the interrupt is serviced and the handler called, but
// the interrupt returns to the halt, which is executed again."
//
// Found via a real ROM (gameboy/test_roms/tobutobugirl/) whose main
// loop's own "ei; halt" idiom hit exactly this - treating it as the
// generic halt_bug case instead corrupted the stack by 2 bytes (see
// that ROM's own README.md for the full story) and eventually crashed
// on an illegal opcode after jumping to garbage.
//
// gb_cpu_step() needs gb_read_byte()/gb_write_byte() (mmu.c) but no
// cart/ppu/timer/joypad/apu at all, as long as the test keeps every
// address it touches inside flat memory (0x8000+, avoiding the
// specially-routed I/O ranges) - same minimal-dependency reasoning
// test_apu.c already uses.

static int failures = 0;

static void check(const char *name, int condition) {
    if (condition) {
        printf("OK   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        failures++;
    }
}

int main(void) {
    uint8_t memory[65536] = {0};
    // The real Timer vector (0x0050) is a ROM address (<0x8000), routed
    // through gb_cart_read() rather than the flat memory[] array below -
    // a minimal no-MBC cart backs it, same GBCart-struct-literal
    // approach test_cart.c already uses to bypass gb_cart_load()'s file
    // I/O entirely.
    uint8_t rom[0x8000] = {0};
    rom[0x0050] = 0xD9; // RETI - a minimal Timer-interrupt handler stub

    GBCart cart = {0};
    cart.rom = rom;
    cart.rom_size = sizeof(rom);
    cart.mbc_type = GB_MBC_NONE;

    GBCpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.memory = memory;
    cpu.cart = &cart;
    gb_cpu_init_tables();
    gb_cpu_reset(&cpu);

    // A tiny "ei; halt" idle loop in WRAM (0xC000) - enough to exercise
    // the whole dispatch/return cycle without needing any real
    // registered-handler machinery.
    memory[0xC000] = 0xFB; // EI
    memory[0xC001] = 0x76; // HALT

    cpu.pc = 0xC000;
    cpu.sp = 0xD000;
    cpu.ime = 0;
    memory[0xFFFF] = 0x04; // IE: Timer (bit 2) enabled
    memory[0xFF0F] = 0x04; // IF: Timer already pending

    gb_cpu_step(&cpu); // EI
    check("EI: ime still 0 immediately after (one-instruction delay)", cpu.ime == 0);
    check("EI: pc advanced past EI to HALT", cpu.pc == 0xC001);

    gb_cpu_step(&cpu); // HALT, the ei-delay sub-case
    check("HALT-after-EI: does not set the generic halt_bug", cpu.halt_bug == 0);
    check("HALT-after-EI: does not actually halt", cpu.halted == 0);
    check("HALT-after-EI: pc rewound back to HALT's own address", cpu.pc == 0xC001);

    uint16_t sp_before_dispatch = cpu.sp;
    gb_cpu_step(&cpu); // ime is now 1 (EI's delay resolved) - real dispatch
    check("dispatch: pc jumped to the Timer vector", cpu.pc == 0x0050);
    check("dispatch: ime cleared for the handler", cpu.ime == 0);
    check("dispatch: pushed the HALT instruction's own address (0xC001), not past it",
          (uint16_t)(memory[cpu.sp] | (memory[(uint16_t)(cpu.sp + 1)] << 8)) == 0xC001);
    check("dispatch: sp decreased by exactly 2 (a balanced single push)",
          cpu.sp == (uint16_t)(sp_before_dispatch - 2));

    gb_cpu_step(&cpu); // RETI
    check("RETI: returns to the HALT instruction, not somewhere else", cpu.pc == 0xC001);
    check("RETI: sp rebalanced back to its pre-dispatch value", cpu.sp == sp_before_dispatch);
    check("RETI: ime set", cpu.ime == 1);

    gb_cpu_step(&cpu); // HALT retried, this time with a genuine ime=1
    check("HALT retried: halts for real now that ime is genuinely 1", cpu.halted == 1);
    check("HALT retried: still no halt_bug", cpu.halt_bug == 0);

    if (failures == 0) {
        printf("\nAll cpu.c tests passed.\n");
        return 0;
    }
    printf("\n%d test(s) FAILED.\n", failures);
    return 1;
}
