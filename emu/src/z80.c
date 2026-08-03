
// z80.c
#include <stdio.h>
#include <stdbool.h>
#include "common.h"

// Base opcode dispatch table
Z80OpcodeHandler main_opcode_table[256];


// --- Helper Functions ---
static inline uint8_t fetch_byte(Z80 *cpu, uint8_t *ram) {
    return ram[cpu->pc++];
}

static inline uint16_t fetch_word(Z80 *cpu, uint8_t *ram) {
    uint8_t low = fetch_byte(cpu, ram);
    uint8_t high = fetch_byte(cpu, ram);
    return (high << 8) | low;
}

static void z80_push16(Z80 *cpu, uint16_t val);
static uint16_t z80_pop16(Z80 *cpu);


// Direct 16-bit memory access helpers
uint8_t z80_read_byte(Z80 *cpu, uint16_t address) {
    return cpu->memory[address];
}

void z80_write_byte(Z80 *cpu, uint16_t address, uint8_t value) {
    cpu->memory[address] = value;
}

uint8_t z80_io_in(Z80 *cpu, uint8_t port) {
    return cpu->io_ports[port];
}

void z80_io_out(Z80 *cpu, uint8_t port, uint8_t value) {
    cpu->io_ports[port] = value;
}

// --- Instruction Handlers ---

// Opcode 0x00: NOP (No Operation)
int z80_op_nop(Z80 *cpu, uint8_t *ram) {
    (void)cpu; (void)ram; // Unused
    return 4; // Takes 4 T-states
}

// Opcode 0x3E: LD A, n (Load immediate 8-bit value into A)
int z80_op_ld_a_n(Z80 *cpu, uint8_t *ram) {
    cpu->a = fetch_byte(cpu, ram);
    return 7;
}

// Opcode 0x01: LD BC, nn (Load immediate 16-bit value into BC)
int z80_op_ld_bc_nn(Z80 *cpu, uint8_t *ram) {
    cpu->bc = fetch_word(cpu, ram);
    return 10;
}

// Opcode 0xC3: JP nn (Unconditional Jump to 16-bit address)
int z80_op_jp_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    cpu->pc = target;
    return 10;
}

// Opcode 0xE9: JP (HL) - despite the parens, this jumps to the address
// *in* HL, not through it as a memory pointer (a well-known Z80 naming
// quirk shared with JP (IX)/JP (IY), which the DD/FD-prefixed 0xE9
// handler already gets right).
int z80_op_jp_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->pc = cpu->hl;
    return 4;
}

// Fallback for opcodes you haven't implemented yet
int z80_op_unimplemented(Z80 *cpu, uint8_t *ram) {
    uint8_t opcode = ram[cpu->pc - 1]; // Current opcode byte
    printf("Unimplemented opcode: 0x%02X at PC: 0x%04X\n", opcode, cpu->pc - 1);
    return 4;
}





// Opcode 0x87: ADD A, A
int z80_op_add_a_a(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_add(cpu, cpu->a);
    return 4;
}

// Opcode 0x80: ADD A, B
int z80_op_add_a_b(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_add(cpu, cpu->b);
    return 4;
}

// Opcode 0xC6: ADD A, n (Immediate)
int z80_op_add_a_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = ram[cpu->pc++];
    z80_alu_add(cpu, val);
    return 7;
}

// Opcode 0xFE: CP n (Immediate)
int z80_op_cp_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = ram[cpu->pc++];
    z80_alu_cp(cpu, val);
    return 7;
}

/*
 * 8-bit and 16-bit 
 */

// 0x04: INC B
int z80_op_inc_b(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->b = z80_alu_inc(cpu, cpu->b);
    return 4;
}

// 0x05: DEC B
int z80_op_dec_b(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->b = z80_alu_dec(cpu, cpu->b);
    return 4;
}

// 0x0C: INC C
int z80_op_inc_c(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->c = z80_alu_inc(cpu, cpu->c);
    return 4;
}

// 0x34: INC (HL)
int z80_op_inc_hl_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint16_t addr = cpu->hl;
    uint8_t val = z80_read_byte(cpu, addr);
    val = z80_alu_inc(cpu, val);
    z80_write_byte(cpu, addr, val);
    return 11;
}

// 0x09: ADD HL, BC
int z80_op_add_hl_bc(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_add16(cpu, cpu->bc);
    return 11;
}

// 0x19: ADD HL, DE
int z80_op_add_hl_de(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_add16(cpu, cpu->de);
    return 11;
}

// 0x29: ADD HL, HL
int z80_op_add_hl_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_add16(cpu, cpu->hl);
    return 11;
}

// 0x39: ADD HL, SP
int z80_op_add_hl_sp(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_add16(cpu, cpu->sp);
    return 11;
}

// 0x03: INC BC
int z80_op_inc_bc(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->bc++;
    return 6;
}

// 0x0B: DEC BC
int z80_op_dec_bc(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->bc--;
    return 6;
}

// 0x13: INC DE
int z80_op_inc_de(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->de++;
    return 6;
}

// 0x1B: DEC DE
int z80_op_dec_de(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->de--;
    return 6;
}

// 0x23: INC HL
int z80_op_inc_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->hl++;
    return 6;
}

// 0x2B: DEC HL
int z80_op_dec_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->hl--;
    return 6;
}

// 0x33: INC SP
int z80_op_inc_sp(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->sp++;
    return 6;
}

// 0x3B: DEC SP
int z80_op_dec_sp(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->sp--;
    return 6;
}

/*
 * CB registers
 */


// Helper to read register by target index [2:0]
static uint8_t get_cb_reg(Z80 *cpu, uint8_t reg_idx) {
    switch (reg_idx) {
        case 0: return cpu->b;
        case 1: return cpu->c;
        case 2: return cpu->d;
        case 3: return cpu->e;
        case 4: return cpu->h;
        case 5: return cpu->l;
        case 6: return z80_read_byte(cpu, cpu->hl);
        case 7: return cpu->a;
        default: return 0;
    }
}

// Helper to write register back by target index [2:0]
static void set_cb_reg(Z80 *cpu, uint8_t reg_idx, uint8_t val) {
    switch (reg_idx) {
        case 0: cpu->b = val; break;
        case 1: cpu->c = val; break;
        case 2: cpu->d = val; break;
        case 3: cpu->e = val; break;
        case 4: cpu->h = val; break;
        case 5: cpu->l = val; break;
        case 6: z80_write_byte(cpu, cpu->hl, val); break;
        case 7: cpu->a = val; break;
    }
}

// The master 0xCB handler registered at main_opcode_table[0xCB]
int z80_op_prefix_cb(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t cb_code = z80_read_byte(cpu, cpu->pc++);

    uint8_t reg_idx = cb_code & 0x07;       // Bits 0-2
    uint8_t bit_or_op = (cb_code >> 3) & 0x07; // Bits 3-5
    uint8_t category = (cb_code >> 6) & 0x03;  // Bits 6-7

    uint8_t val = get_cb_reg(cpu, reg_idx);
    int cycles = (reg_idx == 6) ? 15 : 8; // Memory operations take extra T-states

    switch (category) {
        case 0: // Rotates and Shifts
            switch (bit_or_op) {
                case 0: val = z80_alu_rlc(cpu, val); break;
                case 1: val = z80_alu_rrc(cpu, val); break;
                case 2: val = z80_alu_rl(cpu, val); break;
                case 3: val = z80_alu_rr(cpu, val); break;
                case 4: val = z80_alu_sla(cpu, val); break;
                case 5: val = z80_alu_sra(cpu, val); break;
                case 6: val = z80_alu_sll(cpu, val); break; // Undocumented SLL
                case 7: val = z80_alu_srl(cpu, val); break;
            }
            set_cb_reg(cpu, reg_idx, val);
            break;

        case 1: // BIT b, r
            z80_alu_bit(cpu, bit_or_op, val);
            if (reg_idx == 6) {
                cycles = 12;
                // Undocumented: for BIT b,(HL), X/Y come from the high byte
                // of HL+1 (the internal MEMPTR register), not the tested byte.
                cpu->f = (uint8_t)((cpu->f & ~(FLAG_X | FLAG_Y)) |
                          (((cpu->hl + 1) >> 8) & (FLAG_X | FLAG_Y)));
            }
            break;

        case 2: // RES b, r
            val = z80_alu_res(bit_or_op, val);
            set_cb_reg(cpu, reg_idx, val);
            break;

        case 3: // SET b, r
            val = z80_alu_set(bit_or_op, val);
            set_cb_reg(cpu, reg_idx, val);
            break;
    }

    return cycles;
}

/*
 * Special instructions
 */

 // CPL (0x2F): Complement Accumulator
int z80_op_cpl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = ~cpu->a;

    // Retain S, Z, P/V, C; set H and N
    uint8_t flags = (cpu->f & (FLAG_S | FLAG_Z | FLAG_PV | FLAG_C)) | FLAG_H | FLAG_N;
    
    // Copy bits 3 and 5 from the updated Accumulator
    flags |= (cpu->a & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return 4;
}

