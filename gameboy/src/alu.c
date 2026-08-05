#include "alu.h"

// Every flag computation here is grounded against the opcode table
// fetched from https://github.com/lmmendes/game-boy-opcodes (verified
// against a full 512-entry dump, cross-checked bit-for-bit against the
// gaps in the unprefixed table matching the 11 real invalid SM83
// opcodes) during this phase - see docs/GAMEBOY_ROADMAP.md. Nothing
// here is carried over from emu/src/alu.c's Z80 flag math; the SM83's
// F register only has Z/N/H/C (no X/Y undocumented bits), and several
// of these operations (DAA, ADD SP,e8/LD HL,SP+e8, the accumulator-vs-
// CB rotate distinction) have real, confirmed-different behavior from
// their Z80 namesakes.

void gb_alu_add(GBCpu *cpu, uint8_t val) {
    uint16_t result = (uint16_t)cpu->a + val;
    uint8_t f = 0;
    if ((result & 0xFF) == 0) f |= GB_FLAG_Z;
    if (((cpu->a & 0x0F) + (val & 0x0F)) > 0x0F) f |= GB_FLAG_H;
    if (result > 0xFF) f |= GB_FLAG_C;
    cpu->a = (uint8_t)result;
    cpu->f = f;
}

void gb_alu_adc(GBCpu *cpu, uint8_t val) {
    uint8_t carry = (cpu->f & GB_FLAG_C) ? 1 : 0;
    uint16_t result = (uint16_t)cpu->a + val + carry;
    uint8_t f = 0;
    if ((result & 0xFF) == 0) f |= GB_FLAG_Z;
    if (((cpu->a & 0x0F) + (val & 0x0F) + carry) > 0x0F) f |= GB_FLAG_H;
    if (result > 0xFF) f |= GB_FLAG_C;
    cpu->a = (uint8_t)result;
    cpu->f = f;
}

void gb_alu_sub(GBCpu *cpu, uint8_t val) {
    int result = (int)cpu->a - val;
    uint8_t f = GB_FLAG_N;
    if ((result & 0xFF) == 0) f |= GB_FLAG_Z;
    if ((cpu->a & 0x0F) < (val & 0x0F)) f |= GB_FLAG_H;
    if (cpu->a < val) f |= GB_FLAG_C;
    cpu->a = (uint8_t)result;
    cpu->f = f;
}

void gb_alu_sbc(GBCpu *cpu, uint8_t val) {
    uint8_t carry = (cpu->f & GB_FLAG_C) ? 1 : 0;
    int result = (int)cpu->a - val - carry;
    uint8_t f = GB_FLAG_N;
    if ((result & 0xFF) == 0) f |= GB_FLAG_Z;
    if (((int)(cpu->a & 0x0F) - (val & 0x0F) - carry) < 0) f |= GB_FLAG_H;
    if (result < 0) f |= GB_FLAG_C;
    cpu->a = (uint8_t)result;
    cpu->f = f;
}

void gb_alu_and(GBCpu *cpu, uint8_t val) {
    cpu->a &= val;
    cpu->f = GB_FLAG_H | (cpu->a == 0 ? GB_FLAG_Z : 0);
}

void gb_alu_xor(GBCpu *cpu, uint8_t val) {
    cpu->a ^= val;
    cpu->f = (cpu->a == 0) ? GB_FLAG_Z : 0;
}

void gb_alu_or(GBCpu *cpu, uint8_t val) {
    cpu->a |= val;
    cpu->f = (cpu->a == 0) ? GB_FLAG_Z : 0;
}

void gb_alu_cp(GBCpu *cpu, uint8_t val) {
    uint8_t a = cpu->a;
    gb_alu_sub(cpu, val);
    cpu->a = a; // CP computes flags only - A itself is unchanged
}

uint8_t gb_alu_inc(GBCpu *cpu, uint8_t val) {
    uint8_t result = (uint8_t)(val + 1);
    uint8_t f = cpu->f & GB_FLAG_C; // C is untouched by INC
    if (result == 0) f |= GB_FLAG_Z;
    if ((val & 0x0F) == 0x0F) f |= GB_FLAG_H;
    cpu->f = f;
    return result;
}

