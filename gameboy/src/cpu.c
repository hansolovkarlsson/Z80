#include "cpu.h"
#include "alu.h"
#include "cart.h"
#include <stddef.h>

// Table-driven dispatch, same *shape* as emu/src/z80.c's
// main_opcode_table/z80_op_ld_r_r/z80_op_alu_group pattern - see
// docs/GAMEBOY_ROADMAP.md's "Architecture decision" for why this is an
// independent implementation rather than shared code. Every opcode's
// bytes/cycles/flags below were checked against the official gbdev.io
// opcode table (fetched during this phase - see docs/GAMEBOY_ROADMAP.md
// Status section for the exact source and the one real erratum found
// along the way: BIT b,(HL) is 12 cycles, not 16 like the read-modify-
// write CB ops, since it never writes anything back).

GBOpcodeHandler gb_opcode_table[256];

static inline uint8_t fetch_byte(GBCpu *cpu) {
    return gb_read_byte(cpu, cpu->pc++);
}

static inline uint16_t fetch_word(GBCpu *cpu) {
    uint8_t lo = fetch_byte(cpu);
    uint8_t hi = fetch_byte(cpu);
    return (uint16_t)((hi << 8) | lo);
}

static void gb_push16(GBCpu *cpu, uint16_t val) {
    cpu->sp -= 2;
    gb_write_byte(cpu, cpu->sp, val & 0xFF);
    gb_write_byte(cpu, (uint16_t)(cpu->sp + 1), val >> 8);
}

static uint16_t gb_pop16(GBCpu *cpu) {
    uint8_t lo = gb_read_byte(cpu, cpu->sp);
    uint8_t hi = gb_read_byte(cpu, (uint16_t)(cpu->sp + 1));
    cpu->sp += 2;
    return (uint16_t)((hi << 8) | lo);
}

// 8-bit register index, shared by every opcode range that encodes one:
// 0=B 1=C 2=D 3=E 4=H 5=L 6=(HL) 7=A - identical convention to the Z80's
// own get_cb_reg/set_cb_reg (emu/src/z80.c), inherited unchanged since
// this part of the encoding genuinely didn't change between the two CPUs.
static uint8_t get_reg8(GBCpu *cpu, uint8_t idx) {
    switch (idx) {
        case 0: return cpu->b;
        case 1: return cpu->c;
        case 2: return cpu->d;
        case 3: return cpu->e;
        case 4: return cpu->h;
        case 5: return cpu->l;
        case 6: return gb_read_byte(cpu, cpu->hl);
        default: return cpu->a; // 7
    }
}

static void set_reg8(GBCpu *cpu, uint8_t idx, uint8_t val) {
    switch (idx) {
        case 0: cpu->b = val; break;
        case 1: cpu->c = val; break;
        case 2: cpu->d = val; break;
        case 3: cpu->e = val; break;
        case 4: cpu->h = val; break;
        case 5: cpu->l = val; break;
        case 6: gb_write_byte(cpu, cpu->hl, val); break;
        default: cpu->a = val; break; // 7
    }
}

// 16-bit register-pair index used by LD rr,d16 / INC rr / DEC rr /
// ADD HL,rr: 0=BC 1=DE 2=HL 3=SP.
static uint16_t get_rr(GBCpu *cpu, uint8_t idx) {
    switch (idx) {
        case 0: return cpu->bc;
        case 1: return cpu->de;
        case 2: return cpu->hl;
        default: return cpu->sp; // 3
    }
}

static void set_rr(GBCpu *cpu, uint8_t idx, uint16_t val) {
    switch (idx) {
        case 0: cpu->bc = val; break;
        case 1: cpu->de = val; break;
        case 2: cpu->hl = val; break;
        default: cpu->sp = val; break; // 3
    }
}

// The PUSH/POP register-pair index is the same encoding but with AF in
// slot 3 instead of SP - a real, deliberate difference in the opcode
// map (0xF5/0xF1 push/pop AF, never SP), not an inconsistency.
static uint16_t get_rr2(GBCpu *cpu, uint8_t idx) {
    switch (idx) {
        case 0: return cpu->bc;
        case 1: return cpu->de;
        case 2: return cpu->hl;
        default: return cpu->af; // 3
    }
}