// NEG (0xED 0x44): Negate Accumulator
int z80_op_neg(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t orig_a = cpu->a;
    cpu->a = 0;
    
    // NEG performs 0 - A using our existing SUB helper
    z80_alu_sub(cpu, orig_a);
    return 8;
}

// SCF (0x37): Set Carry Flag
int z80_op_scf(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    // Retain S, Z, P/V; clear H and N; set C
    uint8_t flags = (cpu->f & (FLAG_S | FLAG_Z | FLAG_PV)) | FLAG_C;
    
    // Copy bits 3 and 5 from Accumulator
    flags |= (cpu->a & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return 4;
}

// CCF (0x3F): Complement Carry Flag
int z80_op_ccf(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t old_carry = (cpu->f & FLAG_C) ? 1 : 0;
    
    // Retain S, Z, P/V; clear N; invert C; set H = old_carry
    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_PV);
    if (old_carry) flags |= FLAG_H; // H gets old carry bit
    if (!old_carry) flags |= FLAG_C; // Invert carry bit
    
    // Copy bits 3 and 5 from Accumulator
    flags |= (cpu->a & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return 4;
}

// DAA (0x27): Decimal Adjust Accumulator
int z80_op_daa(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t a = cpu->a;
    uint8_t correction = 0;
    uint8_t carry = cpu->f & FLAG_C;
    uint8_t half_carry = cpu->f & FLAG_H;
    uint8_t sub = cpu->f & FLAG_N;

    if (half_carry || ((a & 0x0F) > 0x09)) {
        correction |= 0x06;
    }

    if (carry || (a > 0x99)) {
        correction |= 0x60;
        carry = FLAG_C;
    }

    if (sub) {
        cpu->a -= correction;
    } else {
        cpu->a += correction;
    }

    // Recalculate flags
    uint8_t flags = sub | carry;
    if (cpu->a & 0x80) flags |= FLAG_S;
    if (cpu->a == 0)   flags |= FLAG_Z;
    
    // Half-Carry logic for DAA
    if (sub) {
        if (half_carry && ((a & 0x0F) < 0x06)) flags |= FLAG_H;
    } else {
        if ((a & 0x0F) > 0x09) flags |= FLAG_H;
    }

    flags |= calculate_parity(cpu->a);
    flags |= (cpu->a & (FLAG_X | FLAG_Y));

    cpu->f = flags;
    return 4;
}

// 0xED 0x73: LD (nn), SP
int z80_op_ed_ld_mem_nn_sp(Z80 *cpu, uint8_t *ram) {
    uint16_t addr = fetch_word(cpu, ram);
    z80_write_byte(cpu, addr, cpu->sp & 0xFF);        // Low byte
    z80_write_byte(cpu, addr + 1, (cpu->sp >> 8) & 0xFF); // High byte
    return 20;
}

// 0xED 0x7B: LD SP, (nn)
int z80_op_ed_ld_sp_mem_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t addr = fetch_word(cpu, ram);
    uint8_t low = z80_read_byte(cpu, addr);
    uint8_t high = z80_read_byte(cpu, addr + 1);
    cpu->sp = (high << 8) | low;
    return 20;
}

// ADC HL, val (16-bit Add with Carry)
void z80_alu_adc16(Z80 *cpu, uint16_t val) {
    uint8_t carry = (cpu->f & FLAG_C) ? 1 : 0;
    uint32_t res = (uint32_t)cpu->hl + val + carry;
    uint16_t res16 = (uint16_t)res;

    uint8_t flags = 0;
    if (res16 & 0x8000) flags |= FLAG_S;
    if (res16 == 0)   flags |= FLAG_Z;
    if (((cpu->hl & 0x0FFF) + (val & 0x0FFF) + carry) > 0x0FFF) flags |= FLAG_H;
    
    // Overflow: set if operands have same sign but result has different sign
    if (~(cpu->hl ^ val) & (cpu->hl ^ res16) & 0x8000) flags |= FLAG_PV;
    
    if (res > 0xFFFF) flags |= FLAG_C;
    
    flags |= ((res16 >> 8) & (FLAG_X | FLAG_Y));

    cpu->hl = res16;
    cpu->f = flags;
}

// SBC HL, val (16-bit Subtract with Carry)
void z80_alu_sbc16(Z80 *cpu, uint16_t val) {
    uint8_t carry = (cpu->f & FLAG_C) ? 1 : 0;
    uint32_t res = (uint32_t)cpu->hl - val - carry;
    uint16_t res16 = (uint16_t)res;

    uint8_t flags = FLAG_N;
    if (res16 & 0x8000) flags |= FLAG_S;
    if (res16 == 0)   flags |= FLAG_Z;
    if ((cpu->hl & 0x0FFF) < ((val & 0x0FFF) + carry)) flags |= FLAG_H;
    
    // Overflow: set if operands have different signs and result sign differs from HL
    if ((cpu->hl ^ val) & (cpu->hl ^ res16) & 0x8000) flags |= FLAG_PV;
    
    if (cpu->hl < (val + carry)) flags |= FLAG_C;
    
    flags |= ((res16 >> 8) & (FLAG_X | FLAG_Y));

    cpu->hl = res16;
    cpu->f = flags;
}