uint8_t gb_alu_dec(GBCpu *cpu, uint8_t val) {
    uint8_t result = (uint8_t)(val - 1);
    uint8_t f = (cpu->f & GB_FLAG_C) | GB_FLAG_N; // C is untouched by DEC
    if (result == 0) f |= GB_FLAG_Z;
    if ((val & 0x0F) == 0x00) f |= GB_FLAG_H;
    cpu->f = f;
    return result;
}

void gb_alu_add_hl(GBCpu *cpu, uint16_t val) {
    uint32_t result = (uint32_t)cpu->hl + val;
    uint8_t f = cpu->f & GB_FLAG_Z; // Z is untouched by 16-bit ADD HL,rr
    if (((cpu->hl & 0x0FFF) + (val & 0x0FFF)) > 0x0FFF) f |= GB_FLAG_H;
    if (result > 0xFFFF) f |= GB_FLAG_C;
    cpu->hl = (uint16_t)result;
    cpu->f = f;
}

// ADD SP,e8 and LD HL,SP+e8 both compute H/C from SP's *low byte* added
// to e8 reinterpreted as unsigned - not from the signed 16-bit result -
// a real, confirmed SM83 quirk (opcodes 0xe8/0xf8 both list flags
// 0,0,H,C: Z is always cleared even if the signed result is 0).
uint16_t gb_alu_add_sp_e8(GBCpu *cpu, int8_t e8) {
    uint16_t sp = cpu->sp;
    uint8_t e8u = (uint8_t)e8;
    uint16_t result = (uint16_t)(sp + (int16_t)e8);
    uint8_t f = 0;
    if (((sp & 0x0F) + (e8u & 0x0F)) > 0x0F) f |= GB_FLAG_H;
    if (((sp & 0xFF) + e8u) > 0xFF) f |= GB_FLAG_C;
    cpu->f = f;
    return result;
}

// The canonical SM83 DAA algorithm (cross-referenced against multiple
// independent open-source implementations that all agree bit-for-bit,
// and validated against Blargg's cpu_instrs test ROM #01 - see
// docs/GAMEBOY_ROADMAP.md's Status section): correction depends on N
// (were we adding or subtracting?) and on H/C from the *previous*
// arithmetic op, not on freshly recomputing them from A.
void gb_alu_daa(GBCpu *cpu) {
    uint8_t a = cpu->a;
    int n = (cpu->f & GB_FLAG_N) != 0;
    int h = (cpu->f & GB_FLAG_H) != 0;
    int c = (cpu->f & GB_FLAG_C) != 0;
    uint8_t correction = 0;
    int set_c = 0;

    if (h || (!n && (a & 0x0F) > 0x09)) {
        correction |= 0x06;
    }
    if (c || (!n && a > 0x99)) {
        correction |= 0x60;
        set_c = 1;
    }

    a = n ? (uint8_t)(a - correction) : (uint8_t)(a + correction);

    uint8_t f = cpu->f & GB_FLAG_N; // N is untouched; H is always cleared
    if (a == 0) f |= GB_FLAG_Z;
    if (set_c) f |= GB_FLAG_C;
    cpu->a = a;
    cpu->f = f;
}

void gb_alu_cpl(GBCpu *cpu) {
    cpu->a = (uint8_t)~cpu->a;
    cpu->f |= (GB_FLAG_N | GB_FLAG_H); // Z, C untouched
}

void gb_alu_scf(GBCpu *cpu) {
    cpu->f = (cpu->f & GB_FLAG_Z) | GB_FLAG_C;
}

void gb_alu_ccf(GBCpu *cpu) {
    uint8_t new_c = (cpu->f & GB_FLAG_C) ? 0 : GB_FLAG_C;
    cpu->f = (cpu->f & GB_FLAG_Z) | new_c;
}

// Accumulator rotates (RLCA/RRCA/RLA/RRA): Z is *always* cleared here,
// unlike the CB-prefixed forms below - a real hardware distinction
// (confirmed: opcodes 0x07/0x0f/0x17/0x1f all list flags 0,0,0,C).
uint8_t gb_alu_rlca(GBCpu *cpu, uint8_t val) {
    uint8_t carry = (val & 0x80) >> 7;
    uint8_t result = (uint8_t)((val << 1) | carry);
    cpu->f = carry ? GB_FLAG_C : 0;
    return result;
}

