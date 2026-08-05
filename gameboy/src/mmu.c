#include "mmu.h"
#include "cart.h"
#include "ppu.h"
#include "timer.h"
#include "joypad.h"
#include <stddef.h>

void (*gb_serial_output_hook)(uint8_t byte) = NULL;

// Echo RAM (0xE000-0xFDFF) is a real hardware mirror of WRAM's first
// 0x1E00 bytes (0xC000-0xDDFF), not a separate region - both addresses
// must observe the same underlying byte. Modeled here by redirecting
// reads/writes in the echo range onto the real WRAM storage rather than
// giving echo RAM its own backing bytes, so writes through either
// address are visible through both without needing to keep two copies
// in sync. Never touches the cartridge-routed ranges below (0x0000-
// 0x7FFF, 0xA000-0xBFFF are both under 0xE000), so no interaction there.
static uint16_t redirect_echo(uint16_t addr) {
    if (addr >= 0xE000 && addr <= 0xFDFF) return (uint16_t)(addr - 0x2000);
    return addr;
}

uint8_t gb_read_byte(GBCpu *cpu, uint16_t addr) {
    if (addr < 0x8000) return gb_cart_read(cpu->cart, addr);
    if (addr >= 0xA000 && addr < 0xC000) return gb_cart_read_ram(cpu->cart, addr);
    if (addr == 0xFF00) return gb_joypad_read(cpu->joypad);
    if (addr >= 0xFF04 && addr <= 0xFF07) return gb_timer_read(cpu->timer, addr);
    if (addr >= 0xFF40 && addr <= 0xFF4B) return gb_ppu_read_reg(cpu->ppu, addr);

    addr = redirect_echo(addr);
    if (addr >= 0xFEA0 && addr <= 0xFEFF) {
        // "Not usable" - real hardware's behavior here depends on PPU
        // state and hardware revision (see pandocs); this fixed 0xFF is
        // a placeholder, not a grounded model of the real quirk.
        return 0xFF;
    }
    return cpu->memory[addr];
}

void gb_write_byte(GBCpu *cpu, uint16_t addr, uint8_t val) {
    if (addr < 0x8000) { gb_cart_write_ctrl(cpu->cart, addr, val); return; }
    if (addr >= 0xA000 && addr < 0xC000) { gb_cart_write_ram(cpu->cart, addr, val); return; }
    if (addr == 0xFF00) { gb_joypad_write(cpu->joypad, val); return; }
    if (addr >= 0xFF04 && addr <= 0xFF07) { gb_timer_write(cpu->timer, cpu, addr, val); return; }
    if (addr >= 0xFF40 && addr <= 0xFF4B) { gb_ppu_write_reg(cpu->ppu, cpu, addr, val); return; }

    addr = redirect_echo(addr);
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