// 0xED prefix handler - handles all 0xED extended opcodes
int z80_op_prefix_ed(Z80 *cpu, uint8_t *ram) {
    uint8_t ed_opcode = z80_read_byte(cpu, cpu->pc++);

    switch (ed_opcode) {
        // --- 16-Bit Register Memory Loads ---
        case 0x43: { // LD (nn), BC
            uint16_t addr = fetch_word(cpu, ram);
            z80_write_byte(cpu, addr, cpu->c);
            z80_write_byte(cpu, addr + 1, cpu->b);
            return 20;
        }
        case 0x4B: { // LD BC, (nn)
            uint16_t addr = fetch_word(cpu, ram);
            uint8_t low = z80_read_byte(cpu, addr);
            uint8_t high = z80_read_byte(cpu, addr + 1);
            cpu->bc = (high << 8) | low;
            return 20;
        }
        case 0x53: { // LD (nn), DE
            uint16_t addr = fetch_word(cpu, ram);
            z80_write_byte(cpu, addr, cpu->e);
            z80_write_byte(cpu, addr + 1, cpu->d);
            return 20;
        }
        case 0x5B: { // LD DE, (nn)
            uint16_t addr = fetch_word(cpu, ram);
            uint8_t low = z80_read_byte(cpu, addr);
            uint8_t high = z80_read_byte(cpu, addr + 1);
            cpu->de = (high << 8) | low;
            return 20;
        }
        case 0x63: { // LD (nn), HL
            uint16_t addr = fetch_word(cpu, ram);
            z80_write_byte(cpu, addr, cpu->l);
            z80_write_byte(cpu, addr + 1, cpu->h);
            return 20;
        }
        case 0x6B: { // LD HL, (nn)
            uint16_t addr = fetch_word(cpu, ram);
            uint8_t low = z80_read_byte(cpu, addr);
            uint8_t high = z80_read_byte(cpu, addr + 1);
            cpu->hl = (high << 8) | low;
            return 20;
        }
        case 0x73: return z80_op_ed_ld_mem_nn_sp(cpu, ram); // LD (nn), SP
        case 0x7B: return z80_op_ed_ld_sp_mem_nn(cpu, ram); // LD SP, (nn)

        // --- 16-Bit ADC HL ---
        case 0x4A: // ADC HL, BC
            z80_alu_adc16(cpu, cpu->bc);
            return 15;
        case 0x5A: // ADC HL, DE
            z80_alu_adc16(cpu, cpu->de);
            return 15;
        case 0x6A: // ADC HL, HL
            z80_alu_adc16(cpu, cpu->hl);
            return 15;
        case 0x7A: // ADC HL, SP
            z80_alu_adc16(cpu, cpu->sp);
            return 15;

        // --- 16-Bit SBC HL ---
        case 0x42: // SBC HL, BC
            z80_alu_sbc16(cpu, cpu->bc);
            return 15;
        case 0x52: // SBC HL, DE
            z80_alu_sbc16(cpu, cpu->de);
            return 15;
        case 0x62: // SBC HL, HL
            z80_alu_sbc16(cpu, cpu->hl);
            return 15;
        case 0x72: // SBC HL, SP
            z80_alu_sbc16(cpu, cpu->sp);
            return 15;

        // --- Negate Accumulator (all variants alias to NEG) ---
        case 0x44: case 0x4C: case 0x54: case 0x5C:
        case 0x64: case 0x6C: case 0x74: case 0x7C:
            return z80_op_neg(cpu, ram);

        // --- RETI / RETN ---
        case 0x4D: // RETI
            cpu->pc = z80_pop16(cpu);
            return 14;
        case 0x45: case 0x55: case 0x5D: case 0x65:
        case 0x6D: case 0x75: case 0x7D: // RETN (and undocumented duplicates)
            cpu->pc = z80_pop16(cpu);
            cpu->iff1 = cpu->iff2;
            return 14;

        // --- Interrupt Mode ---
        case 0x46: case 0x4E: case 0x66: case 0x6E: // IM 0
            cpu->im = 0;
            return 8;
        case 0x56: case 0x76: // IM 1
            cpu->im = 1;
            return 8;
        case 0x5E: case 0x7E: // IM 2
            cpu->im = 2;
            return 8;

        // --- I/R <-> A transfers ---
        case 0x47: // LD I, A
            cpu->i = cpu->a;
            return 9;
        case 0x4F: // LD R, A
            cpu->r = cpu->a;
            return 9;
        case 0x57: // LD A, I
        case 0x5F: { // LD A, R
            uint8_t val = (ed_opcode == 0x57) ? cpu->i : cpu->r;
            cpu->a = val;
            uint8_t flags = cpu->f & FLAG_C; // C unaffected
            if (val & 0x80) flags |= FLAG_S;
            if (val == 0) flags |= FLAG_Z;
            if (cpu->iff2) flags |= FLAG_PV;
            flags |= (val & (FLAG_X | FLAG_Y));
            cpu->f = flags;
            return 9;
        }

        // --- IN r,(C) / OUT (C),r ---
        case 0x40: case 0x48: case 0x50: case 0x58:
        case 0x60: case 0x68: case 0x70: case 0x78: { // IN r,(C)
            uint8_t reg_idx = (ed_opcode >> 3) & 0x07;
            uint8_t val = z80_io_in(cpu, cpu->c);
            if (reg_idx != 6) set_cb_reg(cpu, reg_idx, val); // idx 6 = undocumented IN (C): flags only
            uint8_t flags = cpu->f & FLAG_C; // C unaffected
            if (val & 0x80) flags |= FLAG_S;
            if (val == 0) flags |= FLAG_Z;
            flags |= calculate_parity(val);
            flags |= (val & (FLAG_X | FLAG_Y));
            cpu->f = flags;
            return 12;
        }
        case 0x41: case 0x49: case 0x51: case 0x59:
        case 0x61: case 0x69: case 0x71: case 0x79: { // OUT (C),r
            uint8_t reg_idx = (ed_opcode >> 3) & 0x07;
            uint8_t val = (reg_idx == 6) ? 0 : get_cb_reg(cpu, reg_idx); // undocumented OUT (C),0
            z80_io_out(cpu, cpu->c, val);
            return 12;
        }

        // --- Block Transfers ---
        case 0xA0: return z80_alu_block_transfer(cpu,  1, false); // LDI
        case 0xB0: return z80_alu_block_transfer(cpu,  1, true);  // LDIR
        case 0xA8: return z80_alu_block_transfer(cpu, -1, false); // LDD
        case 0xB8: return z80_alu_block_transfer(cpu, -1, true);  // LDDR

        // --- Block Searches ---
        case 0xA1: return z80_alu_block_search(cpu,  1, false);   // CPI
        case 0xB1: return z80_alu_block_search(cpu,  1, true);    // CPIR
        case 0xA9: return z80_alu_block_search(cpu, -1, false);   // CPD
        case 0xB9: return z80_alu_block_search(cpu, -1, true);    // CPDR

        // --- BCD Digit Rotates ---
        case 0x6F: { // RLD
            uint8_t mem = z80_read_byte(cpu, cpu->hl);
            uint8_t a_lo = cpu->a & 0x0F;
            uint8_t new_mem = (uint8_t)((mem << 4) | a_lo);
            cpu->a = (uint8_t)((cpu->a & 0xF0) | (mem >> 4));
            z80_write_byte(cpu, cpu->hl, new_mem);

            uint8_t flags = cpu->f & FLAG_C; // C unaffected
            if (cpu->a & 0x80) flags |= FLAG_S;
            if (cpu->a == 0) flags |= FLAG_Z;
            flags |= (cpu->a & (FLAG_X | FLAG_Y));
            flags |= calculate_parity(cpu->a);
            cpu->f = flags;
            return 18;
        }
        case 0x67: { // RRD
            uint8_t mem = z80_read_byte(cpu, cpu->hl);
            uint8_t a_lo = cpu->a & 0x0F;
            uint8_t new_mem = (uint8_t)((a_lo << 4) | (mem >> 4));
            cpu->a = (uint8_t)((cpu->a & 0xF0) | (mem & 0x0F));
            z80_write_byte(cpu, cpu->hl, new_mem);

            uint8_t flags = cpu->f & FLAG_C; // C unaffected
            if (cpu->a & 0x80) flags |= FLAG_S;
            if (cpu->a == 0) flags |= FLAG_Z;
            flags |= (cpu->a & (FLAG_X | FLAG_Y));
            flags |= calculate_parity(cpu->a);
            cpu->f = flags;
            return 18;
        }

        default:
            printf("[0xED] Unimplemented opcode: 0xED 0x%02X at PC: 0x%04X\n", 
                   ed_opcode, cpu->pc - 2);
            return 4;
    }
}

    

/*
 * IX/IY
 */

 // Handles double prefix operations: 0xDD 0xCB d opcode / 0xFD 0xCB d opcode
int z80_op_index_cb(Z80 *cpu, uint8_t *ram, uint16_t addr, uint8_t cb_opcode) {
    (void)ram;
    uint8_t reg_copy_idx = cb_opcode & 0x07;       // Bits 0-2 (Copy target register)
    uint8_t bit_or_op    = (cb_opcode >> 3) & 0x07; // Bits 3-5 (Operation / Bit index)
    uint8_t category     = (cb_opcode >> 6) & 0x03; // Bits 6-7 (Category)

    // Read initial byte from target memory location (IX+d or IY+d)
    uint8_t val = z80_read_byte(cpu, addr);

    switch (category) {
        case 0: // Rotates and Shifts
            switch (bit_or_op) {
                case 0: val = z80_alu_rlc(cpu, val); break;
                case 1: val = z80_alu_rrc(cpu, val); break;
                case 2: val = z80_alu_rl(cpu, val); break;
                case 3: val = z80_alu_rr(cpu, val); break;
                case 4: val = z80_alu_sla(cpu, val); break;
                case 5: val = z80_alu_sra(cpu, val); break;
                case 6: val = z80_alu_sll(cpu, val); break; // Undocumented SLL
                case 7: val = z80_alu_srl(cpu, val); break;
            }
            // Always write result back to memory address
            z80_write_byte(cpu, addr, val);

            // Undocumented Z80 feature: copy result to register if reg_copy_idx != 6
            if (reg_copy_idx != 6) {
                set_cb_reg(cpu, reg_copy_idx, val);
            }
            break;

        case 1: // BIT b, (IX/IY + d)
            z80_alu_bit(cpu, bit_or_op, val);
            
            // For BIT on indexed memory, undocumented X/Y flags are taken from 
            // the high byte of the target address pointer (addr >> 8)
            cpu->f = (cpu->f & ~(FLAG_X | FLAG_Y)) | ((addr >> 8) & (FLAG_X | FLAG_Y));
            break;

        case 2: // RES b, (IX/IY + d)
            val = z80_alu_res(bit_or_op, val);
            z80_write_byte(cpu, addr, val);

            if (reg_copy_idx != 6) {
                set_cb_reg(cpu, reg_copy_idx, val);
            }
            break;

        case 3: // SET b, (IX/IY + d)
            val = z80_alu_set(bit_or_op, val);
            z80_write_byte(cpu, addr, val);

            if (reg_copy_idx != 6) {
                set_cb_reg(cpu, reg_copy_idx, val);
            }
            break;
    }

    return 23; // Indexed CB operations always consume 23 T-states
}


// Helper to handle IX or IY base prefix operations
// Read an 8-bit operand for a DD/FD-prefixed opcode in the 0x40-0xBF range.
// B, C, D, E, A are unaffected by the prefix; H/L become IXH/IYH, IXL/IYL
// (undocumented); (HL) becomes (index_reg + d), consuming a displacement
// byte from the instruction stream.
static uint8_t get_idx_reg8(Z80 *cpu, uint16_t *index_reg, uint8_t reg_idx) {
    switch (reg_idx) {
        case 0: return cpu->b;
        case 1: return cpu->c;
        case 2: return cpu->d;
        case 3: return cpu->e;
        case 4: return (uint8_t)(*index_reg >> 8);
        case 5: return (uint8_t)(*index_reg & 0xFF);
        case 6: {
            int8_t d = (int8_t)z80_read_byte(cpu, cpu->pc++);
            return z80_read_byte(cpu, (uint16_t)(*index_reg + d));
        }
        case 7: return cpu->a;
        default: return 0;
    }
}

