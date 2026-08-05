#include "mmu.h"
#include <stddef.h>

void (*gb_serial_output_hook)(uint8_t byte) = NULL;

// Echo RAM (0xE000-0xFDFF) is a real hardware mirror of WRAM's first
// 0x1E00 bytes (0xC000-0xDDFF), not a separate region - both addresses
// must observe the same underlying byte. Modeled here by redirecting
// reads/writes in the echo range onto the real WRAM storage rather than
// giving echo RAM its own backing bytes, so writes through either
// address are visible through both without needing to keep two copies
// in sync.
static uint16_t redirect_echo(uint16_t addr) {
    if (addr >= 0xE000 && addr <= 0xFDFF) return (uint16_t)(addr - 0x2000);
    return addr;
}

uint8_t gb_read_byte(GBCpu *cpu, uint16_t addr) {
    addr = redirect_echo(addr);
    if (addr >= 0xFEA0 && addr <= 0xFEFF) {
        // "Not usable" - real hardware's behavior here depends on PPU
        // state and hardware revision (see pandocs); no PPU exists yet
        // in this phase, so this fixed 0xFF is a placeholder, not a
        // grounded model of the real quirk. Revisit alongside Phase 3.
        return 0xFF;
    }
    return cpu->memory[addr];
}

void gb_write_byte(GBCpu *cpu, uint16_t addr, uint8_t val) {
    addr = redirect_echo(addr);
    if (addr < 0x8000) {
        // ROM area. Real hardware routes writes here to MBC bank-switch
        // logic - Phase 2's job. A flat, unbanked ROM (everything this
        // phase's test ROMs are) never relies on this, so silently
        // discarding is correct for now, not a shortcut being taken.
        return;
    }
    if (addr >= 0xFEA0 && addr <= 0xFEFF) {
        return; // "not usable" - see the read-side comment above
    }
    if (addr == 0xFF02 && (val & 0x81) == 0x81) {
        // Serial transfer start, internal clock: Blargg's test ROMs use
        // exactly this to emit one output character via SB (0xFF01)
        // without a real link-cable peer attached - see mmu.h.
        if (gb_serial_output_hook) gb_serial_output_hook(cpu->memory[0xFF01]);
    }
    cpu->memory[addr] = val;
}
