#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include "decode.h"

static const char *R8[8]  = {"B", "C", "D", "E", "H", "L", "(HL)", "A"};
static const char *RP[4]  = {"BC", "DE", "HL", "SP"};
static const char *CC[8]  = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};
static const char *ALU_MNEM[8] = {"ADD A,", "ADC A,", "SUB ", "SBC A,", "AND ", "XOR ", "OR ", "CP "};
static const char *ROT_MNEM[8] = {"RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SRL"};

static void hex8(char *buf, size_t n, uint8_t v) {
    if (((v >> 4) & 0xF) >= 0xA) snprintf(buf, n, "0%02Xh", v);
    else snprintf(buf, n, "%02Xh", v);
}

static void hex16(char *buf, size_t n, uint16_t v) {
    if (((v >> 12) & 0xF) >= 0xA) snprintf(buf, n, "0%04Xh", v);
    else snprintf(buf, n, "%04Xh", v);
}

static uint16_t rd16(const uint8_t *mem, uint16_t addr) {
    return (uint16_t)(mem[addr] | (mem[(uint16_t)(addr + 1)] << 8));
}

static DecodedInsn mk(int length, const char *fmt, ...) {
    DecodedInsn d;
    memset(&d, 0, sizeof(d));
    d.length = length;
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(d.text, sizeof(d.text), fmt, ap);
    va_end(ap);
    return d;
}

// --- 0xCB-prefixed: rotate/shift/BIT/SET/RES ---
static DecodedInsn decode_cb(const uint8_t *mem, uint16_t addr) {
    uint8_t op = mem[(uint16_t)(addr + 1)];
    uint8_t r = op & 0x07;
    uint8_t bitop = (op >> 3) & 0x07;
    uint8_t category = (op >> 6) & 0x03;

    DecodedInsn d;
    memset(&d, 0, sizeof(d));
    d.length = 2;

    switch (category) {
        case 0: snprintf(d.text, sizeof(d.text), "%s %s", ROT_MNEM[bitop], R8[r]); break;
        case 1: snprintf(d.text, sizeof(d.text), "BIT %d,%s", bitop, R8[r]); break;
        case 2: snprintf(d.text, sizeof(d.text), "RES %d,%s", bitop, R8[r]); break;
        default: snprintf(d.text, sizeof(d.text), "SET %d,%s", bitop, R8[r]); break;
    }
    return d;
}

