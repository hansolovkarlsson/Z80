#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "encode.h"
#include "expr.h"

typedef enum {
    OP_NONE,
    OP_REG,        // reg: 0=B,1=C,2=D,3=E,4=H,5=L,7=A (6 reserved for (HL), see OP_MEM_HL)
    OP_IXH, OP_IXL, OP_IYH, OP_IYL,
    OP_REGPAIR,    // reg: 0=BC,1=DE,2=HL,3=SP
    OP_AF,
    OP_AF_ALT,     // AF'
    OP_IX,
    OP_IY,
    OP_REG_I,
    OP_REG_R,
    OP_MEM_HL,     // (HL)
    OP_MEM_BC,     // (BC)
    OP_MEM_DE,     // (DE)
    OP_MEM_SP,     // (SP)
    OP_MEM_C,      // (C)
    OP_MEM_IX,     // (IX+d) -- expr holds the displacement text
    OP_MEM_IY,     // (IY+d)
    OP_MEM_IMM,    // (expr) -- 16-bit address
    OP_IMM,        // expr
} OperKind;

typedef struct {
    OperKind kind;
    int reg;
    char expr[160];
} Operand;

static void str_trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t')) s[--len] = '\0';
}

static int str_eq_ci(const char *a, const char *b) {
    return strcasecmp(a, b) == 0;
}

static void emit(EncOut *o, uint8_t b) {
    if (o->nbytes < (int)sizeof(o->bytes)) o->bytes[o->nbytes++] = b;
}

static void emit_word(EncOut *o, uint16_t w) {
    emit(o, (uint8_t)(w & 0xFF));
    emit(o, (uint8_t)(w >> 8));
}

static long eval(EncCtx *ctx, const char *expr_text, EncOut *out) {
    ExprEnv env;
    env.symtab = ctx->symtab;
    env.pc = ctx->pc;
    env.pass = ctx->pass;
    env.unresolved = 0;
    env.err = NULL;

    const char *p = expr_text;
    long v = expr_parse(&p, &env);
    if (env.err && !out->err) out->err = env.err;
    if (env.unresolved) ctx->unresolved = 1;
    return v;
}

// --- Operand parsing (purely syntactic - no expression evaluation) ---

static int reg8_index(const char *name) {
    if (str_eq_ci(name, "B")) return 0;
    if (str_eq_ci(name, "C")) return 1;
    if (str_eq_ci(name, "D")) return 2;
    if (str_eq_ci(name, "E")) return 3;
    if (str_eq_ci(name, "H")) return 4;
    if (str_eq_ci(name, "L")) return 5;
    if (str_eq_ci(name, "A")) return 7;
    return -1;
}

static int regpair_index(const char *name) {
    if (str_eq_ci(name, "BC")) return 0;
    if (str_eq_ci(name, "DE")) return 1;
    if (str_eq_ci(name, "HL")) return 2;
    if (str_eq_ci(name, "SP")) return 3;
    return -1;
}