static void set_rr2(GBCpu *cpu, uint8_t idx, uint16_t val) {
    switch (idx) {
        case 0: cpu->bc = val; break;
        case 1: cpu->de = val; break;
        case 2: cpu->hl = val; break;
        default: cpu->af = val & 0xFFF0; break; // 3 - F's low nibble always reads 0
    }
}

// Condition-code index used by JR/JP/CALL/RET's conditional forms:
// 0=NZ 1=Z 2=NC 3=C.
static int check_cond(GBCpu *cpu, uint8_t idx) {
    switch (idx) {
        case 0: return !(cpu->f & GB_FLAG_Z);
        case 1: return (cpu->f & GB_FLAG_Z) != 0;
        case 2: return !(cpu->f & GB_FLAG_C);
        default: return (cpu->f & GB_FLAG_C) != 0; // 3
    }
}

static void gb_alu_dispatch(GBCpu *cpu, uint8_t op_idx, uint8_t val) {
    switch (op_idx) {
        case 0: gb_alu_add(cpu, val); break;
        case 1: gb_alu_adc(cpu, val); break;
        case 2: gb_alu_sub(cpu, val); break;
        case 3: gb_alu_sbc(cpu, val); break;
        case 4: gb_alu_and(cpu, val); break;
        case 5: gb_alu_xor(cpu, val); break;
        case 6: gb_alu_or(cpu, val); break;
        default: gb_alu_cp(cpu, val); break; // 7
    }
}

// --- Individually-named opcodes (everything that isn't one of the
// four fully regular blocks below: LD r,r; the r8 and d8 ALU groups;
// and the CB-prefixed table) ---

static int gb_op_nop(GBCpu *cpu) { (void)cpu; return 4; }

static int gb_op_ld_bc_a(GBCpu *cpu) { gb_write_byte(cpu, cpu->bc, cpu->a); return 8; }
static int gb_op_ld_de_a(GBCpu *cpu) { gb_write_byte(cpu, cpu->de, cpu->a); return 8; }
static int gb_op_ld_a_bc(GBCpu *cpu) { cpu->a = gb_read_byte(cpu, cpu->bc); return 8; }
static int gb_op_ld_a_de(GBCpu *cpu) { cpu->a = gb_read_byte(cpu, cpu->de); return 8; }

static int gb_op_ld_hli_a(GBCpu *cpu) { gb_write_byte(cpu, cpu->hl, cpu->a); cpu->hl++; return 8; }
static int gb_op_ld_hld_a(GBCpu *cpu) { gb_write_byte(cpu, cpu->hl, cpu->a); cpu->hl--; return 8; }
static int gb_op_ld_a_hli(GBCpu *cpu) { cpu->a = gb_read_byte(cpu, cpu->hl); cpu->hl++; return 8; }
static int gb_op_ld_a_hld(GBCpu *cpu) { cpu->a = gb_read_byte(cpu, cpu->hl); cpu->hl--; return 8; }

static int gb_op_ld_a16_sp(GBCpu *cpu) {
    uint16_t addr = fetch_word(cpu);
    gb_write_byte(cpu, addr, cpu->sp & 0xFF);
    gb_write_byte(cpu, (uint16_t)(addr + 1), cpu->sp >> 8);
    return 20;
}

static int gb_op_ld_a16_a(GBCpu *cpu) {
    uint16_t addr = fetch_word(cpu);
    gb_write_byte(cpu, addr, cpu->a);
    return 16;
}

static int gb_op_ld_a_a16(GBCpu *cpu) {
    uint16_t addr = fetch_word(cpu);
    cpu->a = gb_read_byte(cpu, addr);
    return 16;
}

static int gb_op_ldh_a8_a(GBCpu *cpu) {
    uint8_t off = fetch_byte(cpu);
    gb_write_byte(cpu, (uint16_t)(0xFF00 + off), cpu->a);
    return 12;
}

static int gb_op_ldh_a_a8(GBCpu *cpu) {
    uint8_t off = fetch_byte(cpu);
    cpu->a = gb_read_byte(cpu, (uint16_t)(0xFF00 + off));
    return 12;
}

static int gb_op_ldh_c_a(GBCpu *cpu) { gb_write_byte(cpu, (uint16_t)(0xFF00 + cpu->c), cpu->a); return 8; }
static int gb_op_ldh_a_c(GBCpu *cpu) { cpu->a = gb_read_byte(cpu, (uint16_t)(0xFF00 + cpu->c)); return 8; }

static int gb_op_ld_sp_hl(GBCpu *cpu) { cpu->sp = cpu->hl; return 8; }