// --- 0xED-prefixed: extended instructions ---
static DecodedInsn decode_ed(const uint8_t *mem, uint16_t addr) {
    uint8_t op = mem[(uint16_t)(addr + 1)];
    DecodedInsn d;
    memset(&d, 0, sizeof(d));

    switch (op) {
        case 0x40: case 0x48: case 0x50: case 0x58:
        case 0x60: case 0x68: case 0x70: case 0x78:
            d.length = 2;
            snprintf(d.text, sizeof(d.text), "IN %s,(C)", R8[(op >> 3) & 7]);
            return d;
        case 0x41: case 0x49: case 0x51: case 0x59:
        case 0x61: case 0x69: case 0x71: case 0x79:
            d.length = 2;
            snprintf(d.text, sizeof(d.text), "OUT (C),%s", R8[(op >> 3) & 7]);
            return d;
        case 0x42: case 0x52: case 0x62: case 0x72:
            d.length = 2;
            snprintf(d.text, sizeof(d.text), "SBC HL,%s", RP[(op >> 4) & 3]);
            return d;
        case 0x4A: case 0x5A: case 0x6A: case 0x7A:
            d.length = 2;
            snprintf(d.text, sizeof(d.text), "ADC HL,%s", RP[(op >> 4) & 3]);
            return d;
        case 0x43: case 0x53: case 0x63: case 0x73: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 2));
            char hexbuf[8];
            hex16(hexbuf, sizeof(hexbuf), nn);
            d.length = 4;
            snprintf(d.text, sizeof(d.text), "LD (%s),%s", hexbuf, RP[(op >> 4) & 3]);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x4B: case 0x5B: case 0x6B: case 0x7B: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 2));
            char hexbuf[8];
            hex16(hexbuf, sizeof(hexbuf), nn);
            d.length = 4;
            snprintf(d.text, sizeof(d.text), "LD %s,(%s)", RP[(op >> 4) & 3], hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x44: case 0x4C: case 0x54: case 0x5C:
        case 0x64: case 0x6C: case 0x74: case 0x7C:
            d.length = 2; snprintf(d.text, sizeof(d.text), "NEG"); return d;
        case 0x45: case 0x55: case 0x65: case 0x75:
        case 0x4D: case 0x5D: case 0x6D: case 0x7D:
            d.length = 2;
            snprintf(d.text, sizeof(d.text), (op == 0x4D) ? "RETI" : "RETN");
            return d;
        case 0x46: case 0x66: d.length = 2; snprintf(d.text, sizeof(d.text), "IM 0"); return d;
        case 0x56: case 0x76: d.length = 2; snprintf(d.text, sizeof(d.text), "IM 1"); return d;
        case 0x5E: case 0x7E: d.length = 2; snprintf(d.text, sizeof(d.text), "IM 2"); return d;
        case 0x47: d.length = 2; snprintf(d.text, sizeof(d.text), "LD I,A"); return d;
        case 0x4F: d.length = 2; snprintf(d.text, sizeof(d.text), "LD R,A"); return d;
        case 0x57: d.length = 2; snprintf(d.text, sizeof(d.text), "LD A,I"); return d;
        case 0x5F: d.length = 2; snprintf(d.text, sizeof(d.text), "LD A,R"); return d;
        case 0x67: d.length = 2; snprintf(d.text, sizeof(d.text), "RRD"); return d;
        case 0x6F: d.length = 2; snprintf(d.text, sizeof(d.text), "RLD"); return d;
        case 0xA0: d.length = 2; snprintf(d.text, sizeof(d.text), "LDI");  return d;
        case 0xA8: d.length = 2; snprintf(d.text, sizeof(d.text), "LDD");  return d;
        case 0xB0: d.length = 2; snprintf(d.text, sizeof(d.text), "LDIR"); return d;
        case 0xB8: d.length = 2; snprintf(d.text, sizeof(d.text), "LDDR"); return d;
        case 0xA1: d.length = 2; snprintf(d.text, sizeof(d.text), "CPI");  return d;
        case 0xA9: d.length = 2; snprintf(d.text, sizeof(d.text), "CPD");  return d;
        case 0xB1: d.length = 2; snprintf(d.text, sizeof(d.text), "CPIR"); return d;
        case 0xB9: d.length = 2; snprintf(d.text, sizeof(d.text), "CPDR"); return d;
        case 0xA2: d.length = 2; snprintf(d.text, sizeof(d.text), "INI");  return d;
        case 0xAA: d.length = 2; snprintf(d.text, sizeof(d.text), "IND");  return d;
        case 0xB2: d.length = 2; snprintf(d.text, sizeof(d.text), "INIR"); return d;
        case 0xBA: d.length = 2; snprintf(d.text, sizeof(d.text), "INDR"); return d;
        case 0xA3: d.length = 2; snprintf(d.text, sizeof(d.text), "OUTI"); return d;
        case 0xAB: d.length = 2; snprintf(d.text, sizeof(d.text), "OUTD"); return d;
        case 0xB3: d.length = 2; snprintf(d.text, sizeof(d.text), "OTIR"); return d;
        case 0xBB: d.length = 2; snprintf(d.text, sizeof(d.text), "OTDR"); return d;
        default: {
            char hexbuf[8];
            hex8(hexbuf, sizeof(hexbuf), op);
            d.length = 2;
            snprintf(d.text, sizeof(d.text), "DB 0EDh,%s", hexbuf);
            return d;
        }
    }
}

