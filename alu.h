#ifndef _ALU_H
#define _ALU_H

#include "z80.h"

#define FLAG_C  (1 << 0) // Carry
#define FLAG_N  (1 << 1) // Add/Subtract
#define FLAG_PV (1 << 2) // Parity / Overflow
#define FLAG_X  (1 << 3) // Undocumented bit 3
#define FLAG_H  (1 << 4) // Half-Carry
#define FLAG_Y  (1 << 5) // Undocumented bit 5
#define FLAG_Z  (1 << 6) // Zero
#define FLAG_S  (1 << 7) // Sign

uint8_t calculate_parity(uint8_t val);

void z80_alu_add(Z80 *cpu, uint8_t val);
void z80_alu_adc(Z80 *cpu, uint8_t val);
void z80_alu_sub(Z80 *cpu, uint8_t val);
void z80_alu_sbc(Z80 *cpu, uint8_t val);
void z80_alu_cp(Z80 *cpu, uint8_t val);
void z80_alu_and(Z80 *cpu, uint8_t val);
void z80_alu_xor(Z80 *cpu, uint8_t val);
void z80_alu_or(Z80 *cpu, uint8_t val);
// single reg op
uint8_t z80_alu_inc(Z80 *cpu, uint8_t val);
uint8_t z80_alu_dec(Z80 *cpu, uint8_t val);
void z80_alu_add16(Z80 *cpu, uint16_t val);
// CB ops
void z80_alu_bit(Z80 *cpu, uint8_t bit, uint8_t val);
uint8_t z80_alu_set(uint8_t bit, uint8_t val);
uint8_t z80_alu_res(uint8_t bit, uint8_t val);
uint8_t z80_alu_rlc(Z80 *cpu, uint8_t val);
uint8_t z80_alu_rrc(Z80 *cpu, uint8_t val);
uint8_t z80_alu_rl(Z80 *cpu, uint8_t val);
uint8_t z80_alu_rr(Z80 *cpu, uint8_t val);
uint8_t z80_alu_sla(Z80 *cpu, uint8_t val);
uint8_t z80_alu_sll(Z80 *cpu, uint8_t val);
uint8_t z80_alu_sra(Z80 *cpu, uint8_t val);
uint8_t z80_alu_srl(Z80 *cpu, uint8_t val);
// EB
int z80_alu_block_transfer(Z80 *cpu, int increment, bool repeat);
int z80_alu_block_search(Z80 *cpu, int increment, bool repeat);
// IX/IY
void z80_alu_add16_idx(Z80 *cpu, uint16_t *index_reg, uint16_t val);


#endif
