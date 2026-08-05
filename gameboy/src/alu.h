#ifndef _GB_ALU_H
#define _GB_ALU_H

#include "cpu.h"

// The SM83's F register only implements the top 4 bits - the bottom 4
// are always 0 (both when read directly and, per pandocs, when POP AF
// restores F from the stack: real hardware masks the low nibble to 0
// regardless of what was pushed). No X/Y undocumented-flag bits exist
// here the way they do on a real Z80 (emu/src/alu.h's FLAG_X/FLAG_Y) -
// this is a genuine hardware difference, not an oversight.
#define GB_FLAG_C (1 << 4) // Carry
#define GB_FLAG_H (1 << 5) // Half-Carry
#define GB_FLAG_N (1 << 6) // Add/Subtract
#define GB_FLAG_Z (1 << 7) // Zero

void gb_alu_add(GBCpu *cpu, uint8_t val);
void gb_alu_adc(GBCpu *cpu, uint8_t val);
void gb_alu_sub(GBCpu *cpu, uint8_t val);
void gb_alu_sbc(GBCpu *cpu, uint8_t val);
void gb_alu_and(GBCpu *cpu, uint8_t val);
void gb_alu_xor(GBCpu *cpu, uint8_t val);
void gb_alu_or(GBCpu *cpu, uint8_t val);
void gb_alu_cp(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_inc(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_dec(GBCpu *cpu, uint8_t val);

void gb_alu_add_hl(GBCpu *cpu, uint16_t val);
// Shared by ADD SP,e8 and LD HL,SP+e8 - both compute the exact same
// result and flags from SP and a signed 8-bit immediate, differing only
// in which register receives the result (see opcodes 0xe8/0xf8 in
// docs/GAMEBOY_ROADMAP.md's grounding notes).
uint16_t gb_alu_add_sp_e8(GBCpu *cpu, int8_t e8);

void gb_alu_daa(GBCpu *cpu);
void gb_alu_cpl(GBCpu *cpu);
void gb_alu_scf(GBCpu *cpu);
void gb_alu_ccf(GBCpu *cpu);

// Accumulator forms (RLCA/RRCA/RLA/RRA): always clear Z regardless of
// the result. Distinct from the CB-prefixed forms below, which set Z
// from the result - a real hardware distinction inherited from the Z80,
// not an inconsistency to "fix".
uint8_t gb_alu_rlca(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_rrca(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_rla(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_rra(GBCpu *cpu, uint8_t val);

// CB-prefixed forms: set Z from the result.
uint8_t gb_alu_rlc(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_rrc(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_rl(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_rr(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_sla(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_sra(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_swap(GBCpu *cpu, uint8_t val);
uint8_t gb_alu_srl(GBCpu *cpu, uint8_t val);

void gb_alu_bit(GBCpu *cpu, uint8_t bit, uint8_t val);
uint8_t gb_alu_set(uint8_t bit, uint8_t val);
uint8_t gb_alu_res(uint8_t bit, uint8_t val);

#endif