// --- DD/FD CB d op: indexed rotate/shift/BIT/SET/RES ---
static DecodedInsn decode_index_cb(const uint8_t *mem, uint16_t addr, const char *ixname) {
    int8_t disp = (int8_t)mem[(uint16_t)(addr + 2)];
    uint8_t op = mem[(uint16_t)(addr + 3)];
    uint8_t r = op & 0x07;
    uint8_t bitop = (op >> 3) & 0x07;
    uint8_t category = (op >> 6) & 0x03;

    char disp_buf[16];
    snprintf(disp_buf, sizeof(disp_buf), "%s%d", disp >= 0 ? "+" : "", disp);

    DecodedInsn d;
    memset(&d, 0, sizeof(d));
    d.length = 4;

    // Real hardware also copies the rotate/shift/SET/RES result into a
    // register when r != 6 (the (HL) slot) - undocumented, shown here as
    // a second operand (common disassembler convention for this form).
    switch (category) {
        case 0:
            if (r == 6) snprintf(d.text, sizeof(d.text), "%s (%s%s)", ROT_MNEM[bitop], ixname, disp_buf);
            else snprintf(d.text, sizeof(d.text), "%s (%s%s),%s", ROT_MNEM[bitop], ixname, disp_buf, R8[r]);
            break;
        case 1:
            snprintf(d.text, sizeof(d.text), "BIT %d,(%s%s)", bitop, ixname, disp_buf);
            break;
        case 2:
            if (r == 6) snprintf(d.text, sizeof(d.text), "RES %d,(%s%s)", bitop, ixname, disp_buf);
            else snprintf(d.text, sizeof(d.text), "RES %d,(%s%s),%s", bitop, ixname, disp_buf, R8[r]);
            break;
        default:
            if (r == 6) snprintf(d.text, sizeof(d.text), "SET %d,(%s%s)", bitop, ixname, disp_buf);
            else snprintf(d.text, sizeof(d.text), "SET %d,(%s%s),%s", bitop, ixname, disp_buf, R8[r]);
            break;
    }
    return d;
}

static DecodedInsn decode_instruction_impl(const uint8_t *mem, uint16_t addr);