static int gb_op_add_sp_e8(GBCpu *cpu) {
    int8_t e8 = (int8_t)fetch_byte(cpu);
    cpu->sp = gb_alu_add_sp_e8(cpu, e8);
    return 16;
}

static int gb_op_ld_hl_sp_e8(GBCpu *cpu) {
    int8_t e8 = (int8_t)fetch_byte(cpu);
    cpu->hl = gb_alu_add_sp_e8(cpu, e8);
    return 12;
}

static int gb_op_rlca(GBCpu *cpu) { cpu->a = gb_alu_rlca(cpu, cpu->a); return 4; }
static int gb_op_rrca(GBCpu *cpu) { cpu->a = gb_alu_rrca(cpu, cpu->a); return 4; }
static int gb_op_rla(GBCpu *cpu) { cpu->a = gb_alu_rla(cpu, cpu->a); return 4; }
static int gb_op_rra(GBCpu *cpu) { cpu->a = gb_alu_rra(cpu, cpu->a); return 4; }
static int gb_op_daa(GBCpu *cpu) { gb_alu_daa(cpu); return 4; }
static int gb_op_cpl(GBCpu *cpu) { gb_alu_cpl(cpu); return 4; }
static int gb_op_scf(GBCpu *cpu) { gb_alu_scf(cpu); return 4; }
static int gb_op_ccf(GBCpu *cpu) { gb_alu_ccf(cpu); return 4; }

static int gb_op_jr(GBCpu *cpu) {
    int8_t off = (int8_t)fetch_byte(cpu);
    cpu->pc = (uint16_t)(cpu->pc + off);
    return 12;
}

static int gb_op_jp(GBCpu *cpu) { cpu->pc = fetch_word(cpu); return 16; }

// Real behavior: jump straight to the value *in* HL - unlike every
// other "(HL)" operand in this table, this one is not a memory
// dereference (confirmed against the official opcode table's per-
// operand `immediate` flag - see docs/GAMEBOY_ROADMAP.md).
static int gb_op_jp_hl(GBCpu *cpu) { cpu->pc = cpu->hl; return 4; }

static int gb_op_call(GBCpu *cpu) {
    uint16_t addr = fetch_word(cpu);
    gb_push16(cpu, cpu->pc);
    cpu->pc = addr;
    return 24;
}

static int gb_op_ret(GBCpu *cpu) { cpu->pc = gb_pop16(cpu); return 16; }

// RETI sets IME immediately, unlike EI - there's no one-instruction
// delay here since, unlike EI, there's no risk of it firing before the
// interrupt handler it's returning from has even finished tidying up.
static int gb_op_reti(GBCpu *cpu) {
    cpu->pc = gb_pop16(cpu);
    cpu->ime = 1;
    return 16;
}

static int gb_op_di(GBCpu *cpu) { cpu->ime = 0; cpu->ime_pending = 0; return 4; }
static int gb_op_ei(GBCpu *cpu) { cpu->ime_pending = 1; return 4; }

// STOP is a real 2-byte instruction (opcode + a padding byte, normally
// 0x00) per the official opcode table, not the 1-byte form some older
// references list - confirmed during this phase, not guessed. Real
// hardware's full STOP behavior (low-power mode, exiting via joypad
// input) needs the interrupt/joypad controller Phase 4 will add; for
// now this just consumes both bytes and marks the state.
static int gb_op_stop(GBCpu *cpu) {
    fetch_byte(cpu);
    cpu->stopped = 1;
    return 4;
}

static int gb_op_illegal(GBCpu *cpu) { (void)cpu; return -1; }

// --- Fully regular blocks: one handler each, decoding the actual
// opcode byte out of cpu->pc-1 the same way z80_op_ld_r_r does
// (emu/src/z80.c) ---

static int gb_op_ld_rr_d16(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 4) & 0x03;
    set_rr(cpu, idx, fetch_word(cpu));
    return 12;
}

static int gb_op_inc_rr(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 4) & 0x03;
    set_rr(cpu, idx, (uint16_t)(get_rr(cpu, idx) + 1));
    return 8;
}

static int gb_op_dec_rr(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 4) & 0x03;
    set_rr(cpu, idx, (uint16_t)(get_rr(cpu, idx) - 1));
    return 8;
}

static int gb_op_add_hl_rr(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 4) & 0x03;
    gb_alu_add_hl(cpu, get_rr(cpu, idx));
    return 8;
}

