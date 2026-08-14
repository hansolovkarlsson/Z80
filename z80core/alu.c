
#include <stdbool.h>
#include "alu.h"


// Returns 1 if the number of set bits in 'val' is even, else 0
inline uint8_t calculate_parity(uint8_t val) {
    uint8_t bits = 0;
    for (int i = 0; i < 8; i++) {
        if (val & (1 << i)) bits++;
    }
    return (bits % 2 == 0) ? FLAG_PV : 0;
}

// ADD A, val
void z80_alu_add(Z80 *cpu, uint8_t val) {
    uint16_t res = (uint16_t)cpu->a + val;
    uint8_t result8 = (uint8_t)res;

    // Calculate Flags
    uint8_t flags = 0;
    if (result8 & 0x80) flags |= FLAG_S;                     // Sign
    if (result8 == 0)   flags |= FLAG_Z;                     // Zero
    if (((cpu->a & 0x0F) + (val & 0x0F)) > 0x0F) flags |= FLAG_H; // Half-Carry
    
    // Overflow: set if operands have same sign, but result has different sign
    if (~(cpu->a ^ val) & (cpu->a ^ result8) & 0x80) flags |= FLAG_PV; 
    
    if (res > 0xFF)     flags |= FLAG_C;                     // Carry
    
    // Copy bits 3 and 5 of result into undocumented flags
    flags |= (result8 & (FLAG_X | FLAG_Y)); 

    cpu->a = result8;
    cpu->f = flags;
}

// ADC A, val (Add with Carry)
void z80_alu_adc(Z80 *cpu, uint8_t val) {
    uint8_t carry = (cpu->f & FLAG_C) ? 1 : 0;
    uint16_t res = (uint16_t)cpu->a + val + carry;
    uint8_t result8 = (uint8_t)res;

    uint8_t flags = 0;
    if (result8 & 0x80) flags |= FLAG_S;
    if (result8 == 0)   flags |= FLAG_Z;
    if (((cpu->a & 0x0F) + (val & 0x0F) + carry) > 0x0F) flags |= FLAG_H;
    if (~(cpu->a ^ val) & (cpu->a ^ result8) & 0x80) flags |= FLAG_PV;
    if (res > 0xFF)     flags |= FLAG_C;
    
    flags |= (result8 & (FLAG_X | FLAG_Y));

    cpu->a = result8;
    cpu->f = flags;
}

// SUB A, val
void z80_alu_sub(Z80 *cpu, uint8_t val) {
    uint16_t res = (uint16_t)cpu->a - val;
    uint8_t result8 = (uint8_t)res;

    uint8_t flags = FLAG_N; // Subtraction flag set
    if (result8 & 0x80) flags |= FLAG_S;
    if (result8 == 0)   flags |= FLAG_Z;
    if ((cpu->a & 0x0F) < (val & 0x0F)) flags |= FLAG_H;
    
    // Overflow: set if operands have different signs, and result sign differs from accumulator
    if ((cpu->a ^ val) & (cpu->a ^ result8) & 0x80) flags |= FLAG_PV;
    if (cpu->a < val)   flags |= FLAG_C;
    
    flags |= (result8 & (FLAG_X | FLAG_Y));

    cpu->a = result8;
    cpu->f = flags;
}

// SBC A, val (Subtract with Carry)
void z80_alu_sbc(Z80 *cpu, uint8_t val) {
    uint8_t carry = (cpu->f & FLAG_C) ? 1 : 0;
    uint16_t res = (uint16_t)cpu->a - val - carry;
    uint8_t result8 = (uint8_t)res;

    uint8_t flags = FLAG_N;
    if (result8 & 0x80) flags |= FLAG_S;
    if (result8 == 0)   flags |= FLAG_Z;
    if ((cpu->a & 0x0F) < ((val & 0x0F) + carry)) flags |= FLAG_H;
    if ((cpu->a ^ val) & (cpu->a ^ result8) & 0x80) flags |= FLAG_PV;
    if (cpu->a < (val + carry)) flags |= FLAG_C;
    
    flags |= (result8 & (FLAG_X | FLAG_Y));

    cpu->a = result8;
    cpu->f = flags;
}

// CP val (Compare A with val — performs SUB but discards the result, keeping flags)
void z80_alu_cp(Z80 *cpu, uint8_t val) {
    uint8_t orig_a = cpu->a;
    z80_alu_sub(cpu, val);
    
    // CP sets bits X and Y from the operand 'val' instead of the result!
    cpu->f = (cpu->f & ~(FLAG_X | FLAG_Y)) | (val & (FLAG_X | FLAG_Y));
    cpu->a = orig_a; // Restore original Accumulator value
}