// Write an 8-bit operand for a DD/FD-prefixed opcode; mirrors get_idx_reg8.
static void set_idx_reg8(Z80 *cpu, uint16_t *index_reg, uint8_t reg_idx, uint8_t val) {
    switch (reg_idx) {
        case 0: cpu->b = val; break;
        case 1: cpu->c = val; break;
        case 2: cpu->d = val; break;
        case 3: cpu->e = val; break;
        case 4: *index_reg = (uint16_t)((*index_reg & 0x00FF) | ((uint16_t)val << 8)); break;
        case 5: *index_reg = (uint16_t)((*index_reg & 0xFF00) | val); break;
        case 6: {
            int8_t d = (int8_t)z80_read_byte(cpu, cpu->pc++);
            z80_write_byte(cpu, (uint16_t)(*index_reg + d), val);
            break;
        }
        case 7: cpu->a = val; break;
    }
}

// DD/FD-prefixed 0x40-0x7F: LD r,r' with H/L->IXH/IXL and (HL)->(index_reg+d).
// 0x76 remains HALT (unaffected by the prefix), as on real hardware.
static int z80_op_index_ld_r_r(Z80 *cpu, uint16_t *index_reg, uint8_t opcode) {
    if (opcode == 0x76) {
        cpu->pc--;
        return 4;
    }

    uint8_t src_idx = opcode & 0x07;
    uint8_t dst_idx = (opcode >> 3) & 0x07;
    bool mem_involved = (src_idx == 6 || dst_idx == 6);

    // When (IX+d)/(IY+d) memory is one of the operands, a H/L on the OTHER
    // side means the real H/L register, not IXH/IXL - real Z80 hardware
    // quirk (e.g. LD H,(IX+d) loads into H, not IXH).
    uint8_t val;
    if (mem_involved && src_idx == 4) val = cpu->h;
    else if (mem_involved && src_idx == 5) val = cpu->l;
    else val = get_idx_reg8(cpu, index_reg, src_idx);

    if (mem_involved && dst_idx == 4) cpu->h = val;
    else if (mem_involved && dst_idx == 5) cpu->l = val;
    else set_idx_reg8(cpu, index_reg, dst_idx, val);

    return mem_involved ? 19 : 8;
}

// DD/FD-prefixed 0x80-0xBF: ALU A,r with H/L->IXH/IXL and (HL)->(index_reg+d).
static int z80_op_index_alu_group(Z80 *cpu, uint16_t *index_reg, uint8_t opcode) {
    uint8_t src_idx = opcode & 0x07;
    uint8_t operation = (opcode >> 3) & 0x07;

    uint8_t val = get_idx_reg8(cpu, index_reg, src_idx);

    switch (operation) {
        case 0: z80_alu_add(cpu, val); break;
        case 1: z80_alu_adc(cpu, val); break;
        case 2: z80_alu_sub(cpu, val); break;
        case 3: z80_alu_sbc(cpu, val); break;
        case 4: z80_alu_and(cpu, val); break;
        case 5: z80_alu_xor(cpu, val); break;
        case 6: z80_alu_or(cpu, val);  break;
        case 7: z80_alu_cp(cpu, val);  break;
    }

    return (src_idx == 6) ? 19 : 8;
}

int z80_op_prefix_index(Z80 *cpu, uint8_t *ram, uint16_t *index_reg) {
    uint8_t opcode = z80_read_byte(cpu, cpu->pc++);

    switch (opcode) {
        // --- 16-bit Load Immediate into Index Register ---
        case 0x21: // LD IX/IY, nn
            *index_reg = fetch_word(cpu, ram);
            return 14;
        case 0x22: { // LD (nn), IX/IY
            uint16_t addr = fetch_word(cpu, ram);
            z80_write_byte(cpu, addr, (uint8_t)(*index_reg & 0xFF));
            z80_write_byte(cpu, (uint16_t)(addr + 1), (uint8_t)(*index_reg >> 8));
            return 20;
        }
        case 0x2A: { // LD IX/IY, (nn)
            uint16_t addr = fetch_word(cpu, ram);
            uint8_t low = z80_read_byte(cpu, addr);
            uint8_t high = z80_read_byte(cpu, (uint16_t)(addr + 1));
            *index_reg = (uint16_t)((high << 8) | low);
            return 20;
        }

        // --- 16-bit Addition ---
        case 0x09: // ADD IX/IY, BC
            z80_alu_add16_idx(cpu, index_reg, cpu->bc);
            return 15;
        case 0x19: // ADD IX/IY, DE
            z80_alu_add16_idx(cpu, index_reg, cpu->de);
            return 15;
        case 0x29: // ADD IX/IY, IX/IY
            z80_alu_add16_idx(cpu, index_reg, *index_reg);
            return 15;
        case 0x39: // ADD IX/IY, SP
            z80_alu_add16_idx(cpu, index_reg, cpu->sp);
            return 15;

        // --- 16-bit Inc / Dec ---
        case 0x23: // INC IX/IY
            (*index_reg)++;
            return 10;
        case 0x2B: // DEC IX/IY
            (*index_reg)--;
            return 10;

        // --- Push / Pop Index Register ---
        case 0xE1: // POP IX/IY
            *index_reg = z80_pop16(cpu);
            return 14;
        case 0xE5: // PUSH IX/IY
            z80_push16(cpu, *index_reg);
            return 15;

        // --- Exchange / Transfer ---
        case 0xE3: { // EX (SP), IX/IY
            uint16_t low = z80_read_byte(cpu, cpu->sp);
            uint16_t high = z80_read_byte(cpu, (uint16_t)(cpu->sp + 1));
            z80_write_byte(cpu, cpu->sp, (uint8_t)(*index_reg & 0xFF));
            z80_write_byte(cpu, (uint16_t)(cpu->sp + 1), (uint8_t)(*index_reg >> 8));
            *index_reg = (uint16_t)((high << 8) | low);
            return 23;
        }
        case 0xE9: // JP (IX/IY)
            cpu->pc = *index_reg;
            return 8;
        case 0xF9: // LD SP, IX/IY
            cpu->sp = *index_reg;
            return 10;

        // --- 8-bit Inc / Dec / Load Immediate on IXH/IXL ---
        case 0x24: // INC IXH/IYH
            *index_reg = (uint16_t)((*index_reg & 0x00FF) |
                         ((uint16_t)z80_alu_inc(cpu, (uint8_t)(*index_reg >> 8)) << 8));
            return 8;
        case 0x25: // DEC IXH/IYH
            *index_reg = (uint16_t)((*index_reg & 0x00FF) |
                         ((uint16_t)z80_alu_dec(cpu, (uint8_t)(*index_reg >> 8)) << 8));
            return 8;
        case 0x26: // LD IXH/IYH, n
            *index_reg = (uint16_t)((*index_reg & 0x00FF) | ((uint16_t)fetch_byte(cpu, ram) << 8));
            return 11;
        case 0x2C: // INC IXL/IYL
            *index_reg = (uint16_t)((*index_reg & 0xFF00) |
                         z80_alu_inc(cpu, (uint8_t)(*index_reg & 0xFF)));
            return 8;
        case 0x2D: // DEC IXL/IYL
            *index_reg = (uint16_t)((*index_reg & 0xFF00) |
                         z80_alu_dec(cpu, (uint8_t)(*index_reg & 0xFF)));
            return 8;
        case 0x2E: // LD IXL/IYL, n
            *index_reg = (uint16_t)((*index_reg & 0xFF00) | fetch_byte(cpu, ram));
            return 11;

        // --- 8-bit Inc / Dec / Load Immediate with Displacement ---
        case 0x34: { // INC (IX/IY + d)
            int8_t d = (int8_t)z80_read_byte(cpu, cpu->pc++);
            uint16_t addr = (uint16_t)(*index_reg + d);
            z80_write_byte(cpu, addr, z80_alu_inc(cpu, z80_read_byte(cpu, addr)));
            return 23;
        }
        case 0x35: { // DEC (IX/IY + d)
            int8_t d = (int8_t)z80_read_byte(cpu, cpu->pc++);
            uint16_t addr = (uint16_t)(*index_reg + d);
            z80_write_byte(cpu, addr, z80_alu_dec(cpu, z80_read_byte(cpu, addr)));
            return 23;
        }
        case 0x36: { // LD (IX/IY + d), n
            int8_t d = (int8_t)z80_read_byte(cpu, cpu->pc++);
            uint8_t val = fetch_byte(cpu, ram);
            z80_write_byte(cpu, (uint16_t)(*index_reg + d), val);
            return 19;
        }

        // --- Double Prefix: DD CB d opcode / FD CB d opcode ---
        case 0xCB: {
            int8_t d = (int8_t)z80_read_byte(cpu, cpu->pc++);
            uint8_t cb_opcode = z80_read_byte(cpu, cpu->pc++);
            return z80_op_index_cb(cpu, ram, *index_reg + d, cb_opcode);
        }

        default:
            if (opcode >= 0x40 && opcode <= 0x7F) {
                return z80_op_index_ld_r_r(cpu, index_reg, opcode);
            }
            if (opcode >= 0x80 && opcode <= 0xBF) {
                return z80_op_index_alu_group(cpu, index_reg, opcode);
            }
            // Genuinely prefix-independent opcode: decode as standard
            // opcode. PC is already positioned one past the opcode byte
            // (matching what main_opcode_table handlers expect), so no
            // further adjustment is needed here.
            return main_opcode_table[opcode](cpu, ram);
    }
}