static int gb_op_push_rr2(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 4) & 0x03;
    gb_push16(cpu, get_rr2(cpu, idx));
    return 16;
}

static int gb_op_pop_rr2(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 4) & 0x03;
    set_rr2(cpu, idx, gb_pop16(cpu));
    return 12;
}

static int gb_op_inc_r(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 3) & 0x07;
    set_reg8(cpu, idx, gb_alu_inc(cpu, get_reg8(cpu, idx)));
    return (idx == 6) ? 12 : 4;
}

static int gb_op_dec_r(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 3) & 0x07;
    set_reg8(cpu, idx, gb_alu_dec(cpu, get_reg8(cpu, idx)));
    return (idx == 6) ? 12 : 4;
}

static int gb_op_ld_r_d8(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 3) & 0x07;
    uint8_t val = fetch_byte(cpu);
    set_reg8(cpu, idx, val);
    return (idx == 6) ? 12 : 8;
}

// Covers 0x40-0x7F. 0x76 (which would otherwise decode as the
// impossible "LD (HL),(HL)") is HALT instead - same real hardware
// special case the Z80 shares, handled the same way z80_op_ld_r_r does.
static int gb_op_ld_r_r(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    if (opcode == 0x76) {
        cpu->halted = 1;
        return 4;
    }
    uint8_t dst_idx = (opcode >> 3) & 0x07;
    uint8_t src_idx = opcode & 0x07;
    set_reg8(cpu, dst_idx, get_reg8(cpu, src_idx));
    return (dst_idx == 6 || src_idx == 6) ? 8 : 4;
}

// Covers 0x80-0xBF: ADD/ADC/SUB/SBC/AND/XOR/OR/CP against r8.
static int gb_op_alu_group(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t op_idx = (opcode >> 3) & 0x07;
    uint8_t reg_idx = opcode & 0x07;
    gb_alu_dispatch(cpu, op_idx, get_reg8(cpu, reg_idx));
    return (reg_idx == 6) ? 8 : 4;
}

// Covers the 8 ALU-against-immediate opcodes (0xC6/CE/D6/DE/E6/EE/F6/FE),
// spaced by 0x08 the same way the r8 group above is spaced by 0x01.
static int gb_op_alu_d8_group(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t op_idx = (opcode >> 3) & 0x07;
    gb_alu_dispatch(cpu, op_idx, fetch_byte(cpu));
    return 8;
}

static int gb_op_jr_cc(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 3) & 0x03;
    int8_t off = (int8_t)fetch_byte(cpu);
    if (check_cond(cpu, idx)) {
        cpu->pc = (uint16_t)(cpu->pc + off);
        return 12;
    }
    return 8;
}

static int gb_op_jp_cc(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 3) & 0x03;
    uint16_t addr = fetch_word(cpu);
    if (check_cond(cpu, idx)) {
        cpu->pc = addr;
        return 16;
    }
    return 12;
}

static int gb_op_call_cc(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 3) & 0x03;
    uint16_t addr = fetch_word(cpu);
    if (check_cond(cpu, idx)) {
        gb_push16(cpu, cpu->pc);
        cpu->pc = addr;
        return 24;
    }
    return 12;
}

static int gb_op_ret_cc(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint8_t idx = (opcode >> 3) & 0x03;
    if (check_cond(cpu, idx)) {
        cpu->pc = gb_pop16(cpu);
        return 20;
    }
    return 8;
}

static int gb_op_rst(GBCpu *cpu) {
    uint8_t opcode = gb_read_byte(cpu, (uint16_t)(cpu->pc - 1));
    uint16_t target = opcode & 0x38;
    gb_push16(cpu, cpu->pc);
    cpu->pc = target;
    return 16;
}