// AND val
void z80_alu_and(Z80 *cpu, uint8_t val) {
    cpu->a &= val;
    
    uint8_t flags = FLAG_H; // AND always sets Half-Carry to 1
    if (cpu->a & 0x80) flags |= FLAG_S;
    if (cpu->a == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(cpu->a);
    flags |= (cpu->a & (FLAG_X | FLAG_Y)); // Copy bits 3 and 5

    cpu->f = flags;
}

// XOR val
void z80_alu_xor(Z80 *cpu, uint8_t val) {
    cpu->a ^= val;

    uint8_t flags = 0; // H and C are cleared
    if (cpu->a & 0x80) flags |= FLAG_S;
    if (cpu->a == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(cpu->a);
    flags |= (cpu->a & (FLAG_X | FLAG_Y));

    cpu->f = flags;
}

// OR val
void z80_alu_or(Z80 *cpu, uint8_t val) {
    cpu->a |= val;

    uint8_t flags = 0; // H and C are cleared
    if (cpu->a & 0x80) flags |= FLAG_S;
    if (cpu->a == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(cpu->a);
    flags |= (cpu->a & (FLAG_X | FLAG_Y));

    cpu->f = flags;
}

/*
 * 8-bit INC / DEC and 16-bit ADD / INC / DEC
 */

// INC r (8-bit Increment)
uint8_t z80_alu_inc(Z80 *cpu, uint8_t val) {
    uint8_t res = val + 1;

    // Retain original Carry flag, clear N flag
    uint8_t flags = cpu->f & FLAG_C;

    if (res & 0x80) flags |= FLAG_S;                     // Sign
    if (res == 0)   flags |= FLAG_Z;                     // Zero
    if ((val & 0x0F) == 0x0F) flags |= FLAG_H;           // Half-Carry (carry from bit 3 to 4)
    if (val == 0x7F) flags |= FLAG_PV;                   // Overflow (0x7F -> 0x80)
    flags |= (res & (FLAG_X | FLAG_Y));                 // Undocumented X/Y bits

    cpu->f = flags;
    return res;
}

// DEC r (8-bit Decrement)
uint8_t z80_alu_dec(Z80 *cpu, uint8_t val) {
    uint8_t res = val - 1;

    // Retain original Carry flag, set N flag
    uint8_t flags = (cpu->f & FLAG_C) | FLAG_N;

    if (res & 0x80) flags |= FLAG_S;                     // Sign
    if (res == 0)   flags |= FLAG_Z;                     // Zero
    if ((val & 0x0F) == 0x00) flags |= FLAG_H;           // Half-Carry (borrow from bit 4)
    if (val == 0x80) flags |= FLAG_PV;                   // Overflow (0x80 -> 0x7F)
    flags |= (res & (FLAG_X | FLAG_Y));                 // Undocumented X/Y bits

    cpu->f = flags;
    return res;
}


// ADD HL, val (16-bit Addition)
void z80_alu_add16(Z80 *cpu, uint16_t val) {
    uint32_t res = (uint32_t)cpu->hl + val;
    uint16_t res16 = (uint16_t)res;

    // Retain S, Z, P/V flags; clear N
    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_PV);

    // Half-Carry calculation for bit 11 -> bit 12
    if (((cpu->hl & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF) {
        flags |= FLAG_H;
    }

    if (res > 0xFFFF) flags |= FLAG_C;

    // Undocumented bits copied from the high byte of the result
    flags |= ((res16 >> 8) & (FLAG_X | FLAG_Y));

    cpu->hl = res16;
    cpu->f = flags;
}

/*
 * 	0xCB Prefix Table Setup (for BIT, SET, RES, RLC, RRC, etc.)
 */

 // BIT b, val
void z80_alu_bit(Z80 *cpu, uint8_t bit, uint8_t val) {
    uint8_t bit_mask = (1 << bit);
    
    // Keep Carry flag, clear N, set H
    uint8_t flags = (cpu->f & FLAG_C) | FLAG_H;

    if ((val & bit_mask) == 0) {
        flags |= FLAG_Z | FLAG_PV; // Parity/Overflow set when Z is set in BIT
    }
    
    if (val & 0x80 && bit == 7) flags |= FLAG_S; // Sign bit tested
    
    // Copy bits 3 and 5 from operand
    flags |= (val & (FLAG_X | FLAG_Y));

    cpu->f = flags;
}

// SET b, val
inline uint8_t z80_alu_set(uint8_t bit, uint8_t val) {
    return val | (1 << bit);
}

// RES b, val
inline uint8_t z80_alu_res(uint8_t bit, uint8_t val) {
    return val & ~(1 << bit);
}

// RLC r (Rotate Left Circular)
uint8_t z80_alu_rlc(Z80 *cpu, uint8_t val) {
    uint8_t carry = (val & 0x80) >> 7;
    uint8_t res = (val << 1) | carry;

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (carry)      flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

// RRC r (Rotate Right Circular)
uint8_t z80_alu_rrc(Z80 *cpu, uint8_t val) {
    uint8_t carry = val & 0x01;
    uint8_t res = (val >> 1) | (carry << 7);

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (carry)      flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

// RL r (Rotate Left through Carry)
uint8_t z80_alu_rl(Z80 *cpu, uint8_t val) {
    uint8_t old_carry = (cpu->f & FLAG_C) ? 1 : 0;
    uint8_t new_carry = (val & 0x80) >> 7;
    uint8_t res = (val << 1) | old_carry;

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (new_carry)  flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

// RR r (Rotate Right through Carry)
uint8_t z80_alu_rr(Z80 *cpu, uint8_t val) {
    uint8_t old_carry = (cpu->f & FLAG_C) ? 0x80 : 0x00;
    uint8_t new_carry = val & 0x01;
    uint8_t res = (val >> 1) | old_carry;

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (new_carry)  flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

// SLA r (Shift Left Arithmetic - fills bit 0 with 0)
uint8_t z80_alu_sla(Z80 *cpu, uint8_t val) {
    uint8_t carry = (val & 0x80) >> 7;
    uint8_t res = val << 1;

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (carry)      flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

// SLL r (undocumented Shift Left Logical - fills bit 0 with 1, unlike SLA)
uint8_t z80_alu_sll(Z80 *cpu, uint8_t val) {
    uint8_t carry = (val & 0x80) >> 7;
    uint8_t res = (uint8_t)((val << 1) | 0x01);

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (carry)      flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

// SRA r (Shift Right Arithmetic - preserves bit 7 sign)
uint8_t z80_alu_sra(Z80 *cpu, uint8_t val) {
    uint8_t carry = val & 0x01;
    uint8_t res = (val >> 1) | (val & 0x80);

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (carry)      flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

// SRL r (Shift Right Logical - fills bit 7 with 0)
uint8_t z80_alu_srl(Z80 *cpu, uint8_t val) {
    uint8_t carry = val & 0x01;
    uint8_t res = val >> 1;

    uint8_t flags = 0;
    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    flags |= calculate_parity(res);
    if (carry)      flags |= FLAG_C;
    flags |= (res & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return res;
}

/*
 * Block Transfers
 */

 // LDI / LDD helper
// increment: +1 for LDI/LDIR, -1 for LDD/LDDR
int z80_alu_block_transfer(Z80 *cpu, int increment, bool repeat) {
    uint8_t byte = z80_read_byte(cpu, cpu->hl);
    z80_write_byte(cpu, cpu->de, byte);

    cpu->hl += increment;
    cpu->de += increment;
    cpu->bc--;

    // Keep S, Z, C; clear H and N
    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_C);

    // P/V set if BC != 0
    if (cpu->bc != 0) {
        flags |= FLAG_PV;
    }

    // Undocumented bits X/Y are derived from (byte + A)
    uint8_t n = byte + cpu->a;
    if (n & 0x08) flags |= FLAG_X;
    if (n & 0x02) flags |= FLAG_Y;

    cpu->f = flags;

    // Repetition check
    if (repeat && cpu->bc != 0) {
        cpu->pc -= 2; // Rewind PC back to 0xED opcode to loop
        return 21;    // T-states when repeating
    }

    return 16;        // T-states when finished or non-repeating
}

// CPI / CPD helper
// increment: +1 for CPI/CPIR, -1 for CPD/CPDR
int z80_alu_block_search(Z80 *cpu, int increment, bool repeat) {
    uint8_t val = z80_read_byte(cpu, cpu->hl);
    uint8_t res = cpu->a - val;

    cpu->hl += increment;
    cpu->bc--;

    // Retain Carry flag, set N
    uint8_t flags = (cpu->f & FLAG_C) | FLAG_N;

    if (res & 0x80) flags |= FLAG_S;
    if (res == 0)   flags |= FLAG_Z;
    if ((cpu->a & 0x0F) < (val & 0x0F)) flags |= FLAG_H;
    if (cpu->bc != 0) flags |= FLAG_PV;

    // Undocumented bits X/Y logic for CP block ops
    uint8_t val_h = res - ((flags & FLAG_H) ? 1 : 0);
    if (val_h & 0x08) flags |= FLAG_X;
    if (val_h & 0x02) flags |= FLAG_Y;

    cpu->f = flags;

    // Repeat while BC != 0 AND A != (HL) (i.e., Zero flag is not set)
    if (repeat && cpu->bc != 0 && !(flags & FLAG_Z)) {
        cpu->pc -= 2; // Rewind PC back to 0xED opcode
        return 21;
    }

    return 16;
}

/*
 * IX/IY
 */

void z80_alu_add16_idx(Z80 *cpu, uint16_t *index_reg, uint16_t val) {
    uint32_t res = (uint32_t)*index_reg + val;
    uint16_t res16 = (uint16_t)res;

    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_PV);

    if (((*index_reg & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF) {
        flags |= FLAG_H;
    }
    if (res > 0xFFFF) flags |= FLAG_C;

    flags |= ((res16 >> 8) & (FLAG_X | FLAG_Y));

    *index_reg = res16;
    cpu->f = flags;
}