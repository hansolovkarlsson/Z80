#ifndef _GB_CPU_H
#define _GB_CPU_H

#include <stdint.h>

// See docs/GAMEBOY_ROADMAP.md's "Architecture decision" section for why
// this is a standalone core rather than sharing code/structs with
// cpm/emu/src/z80.h - the two CPUs are related but not identical, and this
// struct reflects the SM83's own real register file: no IX/IY, no
// alternate register set (both are Z80-only), no I/O ports (the SM83 has
// none - all device access is memory-mapped).
typedef struct GBCpu {
    union { struct { uint8_t f, a; }; uint16_t af; };
    union { struct { uint8_t c, b; }; uint16_t bc; };
    union { struct { uint8_t e, d; }; uint16_t de; };
    union { struct { uint8_t l, h; }; uint16_t hl; };

    uint16_t sp;
    uint16_t pc;

    // Interrupt Master Enable - set/cleared by EI/DI/RETI, and by the
    // interrupt dispatch logic itself. EI's real hardware behavior
    // delays the actual enable by one instruction (see pandocs'
    // Interrupts page) - ime_pending/ei_delay implement that.
    uint8_t ime;
    uint8_t ime_pending; // EI was just executed; take effect after the *next* instruction

    // HALT: true once a HALT instruction has been executed. The run loop
    // is expected to stop advancing PC (just burn cycles) while this is
    // set, until an interrupt becomes pending. STOP is similar but also
    // real hardware requires a subsequent input/reset to leave it (not
    // modeled yet - no interrupt controller/joypad exists until Phase 4).
    uint8_t halted;
    uint8_t stopped;

    // The "HALT bug": a real hardware quirk where HALT executed with
    // IME=0 and a pending interrupt already latched (IE & IF != 0)
    // fails to advance PC afterward, causing the next opcode byte to be
    // fetched (and executed) twice. Implemented as of Phase 4 - see
    // cpu.c's gb_cpu_step() and pandocs' halt.md (fetched during
    // Phase 1, when this field was first added but left unused).
    uint8_t halt_bug;

    // VRAM/WRAM/OAM/I-O-registers/HRAM only as of Phase 2 - 0x0000-0x7FFF
    // (ROM) and 0xA000-0xBFFF (external cartridge RAM) are no longer part
    // of this flat array, routed through `cart` instead. See mmu.c.
    uint8_t *memory;

    // Forward-declared rather than #include "cart.h" here - cpu.h
    // shouldn't need to know GBCart's internals, only that mmu.c can
    // reach one through a GBCpu. NULL is valid (Phase 1's old flat-ROM
    // behavior no longer applies once this is wired up in main.c, but
    // the field itself doesn't require a cart to exist for the struct
    // to be well-formed).
    struct GBCart *cart;

    // Same forward-declaration reasoning as `cart` above, added Phase 3.
    struct GBPpu *ppu;

    // Added Phase 4. `timer` is reached directly (not just through
    // mmu.c's routing) because the STOP instruction's handler needs to
    // reset it exactly as a DIV write would - see cpu.c's gb_op_stop().
    struct GBTimer *timer;
    struct GBJoypad *joypad;

    // Added Phase 5 - reached only through mmu.c's routing (the full
    // 0xFF10-0xFF3F span), unlike timer above.
    struct GBApu *apu;
} GBCpu;

uint8_t gb_read_byte(GBCpu *cpu, uint16_t addr);
void gb_write_byte(GBCpu *cpu, uint16_t addr, uint8_t val);

// Unlike Z80OpcodeHandler (cpm/emu/src/z80.h), no separate ram parameter -
// GBCpu carries its own memory pointer and every handler goes through
// gb_read_byte/gb_write_byte, so there's nothing a second parameter
// would add.
typedef int (*GBOpcodeHandler)(GBCpu *cpu);

// Returns the number of T-cycles (4.194304 MHz ticks - not the
// "M-cycles" = T-cycles/4 some references count in) the executed
// instruction took, or a negative value for a genuinely unimplemented
// opcode (the 11 official gaps in the unprefixed table, or a dispatch
// bug), mirroring z80_step()'s own convention in cpm/emu/src/z80.h.
int gb_cpu_step(GBCpu *cpu);

void gb_cpu_init_tables(void);
void gb_cpu_reset(GBCpu *cpu);

#endif