// The entire CB-prefixed table (0xCB is a single main_opcode_table-
// style entry, not a 256-entry sub-table - same choice CLAUDE.md
// documents z80_op_prefix_cb making). Fully regular on the SM83 (no
// IX/IY-driven exceptions to carve out the way the Z80's version has
// to): bits 7-6 select the operation group, bits 5-3 select which
// rotate/shift/bit-index, bits 2-0 select the r8 operand.
static int gb_op_prefix_cb(GBCpu *cpu) {
    uint8_t opcode = fetch_byte(cpu);
    uint8_t reg_idx = opcode & 0x07;
    uint8_t bit_idx = (opcode >> 3) & 0x07;
    uint8_t group = (opcode >> 6) & 0x03;
    uint8_t val = get_reg8(cpu, reg_idx);

    if (group == 0) {
        uint8_t result;
        switch (bit_idx) {
            case 0: result = gb_alu_rlc(cpu, val); break;
            case 1: result = gb_alu_rrc(cpu, val); break;
            case 2: result = gb_alu_rl(cpu, val); break;
            case 3: result = gb_alu_rr(cpu, val); break;
            case 4: result = gb_alu_sla(cpu, val); break;
            case 5: result = gb_alu_sra(cpu, val); break;
            case 6: result = gb_alu_swap(cpu, val); break;
            default: result = gb_alu_srl(cpu, val); break; // 7
        }
        set_reg8(cpu, reg_idx, result);
        return (reg_idx == 6) ? 16 : 8;
    }
    if (group == 1) {
        gb_alu_bit(cpu, bit_idx, val);
        // BIT never writes anything back, so the (HL) form skips the
        // write-back cycles the other three groups need - 12, not 16.
        // Confirmed against the official gbdev.io opcode table; a
        // commonly-mirrored community JSON dataset gets this specific
        // case wrong (says 16) - see docs/GAMEBOY_ROADMAP.md.
        return (reg_idx == 6) ? 12 : 8;
    }
    if (group == 2) {
        set_reg8(cpu, reg_idx, gb_alu_res(bit_idx, val));
    } else {
        set_reg8(cpu, reg_idx, gb_alu_set(bit_idx, val));
    }
    return (reg_idx == 6) ? 16 : 8;
}