// Handler for 0xDD prefix (IX)
int z80_op_prefix_dd(Z80 *cpu, uint8_t *ram) {
    return z80_op_prefix_index(cpu, ram, &cpu->ix);
}

// Handler for 0xFD prefix (IY)
int z80_op_prefix_fd(Z80 *cpu, uint8_t *ram) {
    return z80_op_prefix_index(cpu, ram, &cpu->iy);
}




/*
 * Init vector tables
 */

 /*
 // Handlers for secondary lookup tables
 int z80_op_prefix_cb(Z80 *cpu, uint8_t *ram) {
    uint8_t cb_opcode = fetch_byte(cpu, ram);
    return cb_opcode_table[cb_opcode](cpu, ram);
}

int z80_op_prefix_ed(Z80 *cpu, uint8_t *ram) {
    uint8_t ed_opcode = fetch_byte(cpu, ram);
    return ed_opcode_table[ed_opcode](cpu, ram);
}
*/

/*
 * push/pop
 */
// Stack Helpers
static void z80_push16(Z80 *cpu, uint16_t val) {
    cpu->sp--;
    z80_write_byte(cpu, cpu->sp, (val >> 8) & 0xFF); // High byte
    cpu->sp--;
    z80_write_byte(cpu, cpu->sp, val & 0xFF);        // Low byte
}

static uint16_t z80_pop16(Z80 *cpu) {
    uint8_t low = z80_read_byte(cpu, cpu->sp++);
    uint8_t high = z80_read_byte(cpu, cpu->sp++);
    return (high << 8) | low;
}

// Opcode Handlers
int z80_op_push_bc(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_push16(cpu, cpu->bc);
    return 11;
}

int z80_op_pop_bc(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->bc = z80_pop16(cpu);
    return 10;
}

int z80_op_pop_af(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->af = z80_pop16(cpu);
    return 10;
}

static bool z80_check_cc(Z80 *cpu, uint8_t cc) {
    switch (cc) {
        case 0: return !(cpu->f & FLAG_Z);  // NZ (Not Zero)
        case 1: return  (cpu->f & FLAG_Z);  // Z  (Zero)
        case 2: return !(cpu->f & FLAG_C);  // NC (No Carry)
        case 3: return  (cpu->f & FLAG_C);  // C  (Carry)
        case 4: return !(cpu->f & FLAG_PV); // PO (Parity Odd / No Overflow)
        case 5: return  (cpu->f & FLAG_PV); // PE (Parity Even / Overflow)
        case 6: return !(cpu->f & FLAG_S);  // P  (Positive / Plus)
        case 7: return  (cpu->f & FLAG_S);  // M  (Minus / Negative)
        default: return false;
    }
}


// Unconditional CALL nn
int z80_op_call_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    z80_push16(cpu, cpu->pc); // Push return address onto stack
    cpu->pc = target;
    return 17;
}

// Conditional CALL cc, nn
int z80_op_call_cc_nn(Z80 *cpu, uint8_t cc, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (z80_check_cc(cpu, cc)) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10; // Fewer T-states if condition isn't met
}

// Unconditional RET
int z80_op_ret(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->pc = z80_pop16(cpu);
    return 10;
}

// 0xC0: RET NZ
int z80_op_ret_nz(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (!(cpu->f & FLAG_Z)) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// 0xC8: RET Z
int z80_op_ret_z(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (cpu->f & FLAG_Z) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// 0xD0: RET NC
int z80_op_ret_nc(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (!(cpu->f & FLAG_C)) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// 0xD8: RET C
int z80_op_ret_c(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (cpu->f & FLAG_C) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// 0xE0: RET PO
int z80_op_ret_po(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (!(cpu->f & FLAG_PV)) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// 0xE8: RET PE
int z80_op_ret_pe(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (cpu->f & FLAG_PV) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// Conditional RET cc
int z80_op_ret_cc(Z80 *cpu, uint8_t cc, uint8_t *ram) {
    (void)ram;
    if (z80_check_cc(cpu, cc)) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// Restarts: RST p (p = 0x00, 0x08, 0x10, 0x18, 0x20, 0x28, 0x30, 0x38)
int z80_op_rst(Z80 *cpu, uint8_t p) {
    z80_push16(cpu, cpu->pc);
    cpu->pc = (uint16_t)p;
    return 11;
}

// 0xEB: EX DE, HL
int z80_op_ex_de_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint16_t temp = cpu->de;
    cpu->de = cpu->hl;
    cpu->hl = temp;
    return 4;
}

// 0x08: EX AF, AF'
int z80_op_ex_af_af(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint16_t temp = cpu->af;
    cpu->af = cpu->af_alt;
    cpu->af_alt = temp;
    return 4;
}

// 0xD9: EXX (Exchanges BC, DE, HL with B', C', D', E', H', L')
int z80_op_exx(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint16_t temp;

    temp = cpu->bc; cpu->bc = cpu->bc_alt; cpu->bc_alt = temp;
    temp = cpu->de; cpu->de = cpu->de_alt; cpu->de_alt = temp;
    temp = cpu->hl; cpu->hl = cpu->hl_alt; cpu->hl_alt = temp;

    return 4;
}

// 0xE3: EX (SP), HL (Swaps HL with value at top of memory stack)
int z80_op_ex_sp_mem_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t low = z80_read_byte(cpu, cpu->sp);
    uint8_t high = z80_read_byte(cpu, cpu->sp + 1);
    uint16_t mem_val = (high << 8) | low;

    z80_write_byte(cpu, cpu->sp, cpu->l);
    z80_write_byte(cpu, cpu->sp + 1, cpu->h);

    cpu->hl = mem_val;
    return 19;
}

/*
 * Missed
 */
// 0x2A: LD HL, (nn)
int z80_op_ld_hl_mem_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t addr = fetch_word(cpu, ram);
    uint8_t low = z80_read_byte(cpu, addr);
    uint8_t high = z80_read_byte(cpu, addr + 1);
    cpu->hl = (high << 8) | low;
    return 16;
}

// 0x06: LD B, n
int z80_op_ld_b_n(Z80 *cpu, uint8_t *ram) {
    cpu->b = fetch_byte(cpu, ram);
    return 7;
}

// 0x0E: LD C, n
int z80_op_ld_c_n(Z80 *cpu, uint8_t *ram) {
    cpu->c = fetch_byte(cpu, ram);
    return 7;
}

// 0x1E: LD E, n
int z80_op_ld_e_n(Z80 *cpu, uint8_t *ram) {
    cpu->e = fetch_byte(cpu, ram);
    return 7;
}

// 0x11: LD DE, nn
int z80_op_ld_de_nn(Z80 *cpu, uint8_t *ram) {
    cpu->de = fetch_word(cpu, ram);
    return 10;
}

// 0xF9: LD SP, HL
int z80_op_ld_sp_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->sp = cpu->hl;
    return 6;
}

// 0xDA: JP C, nn
int z80_op_jp_c_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_C) {
        cpu->pc = target;
    }
    return 10;
}

// 0x1D: DEC E
int z80_op_dec_e(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->e = z80_alu_dec(cpu, cpu->e);
    return 4;
}

// Push Handlers
int z80_op_push_af(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_push16(cpu, cpu->af);
    return 11;
}

int z80_op_push_de(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_push16(cpu, cpu->de);
    return 11;
}

int z80_op_push_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_push16(cpu, cpu->hl);
    return 11;
}

// Pop Handlers
int z80_op_pop_de(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->de = z80_pop16(cpu);
    return 10;
}

int z80_op_pop_hl(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->hl = z80_pop16(cpu);
    return 10;
}

/*
 * Next batch
 */

 // 0x21: LD HL, nn
int z80_op_ld_hl_nn(Z80 *cpu, uint8_t *ram) {
    cpu->hl = fetch_word(cpu, ram);
    return 10;
}

// 0x3A: LD A, (nn)
int z80_op_ld_a_mem_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t addr = fetch_word(cpu, ram);
    cpu->a = z80_read_byte(cpu, addr);
    return 13;
}

// 0x7E: LD A, (HL)
int z80_op_ld_a_hl_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = z80_read_byte(cpu, cpu->hl);
    return 7;
}

// 0x1A: LD A, (DE)
int z80_op_ld_a_de_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = z80_read_byte(cpu, cpu->de);
    return 7;
}

// 0x12: LD (DE), A
int z80_op_ld_de_mem_a(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_write_byte(cpu, cpu->de, cpu->a);
    return 7;
}

// 0x02: LD (BC), A
int z80_op_ld_bc_mem_a(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_write_byte(cpu, cpu->bc, cpu->a);
    return 7;
}

// 0x0A: LD A, (BC)
int z80_op_ld_a_bc_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = z80_read_byte(cpu, cpu->bc);
    return 7;
}

// 0xB6: OR (HL)
int z80_op_or_hl_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t val = z80_read_byte(cpu, cpu->hl);
    z80_alu_or(cpu, val);
    return 7;
}