static void parse_operand(const char *text_in, Operand *o) {
    char text[200];
    strncpy(text, text_in, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    str_trim(text);

    memset(o, 0, sizeof(*o));

    if (text[0] == '\0') { o->kind = OP_NONE; return; }

    size_t len = strlen(text);
    if (text[0] == '(' && text[len - 1] == ')') {
        char inner[200];
        size_t inner_len = len - 2;
        if (inner_len >= sizeof(inner)) inner_len = sizeof(inner) - 1;
        memcpy(inner, text + 1, inner_len);
        inner[inner_len] = '\0';
        str_trim(inner);

        if (str_eq_ci(inner, "HL")) { o->kind = OP_MEM_HL; return; }
        if (str_eq_ci(inner, "BC")) { o->kind = OP_MEM_BC; return; }
        if (str_eq_ci(inner, "DE")) { o->kind = OP_MEM_DE; return; }
        if (str_eq_ci(inner, "SP")) { o->kind = OP_MEM_SP; return; }
        if (str_eq_ci(inner, "C"))  { o->kind = OP_MEM_C;  return; }

        if ((inner[0] == 'I' || inner[0] == 'i') && (inner[1] == 'X' || inner[1] == 'x')) {
            o->kind = OP_MEM_IX;
            const char *rest = inner + 2;
            while (*rest == ' ') rest++;
            strncpy(o->expr, *rest ? rest : "0", sizeof(o->expr) - 1);
            return;
        }
        if ((inner[0] == 'I' || inner[0] == 'i') && (inner[1] == 'Y' || inner[1] == 'y')) {
            o->kind = OP_MEM_IY;
            const char *rest = inner + 2;
            while (*rest == ' ') rest++;
            strncpy(o->expr, *rest ? rest : "0", sizeof(o->expr) - 1);
            return;
        }

        o->kind = OP_MEM_IMM;
        strncpy(o->expr, inner, sizeof(o->expr) - 1);
        return;
    }

    int r = reg8_index(text);
    if (r >= 0) { o->kind = OP_REG; o->reg = r; return; }

    if (str_eq_ci(text, "IXH")) { o->kind = OP_IXH; return; }
    if (str_eq_ci(text, "IXL")) { o->kind = OP_IXL; return; }
    if (str_eq_ci(text, "IYH")) { o->kind = OP_IYH; return; }
    if (str_eq_ci(text, "IYL")) { o->kind = OP_IYL; return; }
    if (str_eq_ci(text, "IX"))  { o->kind = OP_IX; return; }
    if (str_eq_ci(text, "IY"))  { o->kind = OP_IY; return; }
    if (str_eq_ci(text, "I"))   { o->kind = OP_REG_I; return; }
    if (str_eq_ci(text, "R"))   { o->kind = OP_REG_R; return; }
    if (str_eq_ci(text, "AF'")) { o->kind = OP_AF_ALT; return; }
    if (str_eq_ci(text, "AF"))  { o->kind = OP_AF; return; }

    int rp = regpair_index(text);
    if (rp >= 0) { o->kind = OP_REGPAIR; o->reg = rp; return; }

    o->kind = OP_IMM;
    strncpy(o->expr, text, sizeof(o->expr) - 1);
}

// Splits operand_text into up to two comma-separated fields, respecting
// parenthesis nesting so "(IX+1)" isn't split on a comma that isn't there
// anyway, but more importantly so future operand forms with nested
// expressions stay intact. Returns the operand count (0-2).
static int split_operands(const char *text_in, char out[2][200]) {
    char text[400];
    strncpy(text, text_in, sizeof(text) - 1);
    text[sizeof(text) - 1] = '\0';
    str_trim(text);

    out[0][0] = out[1][0] = '\0';
    if (text[0] == '\0') return 0;

    int n = 0;
    int depth = 0;
    const char *start = text;
    const char *p = text;
    for (;; p++) {
        char c = *p;
        if (c == '(') depth++;
        else if (c == ')') depth--;
        if ((c == ',' && depth == 0) || c == '\0') {
            if (n < 2) {
                size_t flen = (size_t)(p - start);
                if (flen >= 200) flen = 199;
                memcpy(out[n], start, flen);
                out[n][flen] = '\0';
                str_trim(out[n]);
                n++;
            }
            start = p + 1;
            if (c == '\0') break;
        }
    }
    return n;
}

// r-field for the plain 8-bit register/(HL) context (B,C,D,E,H,L,(HL),A).
static int simple_rfield(const Operand *o) {
    if (o->kind == OP_REG) return o->reg;
    if (o->kind == OP_MEM_HL) return 6;
    return -1;
}

// r-field for a DD/FD-prefixed 0x40-0xBF context: B,C,D,E,A keep their
// normal field; IXH/IYH->4, IXL/IYL->5. *is_iy is 1 for IY-half registers,
// 0 for IX-half, -1 for a plain (prefix-independent) register.
static int idx_rfield(const Operand *o, int *is_iy) {
    if (o->kind == OP_REG && o->reg != 6) { if (is_iy) *is_iy = -1; return o->reg; }
    if (o->kind == OP_IXH) { if (is_iy) *is_iy = 0; return 4; }
    if (o->kind == OP_IXL) { if (is_iy) *is_iy = 0; return 5; }
    if (o->kind == OP_IYH) { if (is_iy) *is_iy = 1; return 4; }
    if (o->kind == OP_IYL) { if (is_iy) *is_iy = 1; return 5; }
    return -1;
}

// --- Per-category encoders ---

static void encode_ld(EncCtx *ctx, Operand *dst, Operand *src, EncOut *out) {
    int dr = simple_rfield(dst);
    int sr = simple_rfield(src);

    if (dr >= 0 && sr >= 0) {
        if (dr == 6 && sr == 6) { out->err = "(HL),(HL) is not a valid instruction (that's HALT)"; return; }
        emit(out, (uint8_t)(0x40 | (dr << 3) | sr));
        return;
    }

    if (dr >= 0 && src->kind == OP_IMM) {
        emit(out, (dr == 6) ? 0x36 : (uint8_t)(0x06 | (dr << 3)));
        emit(out, (uint8_t)eval(ctx, src->expr, out));
        return;
    }

    if (dst->kind == OP_REG && dst->reg == 7 && src->kind == OP_MEM_BC) { emit(out, 0x0A); return; }
    if (dst->kind == OP_REG && dst->reg == 7 && src->kind == OP_MEM_DE) { emit(out, 0x1A); return; }
    if (dst->kind == OP_REG && dst->reg == 7 && src->kind == OP_MEM_IMM) {
        emit(out, 0x3A);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }
    if (src->kind == OP_REG && src->reg == 7 && dst->kind == OP_MEM_BC) { emit(out, 0x02); return; }
    if (src->kind == OP_REG && src->reg == 7 && dst->kind == OP_MEM_DE) { emit(out, 0x12); return; }
    if (src->kind == OP_REG && src->reg == 7 && dst->kind == OP_MEM_IMM) {
        emit(out, 0x32);
        emit_word(out, (uint16_t)eval(ctx, dst->expr, out));
        return;
    }

    if (dst->kind == OP_REG && dst->reg == 7 && src->kind == OP_REG_I) { emit(out, 0xED); emit(out, 0x57); return; }
    if (dst->kind == OP_REG && dst->reg == 7 && src->kind == OP_REG_R) { emit(out, 0xED); emit(out, 0x5F); return; }
    if (dst->kind == OP_REG_I && src->kind == OP_REG && src->reg == 7) { emit(out, 0xED); emit(out, 0x47); return; }
    if (dst->kind == OP_REG_R && src->kind == OP_REG && src->reg == 7) { emit(out, 0xED); emit(out, 0x4F); return; }

    if (dst->kind == OP_REGPAIR && src->kind == OP_IMM) {
        static const uint8_t base[4] = {0x01, 0x11, 0x21, 0x31};
        emit(out, base[dst->reg]);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }
    if (dst->kind == OP_IX && src->kind == OP_IMM) {
        emit(out, 0xDD); emit(out, 0x21);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }
    if (dst->kind == OP_IY && src->kind == OP_IMM) {
        emit(out, 0xFD); emit(out, 0x21);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }

    if (dst->kind == OP_REGPAIR && dst->reg == 2 && src->kind == OP_MEM_IMM) { // LD HL,(nn)
        emit(out, 0x2A);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }
    if (dst->kind == OP_REGPAIR && src->kind == OP_MEM_IMM) { // LD BC/DE/SP,(nn)
        static const uint8_t lo[4] = {0x4B, 0x5B, 0x00, 0x7B};
        emit(out, 0xED); emit(out, lo[dst->reg]);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }
    if (dst->kind == OP_IX && src->kind == OP_MEM_IMM) {
        emit(out, 0xDD); emit(out, 0x2A);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }
    if (dst->kind == OP_IY && src->kind == OP_MEM_IMM) {
        emit(out, 0xFD); emit(out, 0x2A);
        emit_word(out, (uint16_t)eval(ctx, src->expr, out));
        return;
    }

    if (dst->kind == OP_MEM_IMM && src->kind == OP_REGPAIR && src->reg == 2) { // LD (nn),HL
        emit(out, 0x22);
        emit_word(out, (uint16_t)eval(ctx, dst->expr, out));
        return;
    }
    if (dst->kind == OP_MEM_IMM && src->kind == OP_REGPAIR) { // LD (nn),BC/DE/SP
        static const uint8_t lo[4] = {0x43, 0x53, 0x00, 0x73};
        emit(out, 0xED); emit(out, lo[src->reg]);
        emit_word(out, (uint16_t)eval(ctx, dst->expr, out));
        return;
    }
    if (dst->kind == OP_MEM_IMM && src->kind == OP_IX) {
        emit(out, 0xDD); emit(out, 0x22);
        emit_word(out, (uint16_t)eval(ctx, dst->expr, out));
        return;
    }
    if (dst->kind == OP_MEM_IMM && src->kind == OP_IY) {
        emit(out, 0xFD); emit(out, 0x22);
        emit_word(out, (uint16_t)eval(ctx, dst->expr, out));
        return;
    }

    if (dst->kind == OP_REGPAIR && dst->reg == 3 && src->kind == OP_REGPAIR && src->reg == 2) { emit(out, 0xF9); return; }
    if (dst->kind == OP_REGPAIR && dst->reg == 3 && src->kind == OP_IX) { emit(out, 0xDD); emit(out, 0xF9); return; }
    if (dst->kind == OP_REGPAIR && dst->reg == 3 && src->kind == OP_IY) { emit(out, 0xFD); emit(out, 0xF9); return; }

    // (IX+d)/(IY+d) <-> plain register
    if (dr >= 0 && dr != 6 && (src->kind == OP_MEM_IX || src->kind == OP_MEM_IY)) {
        emit(out, src->kind == OP_MEM_IX ? 0xDD : 0xFD);
        emit(out, (uint8_t)(0x46 | (dr << 3)));
        emit(out, (uint8_t)eval(ctx, src->expr, out));
        return;
    }
    if ((dst->kind == OP_MEM_IX || dst->kind == OP_MEM_IY) && sr >= 0 && sr != 6) {
        emit(out, dst->kind == OP_MEM_IX ? 0xDD : 0xFD);
        emit(out, (uint8_t)(0x70 | sr));
        emit(out, (uint8_t)eval(ctx, dst->expr, out));
        return;
    }
    if ((dst->kind == OP_MEM_IX || dst->kind == OP_MEM_IY) && src->kind == OP_IMM) {
        emit(out, dst->kind == OP_MEM_IX ? 0xDD : 0xFD);
        emit(out, 0x36);
        emit(out, (uint8_t)eval(ctx, dst->expr, out));
        emit(out, (uint8_t)eval(ctx, src->expr, out));
        return;
    }

    // Undocumented IXH/IXL/IYH/IYL
    {
        int diy = -1, siy = -1;
        int dxr = idx_rfield(dst, &diy);
        int sxr = idx_rfield(src, &siy);
        if (dxr >= 0 && sxr >= 0 && (diy >= 0 || siy >= 0)) {
            if (diy >= 0 && siy >= 0 && diy != siy) { out->err = "cannot mix IX and IY half-registers"; return; }
            emit(out, (diy == 1 || siy == 1) ? 0xFD : 0xDD);
            emit(out, (uint8_t)(0x40 | (dxr << 3) | sxr));
            return;
        }
        if (dxr >= 0 && diy >= 0 && src->kind == OP_IMM) {
            emit(out, diy ? 0xFD : 0xDD);
            emit(out, (uint8_t)(0x06 | (dxr << 3)));
            emit(out, (uint8_t)eval(ctx, src->expr, out));
            return;
        }
    }

    out->err = "invalid LD operands";
}

static void encode_alu(EncCtx *ctx, const char *mnem, Operand *ops, int n, EncOut *out) {
    if (n == 2 && ops[0].kind == OP_REGPAIR && ops[0].reg == 2 && str_eq_ci(mnem, "ADD") && ops[1].kind == OP_REGPAIR) {
        emit(out, (uint8_t)(0x09 | (ops[1].reg << 4)));
        return;
    }
    if (n == 2 && ops[0].kind == OP_REGPAIR && ops[0].reg == 2 && str_eq_ci(mnem, "ADC") && ops[1].kind == OP_REGPAIR) {
        emit(out, 0xED); emit(out, (uint8_t)(0x4A | (ops[1].reg << 4)));
        return;
    }
    if (n == 2 && ops[0].kind == OP_REGPAIR && ops[0].reg == 2 && str_eq_ci(mnem, "SBC") && ops[1].kind == OP_REGPAIR) {
        emit(out, 0xED); emit(out, (uint8_t)(0x42 | (ops[1].reg << 4)));
        return;
    }
    if (n == 2 && ops[0].kind == OP_IX && str_eq_ci(mnem, "ADD")) {
        int r = -1;
        if (ops[1].kind == OP_REGPAIR) r = ops[1].reg;
        else if (ops[1].kind == OP_IX) r = 2;
        if (r >= 0) { emit(out, 0xDD); emit(out, (uint8_t)(0x09 | (r << 4))); return; }
    }
    if (n == 2 && ops[0].kind == OP_IY && str_eq_ci(mnem, "ADD")) {
        int r = -1;
        if (ops[1].kind == OP_REGPAIR) r = ops[1].reg;
        else if (ops[1].kind == OP_IY) r = 2;
        if (r >= 0) { emit(out, 0xFD); emit(out, (uint8_t)(0x09 | (r << 4))); return; }
    }

    if (n == 2 && !(ops[0].kind == OP_REG && ops[0].reg == 7)) { out->err = "invalid ALU operands"; return; }
    if (n != 1 && n != 2) { out->err = "expected 1 or 2 operands"; return; }
    Operand *src = (n == 2) ? &ops[1] : &ops[0];

    int op_code;
    if (str_eq_ci(mnem, "ADD")) op_code = 0;
    else if (str_eq_ci(mnem, "ADC")) op_code = 1;
    else if (str_eq_ci(mnem, "SUB")) op_code = 2;
    else if (str_eq_ci(mnem, "SBC")) op_code = 3;
    else if (str_eq_ci(mnem, "AND")) op_code = 4;
    else if (str_eq_ci(mnem, "XOR")) op_code = 5;
    else if (str_eq_ci(mnem, "OR"))  op_code = 6;
    else if (str_eq_ci(mnem, "CP"))  op_code = 7;
    else { out->err = "unknown ALU mnemonic"; return; }

    int r = simple_rfield(src);
    if (r >= 0) { emit(out, (uint8_t)(0x80 | (op_code << 3) | r)); return; }

    if (src->kind == OP_IMM) {
        static const uint8_t immop[8] = {0xC6, 0xCE, 0xD6, 0xDE, 0xE6, 0xEE, 0xF6, 0xFE};
        emit(out, immop[op_code]);
        emit(out, (uint8_t)eval(ctx, src->expr, out));
        return;
    }

    if (src->kind == OP_MEM_IX || src->kind == OP_MEM_IY) {
        emit(out, src->kind == OP_MEM_IX ? 0xDD : 0xFD);
        emit(out, (uint8_t)(0x86 | (op_code << 3)));
        emit(out, (uint8_t)eval(ctx, src->expr, out));
        return;
    }

    int iy = -1;
    int xr = idx_rfield(src, &iy);
    if (xr >= 0 && iy >= 0) {
        emit(out, iy ? 0xFD : 0xDD);
        emit(out, (uint8_t)(0x80 | (op_code << 3) | xr));
        return;
    }

    out->err = "invalid ALU operand";
}

static void encode_incdec(EncCtx *ctx, const char *mnem, Operand *ops, int n, EncOut *out) {
    if (n != 1) { out->err = "expected 1 operand"; return; }
    int is_inc = str_eq_ci(mnem, "INC");
    Operand *o = &ops[0];

    int r = simple_rfield(o);
    if (r >= 0) { emit(out, (uint8_t)((is_inc ? 0x04 : 0x05) | (r << 3))); return; }

    if (o->kind == OP_REGPAIR) { emit(out, (uint8_t)((is_inc ? 0x03 : 0x0B) | (o->reg << 4))); return; }
    if (o->kind == OP_IX) { emit(out, 0xDD); emit(out, is_inc ? 0x23 : 0x2B); return; }
    if (o->kind == OP_IY) { emit(out, 0xFD); emit(out, is_inc ? 0x23 : 0x2B); return; }

    if (o->kind == OP_MEM_IX || o->kind == OP_MEM_IY) {
        emit(out, o->kind == OP_MEM_IX ? 0xDD : 0xFD);
        emit(out, is_inc ? 0x34 : 0x35);
        emit(out, (uint8_t)eval(ctx, o->expr, out));
        return;
    }

    int iy = -1;
    int xr = idx_rfield(o, &iy);
    if (xr >= 0 && iy >= 0) {
        emit(out, iy ? 0xFD : 0xDD);
        emit(out, (uint8_t)((is_inc ? 0x04 : 0x05) | (xr << 3)));
        return;
    }

    out->err = "invalid operand";
}

static void encode_rotshift(EncCtx *ctx, const char *mnem, Operand *ops, int n, EncOut *out) {
    if (str_eq_ci(mnem, "RLCA")) { emit(out, 0x07); return; }
    if (str_eq_ci(mnem, "RRCA")) { emit(out, 0x0F); return; }
    if (str_eq_ci(mnem, "RLA"))  { emit(out, 0x17); return; }
    if (str_eq_ci(mnem, "RRA"))  { emit(out, 0x1F); return; }
    if (str_eq_ci(mnem, "RLD"))  { emit(out, 0xED); emit(out, 0x6F); return; }
    if (str_eq_ci(mnem, "RRD"))  { emit(out, 0xED); emit(out, 0x67); return; }

    if (n != 1) { out->err = "expected 1 operand"; return; }
    int op;
    if (str_eq_ci(mnem, "RLC")) op = 0;
    else if (str_eq_ci(mnem, "RRC")) op = 1;
    else if (str_eq_ci(mnem, "RL")) op = 2;
    else if (str_eq_ci(mnem, "RR")) op = 3;
    else if (str_eq_ci(mnem, "SLA")) op = 4;
    else if (str_eq_ci(mnem, "SRA")) op = 5;
    else if (str_eq_ci(mnem, "SLL") || str_eq_ci(mnem, "SL1")) op = 6;
    else if (str_eq_ci(mnem, "SRL")) op = 7;
    else { out->err = "unknown mnemonic"; return; }

    Operand *o = &ops[0];
    int r = simple_rfield(o);
    if (r >= 0) { emit(out, 0xCB); emit(out, (uint8_t)((op << 3) | r)); return; }

    if (o->kind == OP_MEM_IX || o->kind == OP_MEM_IY) {
        emit(out, o->kind == OP_MEM_IX ? 0xDD : 0xFD);
        emit(out, 0xCB);
        emit(out, (uint8_t)eval(ctx, o->expr, out));
        emit(out, (uint8_t)((op << 3) | 6));
        return;
    }

    out->err = "invalid operand";
}

static void encode_bit(EncCtx *ctx, const char *mnem, Operand *ops, int n, EncOut *out) {
    if (n != 2) { out->err = "expected 2 operands"; return; }
    if (ops[0].kind != OP_IMM) { out->err = "expected bit number"; return; }
    long bit = eval(ctx, ops[0].expr, out);
    if ((bit < 0 || bit > 7) && !out->err) out->err = "bit number out of range";

    int cat;
    if (str_eq_ci(mnem, "BIT")) cat = 1;
    else if (str_eq_ci(mnem, "RES")) cat = 2;
    else if (str_eq_ci(mnem, "SET")) cat = 3;
    else { out->err = "unknown mnemonic"; return; }

    Operand *o = &ops[1];
    int r = simple_rfield(o);
    if (r >= 0) { emit(out, 0xCB); emit(out, (uint8_t)((cat << 6) | ((bit & 7) << 3) | r)); return; }

    if (o->kind == OP_MEM_IX || o->kind == OP_MEM_IY) {
        emit(out, o->kind == OP_MEM_IX ? 0xDD : 0xFD);
        emit(out, 0xCB);
        emit(out, (uint8_t)eval(ctx, o->expr, out));
        emit(out, (uint8_t)((cat << 6) | ((bit & 7) << 3) | 6));
        return;
    }

    out->err = "invalid operand";
}

static const char *CC_NAMES[8] = {"NZ", "Z", "NC", "C", "PO", "PE", "P", "M"};

static int match_condition(const char *text, int allow_all) {
    int max = allow_all ? 8 : 4; // JR/DJNZ only allow NZ,Z,NC,C
    for (int i = 0; i < max; i++) {
        if (str_eq_ci(text, CC_NAMES[i])) return i;
    }
    return -1;
}

static void encode_jump(EncCtx *ctx, const char *mnem, char raw_ops[2][200], Operand *ops, int n, EncOut *out) {
    if (str_eq_ci(mnem, "JP")) {
        if (n == 2) {
            int cc = match_condition(raw_ops[0], 1);
            if (cc < 0) { out->err = "invalid condition"; return; }
            emit(out, (uint8_t)(0xC2 | (cc << 3)));
            emit_word(out, (uint16_t)eval(ctx, ops[1].expr, out));
            return;
        }
        if (n == 1) {
            if (ops[0].kind == OP_MEM_HL) { emit(out, 0xE9); return; }
            if (ops[0].kind == OP_MEM_IX) { emit(out, 0xDD); emit(out, 0xE9); return; }
            if (ops[0].kind == OP_MEM_IY) { emit(out, 0xFD); emit(out, 0xE9); return; }
            emit(out, 0xC3);
            emit_word(out, (uint16_t)eval(ctx, ops[0].expr, out));
            return;
        }
        out->err = "expected operand";
        return;
    }

    if (str_eq_ci(mnem, "JR")) {
        Operand *target;
        int cc = -1;
        if (n == 2) {
            cc = match_condition(raw_ops[0], 0);
            if (cc < 0) { out->err = "invalid condition (JR only allows NZ/Z/NC/C)"; return; }
            target = &ops[1];
        } else if (n == 1) {
            target = &ops[0];
        } else {
            out->err = "expected operand";
            return;
        }
        emit(out, cc >= 0 ? (uint8_t)(0x20 | (cc << 3)) : 0x18);
        long addr = eval(ctx, target->expr, out);
        long disp = addr - (ctx->pc + 2);
        if (ctx->pass == 2 && (disp < -128 || disp > 127) && !out->err) out->err = "relative jump out of range";
        emit(out, (uint8_t)disp);
        return;
    }

    if (str_eq_ci(mnem, "DJNZ")) {
        if (n != 1) { out->err = "expected operand"; return; }
        emit(out, 0x10);
        long addr = eval(ctx, ops[0].expr, out);
        long disp = addr - (ctx->pc + 2);
        if (ctx->pass == 2 && (disp < -128 || disp > 127) && !out->err) out->err = "relative jump out of range";
        emit(out, (uint8_t)disp);
        return;
    }

    if (str_eq_ci(mnem, "CALL")) {
        if (n == 2) {
            int cc = match_condition(raw_ops[0], 1);
            if (cc < 0) { out->err = "invalid condition"; return; }
            emit(out, (uint8_t)(0xC4 | (cc << 3)));
            emit_word(out, (uint16_t)eval(ctx, ops[1].expr, out));
            return;
        }
        if (n == 1) {
            emit(out, 0xCD);
            emit_word(out, (uint16_t)eval(ctx, ops[0].expr, out));
            return;
        }
        out->err = "expected operand";
        return;
    }

    if (str_eq_ci(mnem, "RET")) {
        if (n == 0) { emit(out, 0xC9); return; }
        if (n == 1) {
            int cc = match_condition(raw_ops[0], 1);
            if (cc < 0) { out->err = "invalid condition"; return; }
            emit(out, (uint8_t)(0xC0 | (cc << 3)));
            return;
        }
        out->err = "too many operands";
        return;
    }

    if (str_eq_ci(mnem, "RETI")) { emit(out, 0xED); emit(out, 0x4D); return; }
    if (str_eq_ci(mnem, "RETN")) { emit(out, 0xED); emit(out, 0x45); return; }

    if (str_eq_ci(mnem, "RST")) {
        if (n != 1) { out->err = "expected operand"; return; }
        long v = eval(ctx, ops[0].expr, out);
        if (v != 0x00 && v != 0x08 && v != 0x10 && v != 0x18 &&
            v != 0x20 && v != 0x28 && v != 0x30 && v != 0x38) {
            if (!out->err) out->err = "invalid restart address";
            return;
        }
        emit(out, (uint8_t)(0xC7 | v));
        return;
    }
}

static void encode_stack(const char *mnem, Operand *ops, int n, EncOut *out) {
    if (n != 1) { out->err = "expected operand"; return; }
    int is_push = str_eq_ci(mnem, "PUSH");
    Operand *o = &ops[0];

    int qq = -1;
    if (o->kind == OP_REGPAIR && o->reg != 3) qq = o->reg;
    else if (o->kind == OP_AF) qq = 3;

    if (qq >= 0) { emit(out, (uint8_t)((is_push ? 0xC5 : 0xC1) | (qq << 4))); return; }
    if (o->kind == OP_IX) { emit(out, 0xDD); emit(out, is_push ? 0xE5 : 0xE1); return; }
    if (o->kind == OP_IY) { emit(out, 0xFD); emit(out, is_push ? 0xE5 : 0xE1); return; }

    out->err = "invalid operand";
}

static void encode_ex(Operand *ops, int n, EncOut *out) {
    if (n != 2) { out->err = "expected 2 operands"; return; }
    Operand *a = &ops[0], *b = &ops[1];
    if (a->kind == OP_REGPAIR && a->reg == 1 && b->kind == OP_REGPAIR && b->reg == 2) { emit(out, 0xEB); return; }
    if (a->kind == OP_AF && b->kind == OP_AF_ALT) { emit(out, 0x08); return; }
    if (a->kind == OP_MEM_SP && b->kind == OP_REGPAIR && b->reg == 2) { emit(out, 0xE3); return; }
    if (a->kind == OP_MEM_SP && b->kind == OP_IX) { emit(out, 0xDD); emit(out, 0xE3); return; }
    if (a->kind == OP_MEM_SP && b->kind == OP_IY) { emit(out, 0xFD); emit(out, 0xE3); return; }
    out->err = "invalid operands";
}

static void encode_block(const char *mnem, EncOut *out) {
    static const struct { const char *name; uint8_t byte; } table[] = {
        {"LDI", 0xA0}, {"LDD", 0xA8}, {"LDIR", 0xB0}, {"LDDR", 0xB8},
        {"CPI", 0xA1}, {"CPD", 0xA9}, {"CPIR", 0xB1}, {"CPDR", 0xB9},
        {"INI", 0xA2}, {"IND", 0xAA}, {"INIR", 0xB2}, {"INDR", 0xBA},
        {"OUTI", 0xA3}, {"OUTD", 0xAB}, {"OTIR", 0xB3}, {"OTDR", 0xBB},
    };
    for (size_t i = 0; i < sizeof(table) / sizeof(table[0]); i++) {
        if (str_eq_ci(mnem, table[i].name)) { emit(out, 0xED); emit(out, table[i].byte); return; }
    }
    out->err = "unknown mnemonic";
}

static void encode_io(EncCtx *ctx, const char *mnem, Operand *ops, int n, EncOut *out) {
    if (str_eq_ci(mnem, "IN")) {
        if (n != 2) { out->err = "expected 2 operands"; return; }
        if (ops[0].kind == OP_REG && ops[0].reg == 7 && ops[1].kind == OP_MEM_IMM) {
            emit(out, 0xDB);
            emit(out, (uint8_t)eval(ctx, ops[1].expr, out));
            return;
        }
        int r = simple_rfield(&ops[0]);
        if (r >= 0 && ops[1].kind == OP_MEM_C) {
            emit(out, 0xED); emit(out, (uint8_t)(0x40 | (r << 3)));
            return;
        }
        out->err = "invalid operands";
        return;
    }
    if (str_eq_ci(mnem, "OUT")) {
        if (n != 2) { out->err = "expected 2 operands"; return; }
        if (ops[0].kind == OP_MEM_IMM && ops[1].kind == OP_REG && ops[1].reg == 7) {
            emit(out, 0xD3);
            emit(out, (uint8_t)eval(ctx, ops[0].expr, out));
            return;
        }
        int r = simple_rfield(&ops[1]);
        if (ops[0].kind == OP_MEM_C && r >= 0) {
            emit(out, 0xED); emit(out, (uint8_t)(0x41 | (r << 3)));
            return;
        }
        out->err = "invalid operands";
        return;
    }
}

static void encode_misc(EncCtx *ctx, const char *mnem, Operand *ops, int n, EncOut *out) {
    if (str_eq_ci(mnem, "NOP"))  { emit(out, 0x00); return; }
    if (str_eq_ci(mnem, "HALT")) { emit(out, 0x76); return; }
    if (str_eq_ci(mnem, "DI"))   { emit(out, 0xF3); return; }
    if (str_eq_ci(mnem, "EI"))   { emit(out, 0xFB); return; }
    if (str_eq_ci(mnem, "DAA"))  { emit(out, 0x27); return; }
    if (str_eq_ci(mnem, "CPL"))  { emit(out, 0x2F); return; }
    if (str_eq_ci(mnem, "NEG"))  { emit(out, 0xED); emit(out, 0x44); return; }
    if (str_eq_ci(mnem, "CCF"))  { emit(out, 0x3F); return; }
    if (str_eq_ci(mnem, "SCF"))  { emit(out, 0x37); return; }
    if (str_eq_ci(mnem, "EXX"))  { emit(out, 0xD9); return; }
    if (str_eq_ci(mnem, "IM")) {
        if (n != 1) { out->err = "expected IM 0/1/2"; return; }
        long mode = eval(ctx, ops[0].expr, out);
        if (mode == 0) { emit(out, 0xED); emit(out, 0x46); return; }
        if (mode == 1) { emit(out, 0xED); emit(out, 0x56); return; }
        if (mode == 2) { emit(out, 0xED); emit(out, 0x5E); return; }
        if (!out->err) out->err = "invalid interrupt mode";
        return;
    }
    out->err = "unknown mnemonic";
}

void encode_instruction(EncCtx *ctx, const char *mnemonic, const char *operand_text, EncOut *out) {
    memset(out, 0, sizeof(*out));

    char raw_ops[2][200];
    int n = split_operands(operand_text, raw_ops);
    Operand ops[2];
    for (int i = 0; i < n; i++) parse_operand(raw_ops[i], &ops[i]);

    const char *m = mnemonic;

    if (str_eq_ci(m, "LD")) {
        if (n != 2) { out->err = "LD expects 2 operands"; return; }
        encode_ld(ctx, &ops[0], &ops[1], out);
        return;
    }
    if (str_eq_ci(m, "PUSH") || str_eq_ci(m, "POP")) { encode_stack(m, ops, n, out); return; }
    if (str_eq_ci(m, "EX")) { encode_ex(ops, n, out); return; }

    if (str_eq_ci(m, "ADD") || str_eq_ci(m, "ADC") || str_eq_ci(m, "SUB") || str_eq_ci(m, "SBC") ||
        str_eq_ci(m, "AND") || str_eq_ci(m, "XOR") || str_eq_ci(m, "OR")  || str_eq_ci(m, "CP")) {
        encode_alu(ctx, m, ops, n, out);
        return;
    }

    if (str_eq_ci(m, "INC") || str_eq_ci(m, "DEC")) { encode_incdec(ctx, m, ops, n, out); return; }

    if (str_eq_ci(m, "RLCA") || str_eq_ci(m, "RRCA") || str_eq_ci(m, "RLA") || str_eq_ci(m, "RRA") ||
        str_eq_ci(m, "RLD")  || str_eq_ci(m, "RRD")  ||
        str_eq_ci(m, "RLC")  || str_eq_ci(m, "RRC")  || str_eq_ci(m, "RL") || str_eq_ci(m, "RR") ||
        str_eq_ci(m, "SLA")  || str_eq_ci(m, "SRA")  || str_eq_ci(m, "SLL") || str_eq_ci(m, "SL1") ||
        str_eq_ci(m, "SRL")) {
        encode_rotshift(ctx, m, ops, n, out);
        return;
    }

    if (str_eq_ci(m, "BIT") || str_eq_ci(m, "SET") || str_eq_ci(m, "RES")) { encode_bit(ctx, m, ops, n, out); return; }

    if (str_eq_ci(m, "JP") || str_eq_ci(m, "JR") || str_eq_ci(m, "DJNZ") || str_eq_ci(m, "CALL") ||
        str_eq_ci(m, "RET") || str_eq_ci(m, "RETI") || str_eq_ci(m, "RETN") || str_eq_ci(m, "RST")) {
        encode_jump(ctx, m, raw_ops, ops, n, out);
        return;
    }

    if (str_eq_ci(m, "IN") || str_eq_ci(m, "OUT")) { encode_io(ctx, m, ops, n, out); return; }

    {
        static const char *block_names[] = {
            "LDI", "LDD", "LDIR", "LDDR", "CPI", "CPD", "CPIR", "CPDR",
            "INI", "IND", "INIR", "INDR", "OUTI", "OUTD", "OTIR", "OTDR",
        };
        for (size_t i = 0; i < sizeof(block_names) / sizeof(block_names[0]); i++) {
            if (str_eq_ci(m, block_names[i])) { encode_block(m, out); return; }
        }
    }

    if (str_eq_ci(m, "NOP") || str_eq_ci(m, "HALT") || str_eq_ci(m, "DI") || str_eq_ci(m, "EI") ||
        str_eq_ci(m, "DAA") || str_eq_ci(m, "CPL") || str_eq_ci(m, "NEG") || str_eq_ci(m, "CCF") ||
        str_eq_ci(m, "SCF") || str_eq_ci(m, "EXX") || str_eq_ci(m, "IM")) {
        encode_misc(ctx, m, ops, n, out);
        return;
    }

    out->err = "unknown mnemonic";
}

int is_known_mnemonic(const char *mnemonic) {
    static const char *names[] = {
        "LD", "PUSH", "POP", "EX", "EXX",
        "ADD", "ADC", "SUB", "SBC", "AND", "XOR", "OR", "CP",
        "INC", "DEC",
        "RLCA", "RRCA", "RLA", "RRA", "RLD", "RRD",
        "RLC", "RRC", "RL", "RR", "SLA", "SRA", "SLL", "SL1", "SRL",
        "BIT", "SET", "RES",
        "JP", "JR", "DJNZ", "CALL", "RET", "RETI", "RETN", "RST",
        "IN", "OUT",
        "LDI", "LDD", "LDIR", "LDDR", "CPI", "CPD", "CPIR", "CPDR",
        "INI", "IND", "INIR", "INDR", "OUTI", "OUTD", "OTIR", "OTDR",
        "NOP", "HALT", "DI", "EI", "DAA", "CPL", "NEG", "CCF", "SCF", "IM",
    };
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); i++) {
        if (str_eq_ci(mnemonic, names[i])) return 1;
    }
    return 0;
}