void gb_cpu_init_tables(void) {
    for (int i = 0; i < 256; i++) gb_opcode_table[i] = gb_op_illegal;

    gb_opcode_table[0x00] = gb_op_nop;
    gb_opcode_table[0x01] = gb_op_ld_rr_d16;
    gb_opcode_table[0x02] = gb_op_ld_bc_a;
    gb_opcode_table[0x03] = gb_op_inc_rr;
    gb_opcode_table[0x04] = gb_op_inc_r;
    gb_opcode_table[0x05] = gb_op_dec_r;
    gb_opcode_table[0x06] = gb_op_ld_r_d8;
    gb_opcode_table[0x07] = gb_op_rlca;
    gb_opcode_table[0x08] = gb_op_ld_a16_sp;
    gb_opcode_table[0x09] = gb_op_add_hl_rr;
    gb_opcode_table[0x0A] = gb_op_ld_a_bc;
    gb_opcode_table[0x0B] = gb_op_dec_rr;
    gb_opcode_table[0x0C] = gb_op_inc_r;
    gb_opcode_table[0x0D] = gb_op_dec_r;
    gb_opcode_table[0x0E] = gb_op_ld_r_d8;
    gb_opcode_table[0x0F] = gb_op_rrca;

    gb_opcode_table[0x10] = gb_op_stop;
    gb_opcode_table[0x11] = gb_op_ld_rr_d16;
    gb_opcode_table[0x12] = gb_op_ld_de_a;
    gb_opcode_table[0x13] = gb_op_inc_rr;
    gb_opcode_table[0x14] = gb_op_inc_r;
    gb_opcode_table[0x15] = gb_op_dec_r;
    gb_opcode_table[0x16] = gb_op_ld_r_d8;
    gb_opcode_table[0x17] = gb_op_rla;
    gb_opcode_table[0x18] = gb_op_jr;
    gb_opcode_table[0x19] = gb_op_add_hl_rr;
    gb_opcode_table[0x1A] = gb_op_ld_a_de;
    gb_opcode_table[0x1B] = gb_op_dec_rr;
    gb_opcode_table[0x1C] = gb_op_inc_r;
    gb_opcode_table[0x1D] = gb_op_dec_r;
    gb_opcode_table[0x1E] = gb_op_ld_r_d8;
    gb_opcode_table[0x1F] = gb_op_rra;

    gb_opcode_table[0x20] = gb_op_jr_cc;
    gb_opcode_table[0x21] = gb_op_ld_rr_d16;
    gb_opcode_table[0x22] = gb_op_ld_hli_a;
    gb_opcode_table[0x23] = gb_op_inc_rr;
    gb_opcode_table[0x24] = gb_op_inc_r;
    gb_opcode_table[0x25] = gb_op_dec_r;
    gb_opcode_table[0x26] = gb_op_ld_r_d8;
    gb_opcode_table[0x27] = gb_op_daa;
    gb_opcode_table[0x28] = gb_op_jr_cc;
    gb_opcode_table[0x29] = gb_op_add_hl_rr;
    gb_opcode_table[0x2A] = gb_op_ld_a_hli;
    gb_opcode_table[0x2B] = gb_op_dec_rr;
    gb_opcode_table[0x2C] = gb_op_inc_r;
    gb_opcode_table[0x2D] = gb_op_dec_r;
    gb_opcode_table[0x2E] = gb_op_ld_r_d8;
    gb_opcode_table[0x2F] = gb_op_cpl;

    gb_opcode_table[0x30] = gb_op_jr_cc;
    gb_opcode_table[0x31] = gb_op_ld_rr_d16;
    gb_opcode_table[0x32] = gb_op_ld_hld_a;
    gb_opcode_table[0x33] = gb_op_inc_rr;
    gb_opcode_table[0x34] = gb_op_inc_r;
    gb_opcode_table[0x35] = gb_op_dec_r;
    gb_opcode_table[0x36] = gb_op_ld_r_d8;
    gb_opcode_table[0x37] = gb_op_scf;
    gb_opcode_table[0x38] = gb_op_jr_cc;
    gb_opcode_table[0x39] = gb_op_add_hl_rr;
    gb_opcode_table[0x3A] = gb_op_ld_a_hld;
    gb_opcode_table[0x3B] = gb_op_dec_rr;
    gb_opcode_table[0x3C] = gb_op_inc_r;
    gb_opcode_table[0x3D] = gb_op_dec_r;
    gb_opcode_table[0x3E] = gb_op_ld_r_d8;
    gb_opcode_table[0x3F] = gb_op_ccf;

    for (int i = 0x40; i <= 0x7F; i++) gb_opcode_table[i] = gb_op_ld_r_r;
    for (int i = 0x80; i <= 0xBF; i++) gb_opcode_table[i] = gb_op_alu_group;

    gb_opcode_table[0xC0] = gb_op_ret_cc;
    gb_opcode_table[0xC1] = gb_op_pop_rr2;
    gb_opcode_table[0xC2] = gb_op_jp_cc;
    gb_opcode_table[0xC3] = gb_op_jp;
    gb_opcode_table[0xC4] = gb_op_call_cc;
    gb_opcode_table[0xC5] = gb_op_push_rr2;
    gb_opcode_table[0xC6] = gb_op_alu_d8_group;
    gb_opcode_table[0xC7] = gb_op_rst;
    gb_opcode_table[0xC8] = gb_op_ret_cc;
    gb_opcode_table[0xC9] = gb_op_ret;
    gb_opcode_table[0xCA] = gb_op_jp_cc;
    gb_opcode_table[0xCB] = gb_op_prefix_cb;
    gb_opcode_table[0xCC] = gb_op_call_cc;
    gb_opcode_table[0xCD] = gb_op_call;
    gb_opcode_table[0xCE] = gb_op_alu_d8_group;
    gb_opcode_table[0xCF] = gb_op_rst;

    gb_opcode_table[0xD0] = gb_op_ret_cc;
    gb_opcode_table[0xD1] = gb_op_pop_rr2;
    gb_opcode_table[0xD2] = gb_op_jp_cc;
    gb_opcode_table[0xD4] = gb_op_call_cc;
    gb_opcode_table[0xD5] = gb_op_push_rr2;
    gb_opcode_table[0xD6] = gb_op_alu_d8_group;
    gb_opcode_table[0xD7] = gb_op_rst;
    gb_opcode_table[0xD8] = gb_op_ret_cc;
    gb_opcode_table[0xD9] = gb_op_reti;
    gb_opcode_table[0xDA] = gb_op_jp_cc;
    gb_opcode_table[0xDC] = gb_op_call_cc;
    gb_opcode_table[0xDE] = gb_op_alu_d8_group;
    gb_opcode_table[0xDF] = gb_op_rst;

    gb_opcode_table[0xE0] = gb_op_ldh_a8_a;
    gb_opcode_table[0xE1] = gb_op_pop_rr2;
    gb_opcode_table[0xE2] = gb_op_ldh_c_a;
    gb_opcode_table[0xE5] = gb_op_push_rr2;
    gb_opcode_table[0xE6] = gb_op_alu_d8_group;
    gb_opcode_table[0xE7] = gb_op_rst;
    gb_opcode_table[0xE8] = gb_op_add_sp_e8;
    gb_opcode_table[0xE9] = gb_op_jp_hl;
    gb_opcode_table[0xEA] = gb_op_ld_a16_a;
    gb_opcode_table[0xEE] = gb_op_alu_d8_group;
    gb_opcode_table[0xEF] = gb_op_rst;

    gb_opcode_table[0xF0] = gb_op_ldh_a_a8;
    gb_opcode_table[0xF1] = gb_op_pop_rr2;
    gb_opcode_table[0xF2] = gb_op_ldh_a_c;
    gb_opcode_table[0xF3] = gb_op_di;
    gb_opcode_table[0xF5] = gb_op_push_rr2;
    gb_opcode_table[0xF6] = gb_op_alu_d8_group;
    gb_opcode_table[0xF7] = gb_op_rst;
    gb_opcode_table[0xF8] = gb_op_ld_hl_sp_e8;
    gb_opcode_table[0xF9] = gb_op_ld_sp_hl;
    gb_opcode_table[0xFA] = gb_op_ld_a_a16;
    gb_opcode_table[0xFB] = gb_op_ei;
    gb_opcode_table[0xFE] = gb_op_alu_d8_group;
    gb_opcode_table[0xFF] = gb_op_rst;

    // 0xD3/0xDB/0xDD/0xE3/0xE4/0xEB/0xEC/0xED/0xF4/0xFC/0xFD are left as
    // gb_op_illegal from the loop above - the 11 real gaps in the SM83's
    // opcode map (confirmed against the official table: each is labeled
    // ILLEGAL_xx there, not just "absent" the way an incomplete table
    // would leave them). Real hardware locks up executing one of these;
    // returning -1 here matches z80_op_unimplemented's own convention
    // for "this is a genuine bug in whatever's running", not a gap in
    // this emulator.
}

