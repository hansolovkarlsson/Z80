#include <stdio.h>
#include <string.h>
#include "../src/cpu.h"
#include "../src/timer.h"

// Direct unit tests for timer.c's trickiest documented behaviors -
// grounded against pandocs' Timer_and_Divider_Registers.md/
// Timer_Obscure_Behaviour.md (fetched during Phase 4 - see
// gameboy/docs/GAMEBOY_ROADMAP.md). All 12 of Blargg's cpu_instrs/instr_timing
// ROMs pass with this timer (see the roadmap's Phase 4 status), which
// already covers a lot - but the specific DIV/TAC-write "spurious
// tick" quirks are obscure enough that they're worth a direct,
// permanent, ROM-independent check too, the same reasoning
// test_cart.c already applies to the MBC banking logic.

static int failures = 0;

static void check(const char *name, int condition) {
    if (condition) {
        printf("OK   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        failures++;
    }
}

// A minimal GBCpu just to give gb_write_byte(cpu, 0xFF0F, ...) (used by
// timer.c to request interrupts) somewhere real to write - no cart/ppu
// needed since nothing here touches ROM or PPU registers.
static GBCpu make_test_cpu(uint8_t *memory) {
    GBCpu cpu;
    memset(&cpu, 0, sizeof(cpu));
    cpu.memory = memory;
    return cpu;
}

static uint8_t if_reg(GBCpu *cpu) { return cpu->memory[0xFF0F]; }

static void test_tima_basic_counting(void) {
    uint8_t memory[65536] = {0};
    GBCpu cpu = make_test_cpu(memory);
    GBTimer timer;
    gb_timer_reset(&timer);

    gb_timer_write(&timer, &cpu, 0xFF07, 0x05); // enabled, clock select 01 = every 16 T-states
    gb_timer_step(&timer, &cpu, 16);
    check("TIMA: increments once after exactly one 16-dot period (clock select 01)",
          gb_timer_read(&timer, 0xFF05) == 1);

    gb_timer_step(&timer, &cpu, 16 * 9);
    check("TIMA: increments the right number of times over many periods",
          gb_timer_read(&timer, 0xFF05) == 10);
}

static void test_tima_disabled_does_not_count(void) {
    uint8_t memory[65536] = {0};
    GBCpu cpu = make_test_cpu(memory);
    GBTimer timer;
    gb_timer_reset(&timer);

    gb_timer_write(&timer, &cpu, 0xFF07, 0x01); // clock select 01, but enable bit clear
    gb_timer_step(&timer, &cpu, 1000);
    check("TIMA: TAC enable bit clear means TIMA never increments",
          gb_timer_read(&timer, 0xFF05) == 0);
    check("DIV: keeps counting regardless of TAC's enable bit",
          gb_timer_read(&timer, 0xFF04) != 0);
}

static void test_tima_overflow_delay(void) {
    uint8_t memory[65536] = {0};
    GBCpu cpu = make_test_cpu(memory);
    GBTimer timer;
    gb_timer_reset(&timer);

    gb_timer_write(&timer, &cpu, 0xFF06, 0x23); // TMA
    gb_timer_write(&timer, &cpu, 0xFF07, 0x05); // enabled, every 16 T-states
    gb_timer_write(&timer, &cpu, 0xFF05, 0xFF); // one tick away from overflow

    gb_timer_step(&timer, &cpu, 16); // the tick that overflows: FF -> 00
    check("TIMA overflow: reads $00 immediately (pandocs' Timer_Obscure_Behaviour.md)",
          gb_timer_read(&timer, 0xFF05) == 0x00);
    check("TIMA overflow: interrupt is NOT requested on the same T-state it overflows",
          (if_reg(&cpu) & 0x04) == 0);

    gb_timer_step(&timer, &cpu, 3); // 3 of the 4 T-states of the delay
    check("TIMA overflow: still $00 partway through the 1-M-cycle delay",
          gb_timer_read(&timer, 0xFF05) == 0x00);
    check("TIMA overflow: interrupt still not requested partway through the delay",
          (if_reg(&cpu) & 0x04) == 0);

    gb_timer_step(&timer, &cpu, 1); // the 4th and final T-state of the delay
    check("TIMA overflow: reloaded from TMA exactly one M-cycle later",
          gb_timer_read(&timer, 0xFF05) == 0x23);
    check("TIMA overflow: interrupt requested exactly when the reload happens",
          (if_reg(&cpu) & 0x04) != 0);
}

static void test_div_write_resets_and_can_spuriously_tick(void) {
    uint8_t memory[65536] = {0};
    GBCpu cpu = make_test_cpu(memory);
    GBTimer timer;
    gb_timer_reset(&timer);

    gb_timer_write(&timer, &cpu, 0xFF07, 0x05); // enabled, clock select 01 -> monitors bit 3
    gb_timer_step(&timer, &cpu, 8); // system counter = 8 = 0b1000, bit 3 set, no edge yet (TIMA still 0)
    check("DIV write setup: TIMA hasn't ticked yet", gb_timer_read(&timer, 0xFF05) == 0);

    gb_timer_write(&timer, &cpu, 0xFF04, 0xFF); // any value resets DIV (the whole system counter) to 0
    check("DIV write: resets the visible DIV register to 0", gb_timer_read(&timer, 0xFF04) == 0);
    check("DIV write: resetting a counter with the monitored bit set causes a spurious TIMA tick "
          "(pandocs' Timer_Obscure_Behaviour.md)",
          gb_timer_read(&timer, 0xFF05) == 1);
}

static void test_tac_write_spurious_tick_on_disable(void) {
    uint8_t memory[65536] = {0};
    GBCpu cpu = make_test_cpu(memory);
    GBTimer timer;
    gb_timer_reset(&timer);

    gb_timer_write(&timer, &cpu, 0xFF07, 0x05); // enabled, clock select 01 -> bit 3
    gb_timer_step(&timer, &cpu, 8); // system counter = 8, bit 3 set

    gb_timer_write(&timer, &cpu, 0xFF07, 0x00); // disable while the selected bit is set
    check("TAC write: disabling the timer while its monitored bit is set causes a spurious "
          "TIMA tick (DMG-specific quirk, pandocs' Timer_Obscure_Behaviour.md)",
          gb_timer_read(&timer, 0xFF05) == 1);
}

int main(void) {
    test_tima_basic_counting();
    test_tima_disabled_does_not_count();
    test_tima_overflow_delay();
    test_div_write_resets_and_can_spuriously_tick();
    test_tac_write_spurious_tick_on_disable();

    if (failures == 0) {
        printf("\nAll timer.c tests passed.\n");
        return 0;
    }
    printf("\n%d timer.c test(s) FAILED.\n", failures);
    return 1;
}