// --- DD/FD-prefixed: IX/IY-specific opcodes ---
static DecodedInsn decode_index(const uint8_t *mem, uint16_t addr, int is_iy) {
    const char *ixname = is_iy ? "IY" : "IX";
    const char *ixh = is_iy ? "IYH" : "IXH";
    const char *ixl = is_iy ? "IYL" : "IXL";
    uint8_t op = mem[(uint16_t)(addr + 1)];

    DecodedInsn d;
    memset(&d, 0, sizeof(d));
    char hexbuf[8], disp_buf[16];

    switch (op) {
        case 0xCB: return decode_index_cb(mem, addr, ixname);

        case 0x21: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 2));
            hex16(hexbuf, sizeof(hexbuf), nn);
            d.length = 4; snprintf(d.text, sizeof(d.text), "LD %s,%s", ixname, hexbuf);
            return d;
        }
        case 0x22: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 2));
            hex16(hexbuf, sizeof(hexbuf), nn);
            d.length = 4; snprintf(d.text, sizeof(d.text), "LD (%s),%s", hexbuf, ixname);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x2A: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 2));
            hex16(hexbuf, sizeof(hexbuf), nn);
            d.length = 4; snprintf(d.text, sizeof(d.text), "LD %s,(%s)", ixname, hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x23: return mk(2, "INC %s", ixname);
        case 0x2B: return mk(2, "DEC %s", ixname);
        case 0x09: return mk(2, "ADD %s,BC", ixname);
        case 0x19: return mk(2, "ADD %s,DE", ixname);
        case 0x29: return mk(2, "ADD %s,%s", ixname, ixname);
        case 0x39: return mk(2, "ADD %s,SP", ixname);

        case 0x24: return mk(2, "INC %s", ixh);
        case 0x25: return mk(2, "DEC %s", ixh);
        case 0x26: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 2)]);
                   return mk(3, "LD %s,%s", ixh, hexbuf);
        case 0x2C: return mk(2, "INC %s", ixl);
        case 0x2D: return mk(2, "DEC %s", ixl);
        case 0x2E: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 2)]);
                   return mk(3, "LD %s,%s", ixl, hexbuf);

        case 0x34:
            snprintf(disp_buf, sizeof(disp_buf), "%s%d",
                     (int8_t)mem[(uint16_t)(addr + 2)] >= 0 ? "+" : "", (int8_t)mem[(uint16_t)(addr + 2)]);
            return mk(3, "INC (%s%s)", ixname, disp_buf);
        case 0x35:
            snprintf(disp_buf, sizeof(disp_buf), "%s%d",
                     (int8_t)mem[(uint16_t)(addr + 2)] >= 0 ? "+" : "", (int8_t)mem[(uint16_t)(addr + 2)]);
            return mk(3, "DEC (%s%s)", ixname, disp_buf);
        case 0x36: {
            int8_t disp = (int8_t)mem[(uint16_t)(addr + 2)];
            snprintf(disp_buf, sizeof(disp_buf), "%s%d", disp >= 0 ? "+" : "", disp);
            hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 3)]);
            return mk(4, "LD (%s%s),%s", ixname, disp_buf, hexbuf);
        }

        case 0xE1: return mk(2, "POP %s", ixname);
        case 0xE5: return mk(2, "PUSH %s", ixname);
        case 0xE3: return mk(2, "EX (SP),%s", ixname);
        case 0xE9: return mk(2, "JP (%s)", ixname);
        case 0xF9: return mk(2, "LD SP,%s", ixname);

        default:
            if (op >= 0x40 && op <= 0x7F) {
                if (op == 0x76) return mk(2, "HALT"); // real Z80: DD 76 is still plain HALT
                uint8_t dst = (op >> 3) & 7, src = op & 7;
                int dmem = (dst == 6), smem = (src == 6);
                char dbuf[16], sbuf[16];
                if (dmem) {
                    int8_t disp = (int8_t)mem[(uint16_t)(addr + 2)];
                    snprintf(dbuf, sizeof(dbuf), "(%s%s%d)", ixname, disp >= 0 ? "+" : "", disp);
                } else if (dst == 4) snprintf(dbuf, sizeof(dbuf), "%s", ixh);
                else if (dst == 5) snprintf(dbuf, sizeof(dbuf), "%s", ixl);
                else snprintf(dbuf, sizeof(dbuf), "%s", R8[dst]);

                if (smem) {
                    int8_t disp = (int8_t)mem[(uint16_t)(addr + 2)];
                    snprintf(sbuf, sizeof(sbuf), "(%s%s%d)", ixname, disp >= 0 ? "+" : "", disp);
                } else if (src == 4) snprintf(sbuf, sizeof(sbuf), "%s", ixh);
                else if (src == 5) snprintf(sbuf, sizeof(sbuf), "%s", ixl);
                else snprintf(sbuf, sizeof(sbuf), "%s", R8[src]);

                return mk((dmem || smem) ? 3 : 2, "LD %s,%s", dbuf, sbuf);
            }
            if (op >= 0x80 && op <= 0xBF) {
                uint8_t alu_op = (op >> 3) & 7, src = op & 7;
                char sbuf[16];
                if (src == 6) {
                    int8_t disp = (int8_t)mem[(uint16_t)(addr + 2)];
                    snprintf(sbuf, sizeof(sbuf), "(%s%s%d)", ixname, disp >= 0 ? "+" : "", disp);
                    return mk(3, "%s%s", ALU_MNEM[alu_op], sbuf);
                }
                if (src == 4) snprintf(sbuf, sizeof(sbuf), "%s", ixh);
                else if (src == 5) snprintf(sbuf, sizeof(sbuf), "%s", ixl);
                else snprintf(sbuf, sizeof(sbuf), "%s", R8[src]);
                return mk(2, "%s%s", ALU_MNEM[alu_op], sbuf);
            }
            // Genuinely prefix-independent: the DD/FD byte is wasted
            // overhead on real hardware; decode what follows normally
            // (this also naturally handles repeated/stacked DD/FD
            // prefixes, since re-entering the top-level decoder here
            // will itself see another prefix byte if there is one).
            {
                DecodedInsn inner = decode_instruction_impl(mem, (uint16_t)(addr + 1));
                d = inner;
                d.length = inner.length + 1;
                return d;
            }
    }
}