// 0xCA: JP Z, nn
int z80_op_jp_z_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_Z) {
        cpu->pc = target;
    }
    return 10;
}

// 0xE2: JP PO, nn (Jump if Parity Odd / Overflow flag is NOT set)
int z80_op_jp_po_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_PV)) {
        cpu->pc = target;
    }
    return 10;
}

// 0xD2: JP NC, nn
int z80_op_jp_nc_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_C)) {
        cpu->pc = target;
    }
    return 10;
}

// 0xEA: JP PE, nn
int z80_op_jp_pe_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_PV) {
        cpu->pc = target;
    }
    return 10;
}

// 0xF2: JP P, nn
int z80_op_jp_p_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_S)) {
        cpu->pc = target;
    }
    return 10;
}

// 0xFA: JP M, nn
int z80_op_jp_m_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_S) {
        cpu->pc = target;
    }
    return 10;
}

// 0x18: JR d (unconditional relative jump)
int z80_op_jr_d(Z80 *cpu, uint8_t *ram) {
    int8_t d = (int8_t)fetch_byte(cpu, ram);
    cpu->pc += d;
    return 12;
}

// 0x20: JR NZ, d
int z80_op_jr_nz_d(Z80 *cpu, uint8_t *ram) {
    int8_t d = (int8_t)fetch_byte(cpu, ram);
    if (!(cpu->f & FLAG_Z)) {
        cpu->pc += d;
        return 12;
    }
    return 7;
}

// 0x28: JR Z, d
int z80_op_jr_z_d(Z80 *cpu, uint8_t *ram) {
    int8_t d = (int8_t)fetch_byte(cpu, ram);
    if (cpu->f & FLAG_Z) {
        cpu->pc += d;
        return 12;
    }
    return 7;
}

// 0x38: JR C, d
int z80_op_jr_c_d(Z80 *cpu, uint8_t *ram) {
    int8_t d = (int8_t)fetch_byte(cpu, ram);
    if (cpu->f & FLAG_C) {
        cpu->pc += d;
        return 12;
    }
    return 7;
}

// next block

// 0x32: LD (nn), A
int z80_op_ld_mem_nn_a(Z80 *cpu, uint8_t *ram) {
    uint16_t addr = fetch_word(cpu, ram);
    z80_write_byte(cpu, addr, cpu->a);
    return 13;
}

// 0x36: LD (HL), n
int z80_op_ld_hl_mem_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_write_byte(cpu, cpu->hl, val);
    return 10;
}

// 0x16: LD D, n
int z80_op_ld_d_n(Z80 *cpu, uint8_t *ram) {
    cpu->d = fetch_byte(cpu, ram);
    return 7;
}

// 0x14: INC D
int z80_op_inc_d(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->d = z80_alu_inc(cpu, cpu->d);
    return 4;
}

// 0xA1: AND C
int z80_op_and_c(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_and(cpu, cpu->c);
    return 4;
}

// --- 8-Bit Register Loads ---

// 0x54: LD D, H
int z80_op_ld_d_h(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->d = cpu->h;
    return 4;
}

// 0x5D: LD E, L
int z80_op_ld_e_l(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->e = cpu->l;
    return 4;
}

// 0x5E: LD E, (HL)
int z80_op_ld_e_hl_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->e = z80_read_byte(cpu, cpu->hl);
    return 7;
}

// 0x65: LD H, L
int z80_op_ld_h_l(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->h = cpu->l;
    return 4;
}

// 0x66: LD H, (HL)
int z80_op_ld_h_hl_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->h = z80_read_byte(cpu, cpu->hl);
    return 7;
}

// 0x6F: LD L, A
int z80_op_ld_l_a(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->l = cpu->a;
    return 4;
}

// 0x79: LD A, C
int z80_op_ld_a_c(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = cpu->c;
    return 4;
}

// 0x7B: LD A, E
int z80_op_ld_a_e(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = cpu->e;
    return 4;
}

// Dynamic handler for LD r, r' (0x40 - 0x7F)
int z80_op_ld_r_r(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t opcode = z80_read_byte(cpu, cpu->pc - 1);
    
    if (opcode == 0x76) {
        // HALT instruction special case
        cpu->pc--; // Keep PC at HALT
        return 4;
    }

    uint8_t src_idx = opcode & 0x07;        // Bits 0-2
    uint8_t dst_idx = (opcode >> 3) & 0x07; // Bits 3-5

    uint8_t val = get_cb_reg(cpu, src_idx); // Uses our register index reader!
    set_cb_reg(cpu, dst_idx, val);          // Uses our register index writer!

    return (src_idx == 6 || dst_idx == 6) ? 7 : 4; // Memory ops take 7 cycles
}

// next batch

// 0x07: RLCA (Rotate A Left Circular)
int z80_op_rlca(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t carry = (cpu->a & 0x80) >> 7;
    cpu->a = (cpu->a << 1) | carry;

    // Retain S, Z, P/V; clear H, N; update C
    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_PV);
    if (carry) flags |= FLAG_C;
    flags |= (cpu->a & (FLAG_X | FLAG_Y)); // Copy bits 3 and 5

    cpu->f = flags;
    return 4;
}

// 0x0F: RRCA (Rotate A Right Circular)
int z80_op_rrca(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t carry = cpu->a & 0x01;
    cpu->a = (cpu->a >> 1) | (carry << 7);

    // Retain S, Z, P/V; clear H, N; update C
    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_PV);
    if (carry) flags |= FLAG_C;
    flags |= (cpu->a & (FLAG_X | FLAG_Y)); // Copy bits 3 and 5

    cpu->f = flags;
    return 4;
}

// 0x17: RLA (Rotate A Left through Carry)
int z80_op_rla(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t old_carry = (cpu->f & FLAG_C) ? 1 : 0;
    uint8_t new_carry = (cpu->a & 0x80) >> 7;
    cpu->a = (cpu->a << 1) | old_carry;

    // Retain S, Z, P/V; clear H, N; update C
    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_PV);
    if (new_carry) flags |= FLAG_C;
    flags |= (cpu->a & (FLAG_X | FLAG_Y)); // Copy bits 3 and 5

    cpu->f = flags;
    return 4;
}

// 0x1F: RRA (Rotate A Right through Carry)
int z80_op_rra(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t old_carry = (cpu->f & FLAG_C) ? 0x80 : 0;
    uint8_t new_carry = cpu->a & 0x01;
    cpu->a = (cpu->a >> 1) | old_carry;

    // Retain S, Z, P/V; clear H, N; update C
    uint8_t flags = cpu->f & (FLAG_S | FLAG_Z | FLAG_PV);
    if (new_carry) flags |= FLAG_C;
    flags |= (cpu->a & (FLAG_X | FLAG_Y)); // Copy bits 3 and 5

    cpu->f = flags;
    return 4;
}

// 0x1C: INC E
int z80_op_inc_e(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->e = z80_alu_inc(cpu, cpu->e);
    return 4;
}

// 0x26: LD H, n
int z80_op_ld_h_n(Z80 *cpu, uint8_t *ram) {
    cpu->h = fetch_byte(cpu, ram);
    return 7;
}

// 0x2E: LD L, n
int z80_op_ld_l_n(Z80 *cpu, uint8_t *ram) {
    cpu->l = fetch_byte(cpu, ram);
    return 7;
}

// 0x3C: INC A
int z80_op_inc_a(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = z80_alu_inc(cpu, cpu->a);
    return 4;
}

// 0x24: INC H
int z80_op_inc_h(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->h = z80_alu_inc(cpu, cpu->h);
    return 4;
}

// 0x25: DEC H
int z80_op_dec_h(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->h = z80_alu_dec(cpu, cpu->h);
    return 4;
}

// 0x2D: DEC L
int z80_op_dec_l(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->l = z80_alu_dec(cpu, cpu->l);
    return 4;
}

// 0x35: DEC (HL)
int z80_op_dec_hl_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint16_t addr = cpu->hl;
    uint8_t val = z80_read_byte(cpu, addr);
    val = z80_alu_dec(cpu, val);
    z80_write_byte(cpu, addr, val);
    return 11;
}

// 0x3D: DEC A
int z80_op_dec_a(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = z80_alu_dec(cpu, cpu->a);
    return 4;
}

// 0xC2: JP NZ, nn
int z80_op_jp_nz_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_Z)) {
        cpu->pc = target;
    }
    return 10;
}

// 0xE6: AND n
int z80_op_and_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_and(cpu, val);
    return 7;
}

// 0xCE: ADC A, n
int z80_op_adc_a_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_adc(cpu, val);
    return 7;
}

// 0xD6: SUB n
int z80_op_sub_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_sub(cpu, val);
    return 7;
}

// 0xDE: SBC A, n
int z80_op_sbc_a_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_sbc(cpu, val);
    return 7;
}

// 0xEE: XOR n
int z80_op_xor_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_xor(cpu, val);
    return 7;
}

// 0xF6: OR n
int z80_op_or_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_or(cpu, val);
    return 7;
}

