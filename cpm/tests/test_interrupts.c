#include <stdio.h>
#include <string.h>
#include "../emu/src/z80.h"
#include "../emu/src/cpm.h" // z80_step()'s declaration now lives here (cpm.c) - see its own comment

// Direct unit tests for Z80 interrupt acceptance (z80_step()'s new
// interrupt-check, z80_service_int()/z80_service_nmi() in z80.c) -
// grounded against the Zilog Z80 CPU User Manual (UM008011-0816),
// "Interrupt Response" section. No CP/M-executable instruction can
// raise an interrupt against itself (INT/NMI are host-side signals on
// real hardware), so unlike every other regression check in this
// project (a real .asm program run through cpm/tests/run_tests.sh),
// this has to drive z80_request_int()/z80_request_nmi() directly from
// C - the same reasoning gameboy/tests/test_cpu.c used for its own
// EI/HALT interrupt-dispatch tests before that project's own split
// into its own repo (see docs/GAMEBOY_ROADMAP.md there).
//
// z80_step() needs check_cpm_bdos()/check_cpm_bios() linked (cpm.c) -
// both are pure functions of (cpu, ram) that early-return doing nothing
// when PC isn't one of a few fixed low/high addresses, so as long as
// every test here keeps PC away from 0x0000/0x0005/BDOS_ENTRY(0xF200)/
// BIOS_BASE(0xFC00), linking cpm.c is safe with no init call needed.

static int failures = 0;

static void check(const char *name, int condition) {
    if (condition) {
        printf("OK   %s\n", name);
    } else {
        printf("FAIL %s\n", name);
        failures++;
    }
}

static void reset(Z80 *cpu, uint8_t *ram) {
    memset(cpu, 0, sizeof(*cpu));
    memset(ram, 0, 65536);
    cpu->memory = ram;
    cpu->pc = 0x8000;
    cpu->sp = 0x9000;
}

