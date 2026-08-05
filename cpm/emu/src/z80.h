#ifndef _Z80_H
#define _Z80_H

#include <stdint.h>

#define RAM_SIZE 65536

typedef struct {
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

    // Memory
    // 64 KB Memory Array
    uint8_t *memory;

    // I/O port space (256 ports). No real devices are attached; IN reads
    // back whatever the last OUT to that port wrote (initially 0), which
    // is enough to make IN/OUT round-trip observably rather than being a
    // silent no-op.
    uint8_t io_ports[256];

} Z80;


// Bus abstraction for zexall integration
uint8_t z80_read_byte(Z80 *cpu, uint16_t address);
void z80_write_byte(Z80 *cpu, uint16_t address, uint8_t value);

// I/O port bus abstraction
uint8_t z80_io_in(Z80 *cpu, uint8_t port);
void z80_io_out(Z80 *cpu, uint8_t port, uint8_t value);

typedef int (*Z80OpcodeHandler)(Z80 *cpu, uint8_t *ram);

void z80_init_tables(void);
int z80_step(Z80 *cpu, uint8_t *ram);

#endif