static DecodedInsn decode_main(const uint8_t *mem, uint16_t addr) {
    uint8_t op = mem[addr];
    char hexbuf[8], hexbuf2[8];

    // 0x40-0x7F: LD r,r' (0x76 = HALT)
    if (op >= 0x40 && op <= 0x7F) {
        if (op == 0x76) return mk(1, "HALT");
        uint8_t dst = (op >> 3) & 7, src = op & 7;
        return mk(1, "LD %s,%s", R8[dst], R8[src]);
    }
    // 0x80-0xBF: ALU A,r
    if (op >= 0x80 && op <= 0xBF) {
        uint8_t alu_op = (op >> 3) & 7, src = op & 7;
        return mk(1, "%s%s", ALU_MNEM[alu_op], R8[src]);
    }

    switch (op) {
        case 0x00: return mk(1, "NOP");
        case 0x01: hex16(hexbuf, sizeof(hexbuf), rd16(mem, (uint16_t)(addr + 1)));
                   return mk(3, "LD BC,%s", hexbuf);
        case 0x02: return mk(1, "LD (BC),A");
        case 0x03: return mk(1, "INC BC");
        case 0x04: return mk(1, "INC B");
        case 0x05: return mk(1, "DEC B");
        case 0x06: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD B,%s", hexbuf);
        case 0x07: return mk(1, "RLCA");
        case 0x08: return mk(1, "EX AF,AF'");
        case 0x09: return mk(1, "ADD HL,BC");
        case 0x0A: return mk(1, "LD A,(BC)");
        case 0x0B: return mk(1, "DEC BC");
        case 0x0C: return mk(1, "INC C");
        case 0x0D: return mk(1, "DEC C");
        case 0x0E: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD C,%s", hexbuf);
        case 0x0F: return mk(1, "RRCA");

        case 0x10: {
            int8_t disp = (int8_t)mem[(uint16_t)(addr + 1)];
            uint16_t target = (uint16_t)(addr + 2 + disp);
            hex16(hexbuf, sizeof(hexbuf), target);
            DecodedInsn d = mk(2, "DJNZ %s", hexbuf);
            d.has_ref = 1; d.ref_addr = target; d.ref_is_code = 1;
            return d;
        }
        case 0x11: hex16(hexbuf, sizeof(hexbuf), rd16(mem, (uint16_t)(addr + 1)));
                   return mk(3, "LD DE,%s", hexbuf);
        case 0x12: return mk(1, "LD (DE),A");
        case 0x13: return mk(1, "INC DE");
        case 0x14: return mk(1, "INC D");
        case 0x15: return mk(1, "DEC D");
        case 0x16: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD D,%s", hexbuf);
        case 0x17: return mk(1, "RLA");

        case 0x18: {
            int8_t disp = (int8_t)mem[(uint16_t)(addr + 1)];
            uint16_t target = (uint16_t)(addr + 2 + disp);
            hex16(hexbuf, sizeof(hexbuf), target);
            DecodedInsn d = mk(2, "JR %s", hexbuf);
            d.has_ref = 1; d.ref_addr = target; d.ref_is_code = 1;
            return d;
        }
        case 0x19: return mk(1, "ADD HL,DE");
        case 0x1A: return mk(1, "LD A,(DE)");
        case 0x1B: return mk(1, "DEC DE");
        case 0x1C: return mk(1, "INC E");
        case 0x1D: return mk(1, "DEC E");
        case 0x1E: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD E,%s", hexbuf);
        case 0x1F: return mk(1, "RRA");

        case 0x20: case 0x28: case 0x30: case 0x38: {
            int8_t disp = (int8_t)mem[(uint16_t)(addr + 1)];
            uint16_t target = (uint16_t)(addr + 2 + disp);
            hex16(hexbuf, sizeof(hexbuf), target);
            int idx = (op - 0x20) / 8; // 0x20/28/30/38 -> NZ/Z/NC/C, i.e. CC[0..3]
            DecodedInsn d = mk(2, "JR %s,%s", CC[idx], hexbuf);
            d.has_ref = 1; d.ref_addr = target; d.ref_is_code = 1;
            return d;
        }
        case 0x21: hex16(hexbuf, sizeof(hexbuf), rd16(mem, (uint16_t)(addr + 1)));
                   return mk(3, "LD HL,%s", hexbuf);
        case 0x22: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "LD (%s),HL", hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x23: return mk(1, "INC HL");
        case 0x24: return mk(1, "INC H");
        case 0x25: return mk(1, "DEC H");
        case 0x26: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD H,%s", hexbuf);
        case 0x27: return mk(1, "DAA");
        case 0x29: return mk(1, "ADD HL,HL");
        case 0x2A: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "LD HL,(%s)", hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x2B: return mk(1, "DEC HL");
        case 0x2C: return mk(1, "INC L");
        case 0x2D: return mk(1, "DEC L");
        case 0x2E: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD L,%s", hexbuf);
        case 0x2F: return mk(1, "CPL");

        case 0x31: hex16(hexbuf, sizeof(hexbuf), rd16(mem, (uint16_t)(addr + 1)));
                   return mk(3, "LD SP,%s", hexbuf);
        case 0x32: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "LD (%s),A", hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x33: return mk(1, "INC SP");
        case 0x34: return mk(1, "INC (HL)");
        case 0x35: return mk(1, "DEC (HL)");
        case 0x36: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD (HL),%s", hexbuf);
        case 0x37: return mk(1, "SCF");
        case 0x39: return mk(1, "ADD HL,SP");
        case 0x3A: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "LD A,(%s)", hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 0;
            return d;
        }
        case 0x3B: return mk(1, "DEC SP");
        case 0x3C: return mk(1, "INC A");
        case 0x3D: return mk(1, "DEC A");
        case 0x3E: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "LD A,%s", hexbuf);
        case 0x3F: return mk(1, "CCF");

        case 0xC0: case 0xC8: case 0xD0: case 0xD8:
        case 0xE0: case 0xE8: case 0xF0: case 0xF8:
            return mk(1, "RET %s", CC[(op >> 3) & 7]);
        case 0xC1: return mk(1, "POP BC");
        case 0xD1: return mk(1, "POP DE");
        case 0xE1: return mk(1, "POP HL");
        case 0xF1: return mk(1, "POP AF");
        case 0xC5: return mk(1, "PUSH BC");
        case 0xD5: return mk(1, "PUSH DE");
        case 0xE5: return mk(1, "PUSH HL");
        case 0xF5: return mk(1, "PUSH AF");

        case 0xC2: case 0xCA: case 0xD2: case 0xDA:
        case 0xE2: case 0xEA: case 0xF2: case 0xFA: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "JP %s,%s", CC[(op >> 3) & 7], hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 1;
            return d;
        }
        case 0xC3: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "JP %s", hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 1;
            return d;
        }
        case 0xE9: return mk(1, "JP (HL)");

        case 0xC4: case 0xCC: case 0xD4: case 0xDC:
        case 0xE4: case 0xEC: case 0xF4: case 0xFC: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "CALL %s,%s", CC[(op >> 3) & 7], hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 1;
            return d;
        }
        case 0xCD: {
            uint16_t nn = rd16(mem, (uint16_t)(addr + 1));
            hex16(hexbuf, sizeof(hexbuf), nn);
            DecodedInsn d = mk(3, "CALL %s", hexbuf);
            d.has_ref = 1; d.ref_addr = nn; d.ref_is_code = 1;
            return d;
        }
        case 0xC9: return mk(1, "RET");

        case 0xC6: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "ADD A,%s", hexbuf);
        case 0xCE: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "ADC A,%s", hexbuf);
        case 0xD6: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "SUB %s", hexbuf);
        case 0xDE: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "SBC A,%s", hexbuf);
        case 0xE6: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "AND %s", hexbuf);
        case 0xEE: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "XOR %s", hexbuf);
        case 0xF6: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "OR %s", hexbuf);
        case 0xFE: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]); return mk(2, "CP %s", hexbuf);

        case 0xC7: case 0xCF: case 0xD7: case 0xDF:
        case 0xE7: case 0xEF: case 0xF7: case 0xFF: {
            uint8_t p = op & 0x38;
            hex8(hexbuf, sizeof(hexbuf), p);
            DecodedInsn d = mk(1, "RST %s", hexbuf);
            d.has_ref = 1; d.ref_addr = p; d.ref_is_code = 1;
            return d;
        }

        case 0xD3: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "OUT (%s),A", hexbuf);
        case 0xDB: hex8(hexbuf, sizeof(hexbuf), mem[(uint16_t)(addr + 1)]);
                   return mk(2, "IN A,(%s)", hexbuf);

        case 0xD9: return mk(1, "EXX");
        case 0xE3: return mk(1, "EX (SP),HL");
        case 0xEB: return mk(1, "EX DE,HL");
        case 0xF9: return mk(1, "LD SP,HL");
        case 0xF3: return mk(1, "DI");
        case 0xFB: return mk(1, "EI");

        default:
            hex8(hexbuf2, sizeof(hexbuf2), op);
            return mk(1, "DB %s", hexbuf2);
    }
}

static DecodedInsn decode_instruction_impl(const uint8_t *mem, uint16_t addr) {
    uint8_t op = mem[addr];
    if (op == 0xCB) return decode_cb(mem, addr);
    if (op == 0xED) return decode_ed(mem, addr);
    if (op == 0xDD) return decode_index(mem, addr, 0);
    if (op == 0xFD) return decode_index(mem, addr, 1);
    return decode_main(mem, addr);
}

DecodedInsn decode_instruction(const uint8_t *mem, uint16_t addr) {
    return decode_instruction_impl(mem, addr);
}