uint8_t gb_alu_rrca(GBCpu *cpu, uint8_t val) {
    uint8_t carry = val & 0x01;
    uint8_t result = (uint8_t)((val >> 1) | (carry << 7));
    cpu->f = carry ? GB_FLAG_C : 0;
    return result;
}

uint8_t gb_alu_rla(GBCpu *cpu, uint8_t val) {
    uint8_t old_carry = (cpu->f & GB_FLAG_C) ? 1 : 0;
    uint8_t new_carry = (val & 0x80) >> 7;
    uint8_t result = (uint8_t)((val << 1) | old_carry);
    cpu->f = new_carry ? GB_FLAG_C : 0;
    return result;
}

uint8_t gb_alu_rra(GBCpu *cpu, uint8_t val) {
    uint8_t old_carry = (cpu->f & GB_FLAG_C) ? 1 : 0;
    uint8_t new_carry = val & 0x01;
    uint8_t result = (uint8_t)((val >> 1) | (old_carry << 7));
    cpu->f = new_carry ? GB_FLAG_C : 0;
    return result;
}

// CB-prefixed forms reuse the accumulator math above (identical bit
// manipulation) and just OR in Z from the result - safe, since the
// accumulator forms above never set Z themselves.
uint8_t gb_alu_rlc(GBCpu *cpu, uint8_t val) {
    uint8_t result = gb_alu_rlca(cpu, val);
    if (result == 0) cpu->f |= GB_FLAG_Z;
    return result;
}

uint8_t gb_alu_rrc(GBCpu *cpu, uint8_t val) {
    uint8_t result = gb_alu_rrca(cpu, val);
    if (result == 0) cpu->f |= GB_FLAG_Z;
    return result;
}

uint8_t gb_alu_rl(GBCpu *cpu, uint8_t val) {
    uint8_t result = gb_alu_rla(cpu, val);
    if (result == 0) cpu->f |= GB_FLAG_Z;
    return result;
}

uint8_t gb_alu_rr(GBCpu *cpu, uint8_t val) {
    uint8_t result = gb_alu_rra(cpu, val);
    if (result == 0) cpu->f |= GB_FLAG_Z;
    return result;
}

uint8_t gb_alu_sla(GBCpu *cpu, uint8_t val) {
    uint8_t carry = (val & 0x80) >> 7;
    uint8_t result = (uint8_t)(val << 1);
    uint8_t f = carry ? GB_FLAG_C : 0;
    if (result == 0) f |= GB_FLAG_Z;
    cpu->f = f;
    return result;
}

// Arithmetic shift right: bit 7 (the sign bit) is preserved, not
// shifted in as 0 - distinct from SRL below.
uint8_t gb_alu_sra(GBCpu *cpu, uint8_t val) {
    uint8_t carry = val & 0x01;
    uint8_t result = (uint8_t)((val >> 1) | (val & 0x80));
    uint8_t f = carry ? GB_FLAG_C : 0;
    if (result == 0) f |= GB_FLAG_Z;
    cpu->f = f;
    return result;
}

// SWAP has no Z80 equivalent at all - it replaces the Z80's undocumented
// SLL in this exact CB slot (0x30-0x37) on the SM83.
uint8_t gb_alu_swap(GBCpu *cpu, uint8_t val) {
    uint8_t result = (uint8_t)((val << 4) | (val >> 4));
    cpu->f = (result == 0) ? GB_FLAG_Z : 0; // C is always cleared
    return result;
}

uint8_t gb_alu_srl(GBCpu *cpu, uint8_t val) {
    uint8_t carry = val & 0x01;
    uint8_t result = val >> 1; // logical shift: bit 7 becomes 0
    uint8_t f = carry ? GB_FLAG_C : 0;
    if (result == 0) f |= GB_FLAG_Z;
    cpu->f = f;
    return result;
}

void gb_alu_bit(GBCpu *cpu, uint8_t bit, uint8_t val) {
    uint8_t f = (cpu->f & GB_FLAG_C) | GB_FLAG_H; // C untouched, H set, N cleared
    if (!(val & (1 << bit))) f |= GB_FLAG_Z;
    cpu->f = f;
}

uint8_t gb_alu_set(uint8_t bit, uint8_t val) {
    return (uint8_t)(val | (1 << bit));
}

uint8_t gb_alu_res(uint8_t bit, uint8_t val) {
    return (uint8_t)(val & ~(1 << bit));
}