// Real DMG post-boot-ROM register values (what the Nintendo logo boot
// ROM leaves behind at PC=0x0100, before cartridge code ever runs) -
// fetched from pandocs' Power-Up Sequence page during Phase 1, not
// guessed, since Blargg's test ROMs (and most real games) are written
// assuming this exact starting state. The F register's H/C bits
// genuinely depend on whether the cartridge header checksum byte at
// 0x014D is exactly zero (clear if so, set otherwise - a real, if
// obscure, hardware behavior straight from pandocs' own footnote, not
// about whether the checksum *validates*: a real Game Boy would refuse
// to run the cartridge at all if it didn't). cart is optional - Phase 1
// test binaries with no attached cartridge, or a NULL cart during unit
// testing, still get a sensible default (the far more common "nonzero
// checksum" case).
void gb_cpu_reset(GBCpu *cpu) {
    uint8_t checksum_byte = cpu->cart ? cpu->cart->header_checksum_byte : 0xFF;
    cpu->af = 0x0100 | (checksum_byte == 0 ? 0x80 : 0xB0);
    cpu->bc = 0x0013;
    cpu->de = 0x00D8;
    cpu->hl = 0x014D;
    cpu->sp = 0xFFFE;
    cpu->pc = 0x0100;
    cpu->ime = 0;
    cpu->ime_pending = 0;
    cpu->halted = 0;
    cpu->stopped = 0;
    cpu->halt_bug = 0;
}

int gb_cpu_step(GBCpu *cpu) {
    if (cpu->halted) {
        // Phase 4 (interrupt controller) will add real wake-on-interrupt
        // logic and the HALT bug (see cpu.h's halt_bug field, grounded
        // against pandocs' halt.md during this phase). Until then, a
        // halted CPU just burns cycles forever - correct for everything
        // this phase's test ROMs exercise except Blargg's own
        // 02-interrupts.gb, a documented, deferred gap (see
        // docs/GAMEBOY_ROADMAP.md's Status section).
        return 4;
    }

    // EI's enable takes effect only after the instruction *following*
    // EI has fully executed - captured here (before this step's fetch)
    // and applied at the bottom (after this step's execute), so it's
    // the instruction after EI, not EI's own step, that's affected.
    uint8_t ime_to_set = cpu->ime_pending;
    cpu->ime_pending = 0;

    uint8_t opcode = fetch_byte(cpu);
    int cycles = gb_opcode_table[opcode](cpu);

    if (ime_to_set) cpu->ime = 1;

    return cycles;
}