int main(void) {
    static uint8_t ram[65536];
    Z80 cpu;
    z80_init_tables();

    // --- IM 1: fixed RST-38h-shaped vector ---
    reset(&cpu, ram);
    cpu.iff1 = 1;
    cpu.im = 1;
    ram[0x8000] = 0x00; // NOP - never actually fetched if the interrupt is accepted first
    z80_request_int(&cpu, 0x00); // data is ignored in Mode 1
    int cycles = z80_step(&cpu, ram);
    check("IM1: takes 13 T-states (Zilog manual: RST's 11 + 2 ack wait states)", cycles == 13);
    check("IM1: PC jumps to the fixed 0x0038 vector", cpu.pc == 0x0038);
    check("IM1: return address pushed correctly", z80_read_byte(&cpu, cpu.sp) == 0x00 && z80_read_byte(&cpu, (uint16_t)(cpu.sp + 1)) == 0x80);
    check("IM1: accepting resets both IFF1 and IFF2", cpu.iff1 == 0 && cpu.iff2 == 0);
    check("IM1: int_pending consumed", cpu.int_pending == 0);

    // --- IM 2: vector table lookup, low bit of the device byte forced to 0 ---
    reset(&cpu, ram);
    cpu.iff1 = 1;
    cpu.im = 2;
    cpu.i = 0x40;
    ram[0x4034] = 0xCD; // table entry for vector byte 0x35 (bit 0 forced off -> 0x34) - low byte
    ram[0x4035] = 0xAB; // high byte -> target 0xABCD
    z80_request_int(&cpu, 0x35);
    cycles = z80_step(&cpu, ram);
    check("IM2: takes 19 T-states (Zilog manual: 7+6+6)", cycles == 19);
    check("IM2: forces the device byte's low bit to 0 before the table lookup", cpu.pc == 0xABCD);
    check("IM2: accepting resets both IFF1 and IFF2", cpu.iff1 == 0 && cpu.iff2 == 0);

    // --- IM 0: single-byte RST vector (this implementation's supported case) ---
    reset(&cpu, ram);
    cpu.iff1 = 1;
    cpu.im = 0;
    z80_request_int(&cpu, 0xCF); // RST 08h
    cycles = z80_step(&cpu, ram);
    check("IM0 RST: takes 13 T-states (Zilog manual: RST's own 11 + 2 ack wait states)", cycles == 13);
    check("IM0 RST: PC jumps to the RST opcode's own page-zero address", cpu.pc == 0x0008);

    // --- IM 0: unsupported (non-RST) device vector - documented gap, not a silent wrong answer ---
    reset(&cpu, ram);
    cpu.iff1 = 1;
    cpu.im = 0;
    z80_request_int(&cpu, 0x00); // NOP - not a bare RST, unsupported by this implementation
    cycles = z80_step(&cpu, ram);
    check("IM0 non-RST: reports unimplemented (-1) rather than silently misbehaving", cycles == -1);

    // --- NMI: fixed vector, only IFF1 cleared, IFF2 preserved for RETN ---
    reset(&cpu, ram);
    cpu.iff1 = 1;
    cpu.iff2 = 1;
    ram[0x0066] = 0xED; ram[0x0067] = 0x45; // RETN, so this test can also confirm the round trip below
    z80_request_nmi(&cpu);
    cycles = z80_step(&cpu, ram);
    check("NMI: takes 11 T-states", cycles == 11);
    check("NMI: PC jumps to the fixed 0x0066 vector", cpu.pc == 0x0066);
    check("NMI: clears IFF1 only, leaves IFF2 untouched", cpu.iff1 == 0 && cpu.iff2 == 1);
    check("NMI: nmi_pending consumed", cpu.nmi_pending == 0);

    cycles = z80_step(&cpu, ram); // RETN
    check("RETN: pops PC back to the interrupted address", cpu.pc == 0x8000);
    check("RETN: restores IFF1 from IFF2", cpu.iff1 == 1);

    // --- NMI is truly non-maskable: accepted even with IFF1=0 ---
    reset(&cpu, ram);
    cpu.iff1 = 0;
    cpu.iff2 = 0;
    z80_request_nmi(&cpu);
    z80_step(&cpu, ram);
    check("NMI: accepted even though IFF1 was 0 (non-maskable)", cpu.pc == 0x0066);

    // --- NMI takes priority over a simultaneously-pending INT ---
    reset(&cpu, ram);
    cpu.iff1 = 1;
    cpu.im = 1;
    z80_request_int(&cpu, 0x00);
    z80_request_nmi(&cpu);
    z80_step(&cpu, ram);
    check("NMI beats a simultaneously-pending INT (Zilog manual, HALT Exit)", cpu.pc == 0x0066);
    check("the still-pending INT isn't silently dropped - real hardware would keep the device's INT line asserted", cpu.int_pending == 1);

    // --- EI's one-instruction delay defers both INT and NMI ---
    reset(&cpu, ram);
    cpu.im = 1;
    ram[0x8000] = 0xFB; // EI
    ram[0x8001] = 0x00; // NOP - "the instruction following EI"
    ram[0x8002] = 0x00; // NOP
    z80_step(&cpu, ram); // EI itself
    z80_request_int(&cpu, 0x00);
    cycles = z80_step(&cpu, ram); // the instruction right after EI
    check("EI-delay: INT requested right after EI is NOT accepted for that next instruction", cpu.pc == 0x8002 && cycles == 4);
    cycles = z80_step(&cpu, ram); // the instruction after that
    check("EI-delay: the deferred INT is accepted on the following step", cpu.pc == 0x0038 && cycles == 13);

    reset(&cpu, ram);
    ram[0x8000] = 0xFB; // EI
    ram[0x8001] = 0x00; // NOP
    z80_step(&cpu, ram); // EI
    z80_request_nmi(&cpu);
    cycles = z80_step(&cpu, ram);
    check("EI-delay: NMI requested right after EI is also deferred one instruction", cpu.pc == 0x8002 && cycles == 4);
    z80_step(&cpu, ram);
    check("EI-delay: the deferred NMI is accepted on the following step", cpu.pc == 0x0066);

    // --- HALT: interrupts break the spin loop; masked INT does not ---
    reset(&cpu, ram);
    cpu.iff1 = 0;
    cpu.im = 1;
    ram[0x8000] = 0x76; // HALT
    z80_request_int(&cpu, 0x00);
    for (int i = 0; i < 5; i++) cycles = z80_step(&cpu, ram);
    check("HALT: a masked INT (IFF1=0) does not break the spin", cpu.pc == 0x8000 && cycles == 4);

    cpu.iff1 = 1;
    cycles = z80_step(&cpu, ram);
    check("HALT: an unmasked INT breaks the spin and dispatches normally", cpu.pc == 0x0038 && cycles == 13);
    check("HALT: the pushed return address is the HALT instruction's own address", z80_read_byte(&cpu, cpu.sp) == 0x00 && z80_read_byte(&cpu, (uint16_t)(cpu.sp + 1)) == 0x80);

    // --- NMI also breaks HALT, unconditionally ---
    reset(&cpu, ram);
    cpu.iff1 = 0;
    ram[0x8000] = 0x76; // HALT
    z80_step(&cpu, ram);
    z80_request_nmi(&cpu);
    cycles = z80_step(&cpu, ram);
    check("HALT: NMI breaks the spin even with IFF1=0", cpu.pc == 0x0066 && cycles == 11);

    // --- z80_clear_int() withdraws a request before it's sampled ---
    reset(&cpu, ram);
    cpu.iff1 = 1;
    cpu.im = 1;
    ram[0x8000] = 0x00; // NOP
    z80_request_int(&cpu, 0x00);
    z80_clear_int(&cpu);
    cycles = z80_step(&cpu, ram);
    check("z80_clear_int(): a withdrawn INT is not accepted", cpu.pc == 0x8001 && cycles == 4);

    printf("\n%s: %d failure(s)\n", failures == 0 ? "PASS" : "FAIL", failures);
    return failures == 0 ? 0 : 1;
}