// 0xF8: RET M (Return if Minus / Sign flag set)
int z80_op_ret_m(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (cpu->f & FLAG_S) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

/*
// 0xFE: CP n
int z80_op_cp_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_cp(cpu, val);
    return 7;
}
*/

// 0x89: ADC A, C
int z80_op_adc_a_c(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_adc(cpu, cpu->c);
    return 4;
}

// 0xAD: XOR L
int z80_op_xor_l(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_alu_xor(cpu, cpu->l);
    return 4;
}

// 0xBE: CP (HL)
int z80_op_cp_hl_mem(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t val = z80_read_byte(cpu, cpu->hl);
    z80_alu_cp(cpu, val);
    return 7;
}

// 0xC4: CALL NZ, nn
int z80_op_call_nz_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_Z)) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// 0xDC: CALL C, nn
int z80_op_call_c_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_C) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// 0xCC: CALL Z, nn
int z80_op_call_z_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_Z) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// 0xD4: CALL NC, nn
int z80_op_call_nc_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_C)) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// 0xE4: CALL PO, nn
int z80_op_call_po_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_PV)) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// 0xEC: CALL PE, nn
int z80_op_call_pe_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_PV) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// 0xF4: CALL P, nn
int z80_op_call_p_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (!(cpu->f & FLAG_S)) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// 0xFC: CALL M, nn
int z80_op_call_m_nn(Z80 *cpu, uint8_t *ram) {
    uint16_t target = fetch_word(cpu, ram);
    if (cpu->f & FLAG_S) {
        z80_push16(cpu, cpu->pc);
        cpu->pc = target;
        return 17;
    }
    return 10;
}

// Dynamic handler for 8-bit ALU operations (0x80 - 0xBF)
int z80_op_alu_group(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t opcode = z80_read_byte(cpu, cpu->pc - 1);
    
    uint8_t src_idx = opcode & 0x07;        // Bits 0-2 (Source register)
    uint8_t operation = (opcode >> 3) & 0x07; // Bits 3-5 (ALU Operation)

    uint8_t val = get_cb_reg(cpu, src_idx);

    switch (operation) {
        case 0: z80_alu_add(cpu, val); break; // ADD A, r
        case 1: z80_alu_adc(cpu, val); break; // ADC A, r
        case 2: z80_alu_sub(cpu, val); break; // SUB r
        case 3: z80_alu_sbc(cpu, val); break; // SBC A, r
        case 4: z80_alu_and(cpu, val); break; // AND r
        case 5: z80_alu_xor(cpu, val); break; // XOR r
        case 6: z80_alu_or(cpu, val);  break; // OR r
        case 7: z80_alu_cp(cpu, val);  break; // CP r
    }

    return (src_idx == 6) ? 7 : 4; // Memory ops (HL) take 7 cycles, registers take 4
}

/// next

// 0xF3: DI (Disable Interrupts)
int z80_op_di(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->iff1 = 0;
    cpu->iff2 = 0;
    return 4;
}

// 0xFB: EI (Enable Interrupts)
int z80_op_ei(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->iff1 = 1;
    cpu->iff2 = 1;
    return 4;
}

// 0x31: LD SP, nn
int z80_op_ld_sp_nn(Z80 *cpu, uint8_t *ram) {
    cpu->sp = fetch_word(cpu, ram);
    return 10;
}

// 0x22: LD (nn), HL
int z80_op_ld_mem_nn_hl(Z80 *cpu, uint8_t *ram) {
    uint16_t addr = fetch_word(cpu, ram);
    z80_write_byte(cpu, addr, cpu->l);     // Low byte first
    z80_write_byte(cpu, addr + 1, cpu->h); // High byte second
    return 16;
}

// 0x73: LD (HL), E
int z80_op_ld_hl_mem_e(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    z80_write_byte(cpu, cpu->hl, cpu->e);
    return 7;
}

/*
// 0x7B: LD A, E
int z80_op_ld_a_e(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->a = cpu->e;
    return 4;
}
*/

// 0x0D: DEC C
int z80_op_dec_c(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->c = z80_alu_dec(cpu, cpu->c);
    return 4;
}

//////

// 0x10: DJNZ d (Decrement B and Jump Relative if B != 0)
int z80_op_djnz_d(Z80 *cpu, uint8_t *ram) {
    int8_t d = (int8_t)fetch_byte(cpu, ram);
    cpu->b--;
    if (cpu->b != 0) {
        cpu->pc += d;
        return 13; // T-states when jump is taken
    }
    return 8;     // T-states when jump is not taken
}

// 0x30: JR NC, d (Jump Relative if Carry is 0)
int z80_op_jr_nc_d(Z80 *cpu, uint8_t *ram) {
    int8_t d = (int8_t)fetch_byte(cpu, ram);
    if (!(cpu->f & FLAG_C)) {
        cpu->pc += d;
        return 12;
    }
    return 7;
}

// 0x15: DEC D
int z80_op_dec_d(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->d = z80_alu_dec(cpu, cpu->d);
    return 4;
}

// 0x2C: INC L
int z80_op_inc_l(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    cpu->l = z80_alu_inc(cpu, cpu->l);
    return 4;
}

/*
// 0xC6: ADD A, n
int z80_op_add_a_n(Z80 *cpu, uint8_t *ram) {
    uint8_t val = fetch_byte(cpu, ram);
    z80_alu_add(cpu, val);
    return 7;
}
*/

// 0xD3: OUT (n), A (Output A to port n)
int z80_op_out_n_a(Z80 *cpu, uint8_t *ram) {
    uint8_t port = fetch_byte(cpu, ram);
    z80_io_out(cpu, port, cpu->a);
    return 11;
}

// 0xDB: IN A, (n) (Input from port n into A)
int z80_op_in_a_n(Z80 *cpu, uint8_t *ram) {
    uint8_t port = fetch_byte(cpu, ram);
    cpu->a = z80_io_in(cpu, port);
    return 11;
}

// 0xF0: RET P (Return if Plus / Positive - Sign flag NOT set)
int z80_op_ret_p(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    if (!(cpu->f & FLAG_S)) {
        cpu->pc = z80_pop16(cpu);
        return 11;
    }
    return 5;
}

// RST handler for all restart instructions
int z80_op_rst_dispatch(Z80 *cpu, uint8_t *ram) {
    (void)ram;
    uint8_t opcode = z80_read_byte(cpu, cpu->pc - 1);
    uint8_t p = opcode & 0x38; // Extract bits 3-5 to get restart address (0x00, 0x08, etc.)
    z80_push16(cpu, cpu->pc);
    cpu->pc = (uint16_t)p;
    return 11;
}







