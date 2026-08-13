#ifndef _Z80_H
#define _Z80_H

#include <stdint.h>

#define RAM_SIZE 65536

typedef struct Z80 {
    // Register pairs using unions for easy 8-bit / 16-bit access
    union { struct { uint8_t f, a; }; uint16_t af; };
    union { struct { uint8_t c, b; }; uint16_t bc; };
    union { struct { uint8_t e, d; }; uint16_t de; };
    union { struct { uint8_t l, h; }; uint16_t hl; };

    // Alternate register set
    union { struct { uint8_t f_alt, a_alt; }; uint16_t af_alt; };
    union { struct { uint8_t c_alt, b_alt; }; uint16_t bc_alt; };
    union { struct { uint8_t e_alt, d_alt; }; uint16_t de_alt; };
    union { struct { uint8_t l_alt, h_alt; }; uint16_t hl_alt; };

    // 16-bit Index and Pointer registers
    uint16_t ix;
    uint16_t iy;
    uint16_t sp;
    uint16_t pc;

    // Special registers
    uint8_t i;
    uint8_t r;

    // Interrupt states
    uint8_t iff1, iff2;
    uint8_t im; // Interrupt Mode 0, 1, or 2

    // Set by the EI opcode handler; consumed (cleared) by the very next
    // z80_step() call, which skips interrupt sampling for that one step
    // regardless of iff1/int_pending/nmi_pending. Real hardware: "When
    // an EI instruction is executed, any pending interrupt request is
    // not accepted until after the instruction following EI is
    // executed" (Zilog Z80 CPU User Manual, "Interrupt Enable/Disable") -
    // documented there for the maskable INT specifically, extended here
    // to NMI too (the manual is silent on that combination, but every
    // serious Z80 reference/emulator this project checked treats the
    // one-instruction inhibit as gating the single internal interrupt-
    // sample latch, not something conditioned on IFF, so it blocks NMI
    // sampling identically).
    uint8_t ei_delay;

    // Maskable interrupt request line. Level-triggered on real hardware
    // (the device holds INT asserted until acknowledged) - modeled here
    // as "stays set until the CPU accepts it or the caller withdraws it
    // via z80_clear_int()", the closest equivalent without a real device
    // driving an actual electrical line (no interrupt-raising hardware
    // is attached yet - see cpm/docs/ROADMAP.md's Known Gaps). int_data
    // is the byte the interrupting device would place on the data bus:
    // ignored in Mode 1, the low 7 bits of the Mode 2 vector-table
    // address (bit 0 is always forced to 0 by real hardware - addresses
    // in the table must be word-aligned), or the actual instruction
    // opcode to execute in Mode 0 - see z80_request_int()'s own comment
    // for this implementation's Mode 0 scope.
    uint8_t int_pending;
    uint8_t int_data;

    // Non-maskable interrupt line. Edge-triggered and latched on real
    // hardware (a single pulse is remembered until serviced, unlike
    // INT's level) - modeled directly as such: z80_request_nmi() sets
    // this, and it's cleared only once actually accepted. Never gated
    // by iff1 (that's what "non-maskable" means) - only by ei_delay
    // above.
    uint8_t nmi_pending;

    // Memory
    // 64 KB Memory Array
    uint8_t *memory;

    // I/O port space (256 ports). No real devices are attached; IN reads
    // back whatever the last OUT to that port wrote (initially 0), which
    // is enough to make IN/OUT round-trip observably rather than being a
    // silent no-op.
    uint8_t io_ports[256];

    // Optional per-machine memory-read override, checked by z80_read_byte()
    // after it reads the flat array. NULL (its zero-initialized default) on
    // every existing caller, so behavior is identical to a plain flat-array
    // read unless a machine target explicitly opts in - added for the ABC80
    // target (abc80/emu/src/main.c), whose ABCbus-delegated address range
    // (0x4000-0xBFFF minus video RAM) must read back a fixed floating-bus
    // value when no expansion card is modeled there, regardless of what was
    // last written - see abc80/docs/ABC80_ROADMAP.md's Milestone 6 section.
    // Writes are deliberately left unintercepted: a real unpopulated bus
    // silently discards writes too, but since this hook already forces
    // every *read* back to a fixed value regardless of the array's actual
    // contents, letting a write land in the underlying array anyway is
    // harmless (nothing ever reads it back correctly) and avoids a second,
    // symmetric write-hook this project has no actual use for yet.
    uint8_t (*bus_read_hook)(struct Z80 *cpu, uint16_t address, uint8_t stored_value);

} Z80;


// Bus abstraction for zexall integration
uint8_t z80_read_byte(Z80 *cpu, uint16_t address);
void z80_write_byte(Z80 *cpu, uint16_t address, uint8_t value);

// I/O port bus abstraction
uint8_t z80_io_in(Z80 *cpu, uint8_t port);
void z80_io_out(Z80 *cpu, uint8_t port, uint8_t value);

typedef int (*Z80OpcodeHandler)(Z80 *cpu, uint8_t *ram);

void z80_init_tables(void);

// The machine-agnostic instruction-execution core (interrupt sampling,
// opcode fetch/dispatch) - no CP/M awareness. CP/M's own z80_step()
// (cpm.h/cpm.c) wraps this with BDOS/BIOS call interception; a non-CP/M
// machine target calls z80_execute() directly instead - see its own
// comment in z80.c for why that split exists.
int z80_execute(Z80 *cpu, uint8_t *ram);

// Interrupt request API - the host side's equivalent of asserting a
// real INT/NMI line. No real device calls these yet (see z80.h's own
// int_pending/nmi_pending comments); presently only exercised by
// cpm/tests/test_interrupts.c's direct unit tests, which is also why
// this needs to be a real, callable API rather than private state -
// there's no CP/M-executable instruction that can raise an interrupt
// against itself, so a host-side caller (a test, or a future real
// device model) is the only way to drive this at all.
void z80_request_int(Z80 *cpu, uint8_t data);
void z80_clear_int(Z80 *cpu); // withdraw INT before it's accepted, mirroring a device deasserting its line
void z80_request_nmi(Z80 *cpu);

#endif