void z80_init_tables(void) {
    // Fill all 256 slots with the fallback handler first
    for (int i = 0; i < 256; i++) {
        main_opcode_table[i] = z80_op_unimplemented;
    }

    // === Basic Operations ===
    main_opcode_table[0x00] = z80_op_nop;
    main_opcode_table[0xC9] = z80_op_ret;
    main_opcode_table[0x76] = z80_op_ld_r_r; // HALT special case

    // === Control Flow ===
    main_opcode_table[0xC3] = z80_op_jp_nn;
    main_opcode_table[0xE9] = z80_op_jp_hl;
    main_opcode_table[0xCD] = z80_op_call_nn;
    main_opcode_table[0xC2] = z80_op_jp_nz_nn;
    main_opcode_table[0xCA] = z80_op_jp_z_nn;
    main_opcode_table[0xD2] = z80_op_jp_nc_nn;
    main_opcode_table[0xDA] = z80_op_jp_c_nn;
    main_opcode_table[0xE2] = z80_op_jp_po_nn;
    main_opcode_table[0xEA] = z80_op_jp_pe_nn;
    main_opcode_table[0xF2] = z80_op_jp_p_nn;
    main_opcode_table[0xFA] = z80_op_jp_m_nn;
    main_opcode_table[0xC4] = z80_op_call_nz_nn;
    main_opcode_table[0xCC] = z80_op_call_z_nn;
    main_opcode_table[0xD4] = z80_op_call_nc_nn;
    main_opcode_table[0xDC] = z80_op_call_c_nn;
    main_opcode_table[0xE4] = z80_op_call_po_nn;
    main_opcode_table[0xEC] = z80_op_call_pe_nn;
    main_opcode_table[0xF4] = z80_op_call_p_nn;
    main_opcode_table[0xFC] = z80_op_call_m_nn;
    main_opcode_table[0xC0] = z80_op_ret_nz;
    main_opcode_table[0xC8] = z80_op_ret_z;
    main_opcode_table[0xD0] = z80_op_ret_nc;
    main_opcode_table[0xD8] = z80_op_ret_c;
    main_opcode_table[0xE0] = z80_op_ret_po;
    main_opcode_table[0xE8] = z80_op_ret_pe;
    main_opcode_table[0x18] = z80_op_jr_d;
    main_opcode_table[0x20] = z80_op_jr_nz_d;
    main_opcode_table[0x28] = z80_op_jr_z_d;
    main_opcode_table[0x30] = z80_op_jr_nc_d;
    main_opcode_table[0x38] = z80_op_jr_c_d;
    main_opcode_table[0x10] = z80_op_djnz_d;

    // === 16-bit Loads ===
    main_opcode_table[0x01] = z80_op_ld_bc_nn;
    main_opcode_table[0x11] = z80_op_ld_de_nn;
    main_opcode_table[0x21] = z80_op_ld_hl_nn;
    main_opcode_table[0x31] = z80_op_ld_sp_nn;
    main_opcode_table[0x2A] = z80_op_ld_hl_mem_nn;
    main_opcode_table[0x22] = z80_op_ld_mem_nn_hl;
    main_opcode_table[0x3A] = z80_op_ld_a_mem_nn;
    main_opcode_table[0x32] = z80_op_ld_mem_nn_a;
    main_opcode_table[0xF9] = z80_op_ld_sp_hl;

    // === 8-bit Loads ===
    main_opcode_table[0x3E] = z80_op_ld_a_n;
    main_opcode_table[0x06] = z80_op_ld_b_n;
    main_opcode_table[0x0E] = z80_op_ld_c_n;
    main_opcode_table[0x16] = z80_op_ld_d_n;
    main_opcode_table[0x1E] = z80_op_ld_e_n;
    main_opcode_table[0x26] = z80_op_ld_h_n;
    main_opcode_table[0x2E] = z80_op_ld_l_n;
    main_opcode_table[0x36] = z80_op_ld_hl_mem_n;
    main_opcode_table[0x7E] = z80_op_ld_a_hl_mem;
    main_opcode_table[0x1A] = z80_op_ld_a_de_mem;
    main_opcode_table[0x12] = z80_op_ld_de_mem_a;
    main_opcode_table[0x0A] = z80_op_ld_a_bc_mem;
    main_opcode_table[0x02] = z80_op_ld_bc_mem_a;
    main_opcode_table[0x5E] = z80_op_ld_e_hl_mem;

    // === Dynamic LD r, r' (0x40-0x7F) ===
    // This covers all register-to-register loads in one loop
    for (int i = 0x40; i <= 0x7F; i++) {
        main_opcode_table[i] = z80_op_ld_r_r;
    }

    // === 16-bit Arithmetic ===
    main_opcode_table[0x09] = z80_op_add_hl_bc;
    main_opcode_table[0x19] = z80_op_add_hl_de;
    main_opcode_table[0x29] = z80_op_add_hl_hl;
    main_opcode_table[0x39] = z80_op_add_hl_sp;

    // === 16-bit Inc/Dec ===
    main_opcode_table[0x03] = z80_op_inc_bc;
    main_opcode_table[0x0B] = z80_op_dec_bc;
    main_opcode_table[0x13] = z80_op_inc_de;
    main_opcode_table[0x1B] = z80_op_dec_de;
    main_opcode_table[0x23] = z80_op_inc_hl;
    main_opcode_table[0x2B] = z80_op_dec_hl;
    main_opcode_table[0x33] = z80_op_inc_sp;
    main_opcode_table[0x3B] = z80_op_dec_sp;

    // === 8-bit Inc/Dec ===
    main_opcode_table[0x04] = z80_op_inc_b;
    main_opcode_table[0x05] = z80_op_dec_b;
    main_opcode_table[0x0C] = z80_op_inc_c;
    main_opcode_table[0x0D] = z80_op_dec_c;
    main_opcode_table[0x14] = z80_op_inc_d;
    main_opcode_table[0x15] = z80_op_dec_d;
    main_opcode_table[0x1C] = z80_op_inc_e;
    main_opcode_table[0x1D] = z80_op_dec_e;
    main_opcode_table[0x24] = z80_op_inc_h;
    main_opcode_table[0x25] = z80_op_dec_h;
    main_opcode_table[0x2C] = z80_op_inc_l;
    main_opcode_table[0x2D] = z80_op_dec_l;
    main_opcode_table[0x3C] = z80_op_inc_a;
    main_opcode_table[0x3D] = z80_op_dec_a;
    main_opcode_table[0x34] = z80_op_inc_hl_mem;
    main_opcode_table[0x35] = z80_op_dec_hl_mem;

    // === 8-bit ALU Operations (0x80-0xBF) ===
    // Covers ADD, ADC, SUB, SBC, AND, XOR, OR, CP with all registers
    for (int i = 0x80; i <= 0xBF; i++) {
        main_opcode_table[i] = z80_op_alu_group;
    }

    // === ALU Immediate ===
    main_opcode_table[0xC6] = z80_op_add_a_n;
    main_opcode_table[0xFE] = z80_op_cp_n;
    main_opcode_table[0xE6] = z80_op_and_n;
    main_opcode_table[0xCE] = z80_op_adc_a_n;
    main_opcode_table[0xD6] = z80_op_sub_n;
    main_opcode_table[0xDE] = z80_op_sbc_a_n;
    main_opcode_table[0xEE] = z80_op_xor_n;
    main_opcode_table[0xF6] = z80_op_or_n;

    // === Special Instructions ===
    main_opcode_table[0x27] = z80_op_daa;
    main_opcode_table[0x2F] = z80_op_cpl;
    main_opcode_table[0x37] = z80_op_scf;
    main_opcode_table[0x3F] = z80_op_ccf;

    // === Rotations ===
    main_opcode_table[0x07] = z80_op_rlca;
    main_opcode_table[0x0F] = z80_op_rrca;
    main_opcode_table[0x17] = z80_op_rla;
    main_opcode_table[0x1F] = z80_op_rra;

    // === Exchanges ===
    main_opcode_table[0x08] = z80_op_ex_af_af;
    main_opcode_table[0xEB] = z80_op_ex_de_hl;
    main_opcode_table[0xD9] = z80_op_exx;
    main_opcode_table[0xE3] = z80_op_ex_sp_mem_hl;

    // === Stack Operations ===
    main_opcode_table[0xC5] = z80_op_push_bc;
    main_opcode_table[0xD5] = z80_op_push_de;
    main_opcode_table[0xE5] = z80_op_push_hl;
    main_opcode_table[0xF5] = z80_op_push_af;
    main_opcode_table[0xC1] = z80_op_pop_bc;
    main_opcode_table[0xD1] = z80_op_pop_de;
    main_opcode_table[0xE1] = z80_op_pop_hl;
    main_opcode_table[0xF1] = z80_op_pop_af;


    // === Miscellaneous ===
    main_opcode_table[0xA1] = z80_op_and_c;
    main_opcode_table[0xB6] = z80_op_or_hl_mem;
    main_opcode_table[0xD3] = z80_op_out_n_a;
    main_opcode_table[0xDB] = z80_op_in_a_n;
    main_opcode_table[0xF3] = z80_op_di;
    main_opcode_table[0xFB] = z80_op_ei;

    // === Conditional Returns ===
    main_opcode_table[0xF8] = z80_op_ret_m;
    main_opcode_table[0xF0] = z80_op_ret_p;

    // === Restart Instructions (RST p) ===
    main_opcode_table[0xC7] = z80_op_rst_dispatch; // RST 00h
    main_opcode_table[0xCF] = z80_op_rst_dispatch; // RST 08h
    main_opcode_table[0xD7] = z80_op_rst_dispatch; // RST 10h
    main_opcode_table[0xDF] = z80_op_rst_dispatch; // RST 18h
    main_opcode_table[0xE7] = z80_op_rst_dispatch; // RST 20h
    main_opcode_table[0xEF] = z80_op_rst_dispatch; // RST 28h
    main_opcode_table[0xF7] = z80_op_rst_dispatch; // RST 30h
    main_opcode_table[0xFF] = z80_op_rst_dispatch; // RST 38h

    // === Prefix Opcodes ===
    main_opcode_table[0xCB] = z80_op_prefix_cb;
    main_opcode_table[0xED] = z80_op_prefix_ed;
    main_opcode_table[0xDD] = z80_op_prefix_dd;
    main_opcode_table[0xFD] = z80_op_prefix_fd;
}


// z80.c

int z80_step(Z80 *cpu, uint8_t *ram) {
    // 1. Intercept CP/M BDOS/BIOS calls before fetching
    check_cpm_bdos(cpu, ram);
    check_cpm_bios(cpu, ram);

    // BDOS function 0 (P_TERMCPM) and the BIOS WBOOT vector both set PC to
    // 0x0000 directly rather than returning to the caller - main.c's run
    // loop checks for PC==0x0000 at the top of its *next* iteration, but
    // without this check here, this function would immediately fetch and
    // execute the JP-to-WBOOT main.c preloads at address 0, undoing the
    // termination before that check ever runs.
    if (cpu->pc == 0x0000) return 0;

    // 2. Fetch opcode byte
    uint8_t opcode = fetch_byte(cpu, ram);

    // 3. Increment memory refresh register R (Bits 0-6 cycle)
    cpu->r = (cpu->r & 0x80) | ((cpu->r + 1) & 0x7F);

    // 4. Decode & Execute via table lookup
    return main_opcode_table[opcode](cpu, ram);
}

