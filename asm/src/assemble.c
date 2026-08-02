#include <ctype.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include "assemble.h"
#include "expr.h"
#include "encode.h"

static void str_trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

// Cuts off a trailing ';' comment, but not one that appears inside a
// single- or double-quoted literal (so "it's" or a string containing ';'
// isn't mistaken for a comment start).
static void strip_comment(char *line) {
    int in_squote = 0, in_dquote = 0;
    for (char *p = line; *p; p++) {
        if (*p == '\'' && !in_dquote) in_squote = !in_squote;
        else if (*p == '"' && !in_squote) in_dquote = !in_dquote;
        else if (*p == ';' && !in_squote && !in_dquote) { *p = '\0'; return; }
    }
}

static long eval_directive_expr(AsmCtx *ctx, const char *text, int *unresolved) {
    ExprEnv env;
    env.symtab = ctx->symtab;
    env.pc = ctx->pc;
    env.pass = ctx->pass;
    env.unresolved = 0;
    env.err = NULL;

    const char *p = text;
    long v = expr_parse(&p, &env);
    if (unresolved) *unresolved = env.unresolved;
    return v;
}

static void asm_emit(AsmCtx *ctx, uint8_t b) {
    if (ctx->pc >= 0 && ctx->pc < IMAGE_SIZE) {
        if (ctx->pass == 2) ctx->image[ctx->pc] = b;
        if (!ctx->had_output || ctx->pc < ctx->min_addr) ctx->min_addr = ctx->pc;
        if (ctx->pc + 1 > ctx->max_addr) ctx->max_addr = ctx->pc + 1;
        ctx->had_output = 1;
    }
    ctx->pc++;
}

static int assemble_db(AsmCtx *ctx, const char *operand_text, LineResult *r) {
    const char *s = operand_text;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        if (!*s) break;

        if (*s == '"' || *s == '\'') {
            char quote = *s++;
            while (*s && *s != quote) asm_emit(ctx, (uint8_t)*s++);
            if (*s == quote) s++;
            continue;
        }

        const char *start = s;
        int depth = 0;
        while (*s && (*s != ',' || depth > 0)) {
            if (*s == '(') depth++;
            else if (*s == ')') depth--;
            s++;
        }
        char field[256];
        size_t len = (size_t)(s - start);
        if (len >= sizeof(field)) len = sizeof(field) - 1;
        memcpy(field, start, len);
        field[len] = '\0';
        str_trim(field);
        if (field[0]) {
            int unresolved = 0;
            long v = eval_directive_expr(ctx, field, &unresolved);
            if (ctx->pass == 2 && unresolved) { r->err = "undefined symbol in DB"; return -1; }
            asm_emit(ctx, (uint8_t)v);
        }
    }
    return 0;
}

static int assemble_dw(AsmCtx *ctx, const char *operand_text, LineResult *r) {
    const char *s = operand_text;
    while (*s) {
        while (*s == ' ' || *s == '\t' || *s == ',') s++;
        if (!*s) break;

        const char *start = s;
        int depth = 0;
        while (*s && (*s != ',' || depth > 0)) {
            if (*s == '(') depth++;
            else if (*s == ')') depth--;
            s++;
        }
        char field[256];
        size_t len = (size_t)(s - start);
        if (len >= sizeof(field)) len = sizeof(field) - 1;
        memcpy(field, start, len);
        field[len] = '\0';
        str_trim(field);
        if (field[0]) {
            int unresolved = 0;
            long v = eval_directive_expr(ctx, field, &unresolved);
            if (ctx->pass == 2 && unresolved) { r->err = "undefined symbol in DW"; return -1; }
            asm_emit(ctx, (uint8_t)(v & 0xFF));
            asm_emit(ctx, (uint8_t)((v >> 8) & 0xFF));
        }
    }
    return 0;
}

// DS count[,fill] - reserves `count` bytes, each set to `fill` (default 0).
static int assemble_ds(AsmCtx *ctx, const char *operand_text, LineResult *r) {
    char count_text[128] = {0}, fill_text[128] = {0};
    const char *comma = strchr(operand_text, ',');
    if (comma) {
        size_t clen = (size_t)(comma - operand_text);
        if (clen >= sizeof(count_text)) clen = sizeof(count_text) - 1;
        memcpy(count_text, operand_text, clen);
        count_text[clen] = '\0';
        strncpy(fill_text, comma + 1, sizeof(fill_text) - 1);
    } else {
        strncpy(count_text, operand_text, sizeof(count_text) - 1);
    }
    str_trim(count_text);
    str_trim(fill_text);

    int unresolved = 0;
    long count = eval_directive_expr(ctx, count_text, &unresolved);
    if (ctx->pass == 2 && unresolved) { r->err = "undefined symbol in DS count"; return -1; }
    if (count < 0) { r->err = "DS count cannot be negative"; return -1; }

    long fill = 0;
    if (fill_text[0]) {
        fill = eval_directive_expr(ctx, fill_text, &unresolved);
        if (ctx->pass == 2 && unresolved) { r->err = "undefined symbol in DS fill value"; return -1; }
    }

    for (long i = 0; i < count; i++) asm_emit(ctx, (uint8_t)fill);
    return 0;
}

int assemble_line(AsmCtx *ctx, const char *line_in, LineResult *r) {
    memset(r, 0, sizeof(*r));

    char line[512];
    strncpy(line, line_in, sizeof(line) - 1);
    line[sizeof(line) - 1] = '\0';

    strip_comment(line);
    str_trim(line);

    if (line[0] == '\0') { r->kind = LINE_NORMAL; return 0; }

    char *p = line;

    // Optional label: identifier immediately followed by ':'.
    char label[SYM_NAME_MAX] = {0};
    {
        char *q = p;
        if (isalpha((unsigned char)*q) || *q == '_' || *q == '.') {
            char *start = q;
            while (isalnum((unsigned char)*q) || *q == '_' || *q == '.') q++;
            if (*q == ':') {
                size_t len = (size_t)(q - start);
                if (len >= sizeof(label)) len = sizeof(label) - 1;
                memcpy(label, start, len);
                label[len] = '\0';
                p = q + 1;
            }
        }
    }
    str_trim(p);

    char mnem[32] = {0};
    char *rest = p;
    {
        char *start = p;
        while (isalnum((unsigned char)*rest) || *rest == '_') rest++;
        size_t len = (size_t)(rest - start);
        if (len >= sizeof(mnem)) len = sizeof(mnem) - 1;
        memcpy(mnem, start, len);
        mnem[len] = '\0';
    }
    str_trim(rest);

    // Colon-less label: "name  instruction  operands", where `name` isn't
    // itself a directive or a real mnemonic (zexall.z80 uses this in a
    // couple of spots, e.g. "bdos	push	af", "crcval	ds	4"). Reinterpret
    // `mnem` as the label and re-derive mnem/rest from what follows it.
    if (label[0] == '\0' && mnem[0] != '\0' && rest[0] != '\0' &&
        !is_known_mnemonic(mnem) &&
        strcasecmp(mnem, "EQU") != 0 && strcasecmp(mnem, "ORG") != 0 &&
        strcasecmp(mnem, "END") != 0 && strcasecmp(mnem, "ASEG") != 0 &&
        strcasecmp(mnem, "CSEG") != 0 && strcasecmp(mnem, "DSEG") != 0 &&
        strcasecmp(mnem, "DB") != 0 && strcasecmp(mnem, "DEFB") != 0 &&
        strcasecmp(mnem, "DW") != 0 && strcasecmp(mnem, "DEFW") != 0 &&
        strcasecmp(mnem, "DS") != 0 && strcasecmp(mnem, "DEFS") != 0 &&
        strcasecmp(mnem, "IF") != 0 && strcasecmp(mnem, "ELSE") != 0 &&
        strcasecmp(mnem, "ENDIF") != 0 && strcasecmp(mnem, "ERROR") != 0) {
        strncpy(label, mnem, sizeof(label) - 1);
        label[sizeof(label) - 1] = '\0';

        char *start = rest;
        char *q = start;
        while (isalnum((unsigned char)*q) || *q == '_') q++;
        size_t len2 = (size_t)(q - start);
        if (len2 >= sizeof(mnem)) len2 = sizeof(mnem) - 1;
        memcpy(mnem, start, len2);
        mnem[len2] = '\0';
        rest = q;
        str_trim(rest);
    }

    // IF/ELSE/ENDIF are handled unconditionally (even while skipping a
    // false branch) so nesting stays tracked and a block can reactivate.
    if (strcasecmp(mnem, "IF") == 0) {
        if (ctx->cond_depth >= MAX_COND_DEPTH) { r->err = "IF nesting too deep"; return -1; }
        int parent_active = (ctx->cond_depth == 0) || ctx->cond_active[ctx->cond_depth - 1];
        int taken = 0;
        if (parent_active) {
            int unresolved = 0;
            long v = eval_directive_expr(ctx, rest, &unresolved);
            if (ctx->pass == 2 && unresolved) { r->err = "undefined symbol in IF"; return -1; }
            taken = (v != 0);
        }
        ctx->cond_parent_active[ctx->cond_depth] = parent_active;
        ctx->cond_taken[ctx->cond_depth] = taken;
        ctx->cond_active[ctx->cond_depth] = parent_active && taken;
        ctx->cond_depth++;
        return 0;
    }
    if (strcasecmp(mnem, "ELSE") == 0) {
        if (ctx->cond_depth == 0) { r->err = "ELSE without IF"; return -1; }
        int d = ctx->cond_depth - 1;
        int parent_active = ctx->cond_parent_active[d];
        ctx->cond_active[d] = parent_active && !ctx->cond_taken[d];
        ctx->cond_taken[d] = 1; // a stray second ELSE at this level stays inactive
        return 0;
    }
    if (strcasecmp(mnem, "ENDIF") == 0) {
        if (ctx->cond_depth == 0) { r->err = "ENDIF without IF"; return -1; }
        ctx->cond_depth--;
        return 0;
    }

    if (ctx->cond_depth > 0 && !ctx->cond_active[ctx->cond_depth - 1]) return 0; // inside a false branch

    if (strcasecmp(mnem, "ERROR") == 0) {
        static char errbuf[320];
        snprintf(errbuf, sizeof(errbuf), "ERROR: %s", rest);
        r->err = errbuf;
        return -1;
    }

    // Bare label line: define it at the current address and stop.
    if (label[0] && mnem[0] == '\0') {
        if (!symtab_define(ctx->symtab, label, ctx->pc)) {
            r->err = "label redefined with a different value";
            return -1;
        }
        return 0;
    }

    if (mnem[0] == '\0') return 0;

    if (strcasecmp(mnem, "EQU") == 0) {
        if (!label[0]) { r->err = "EQU requires a label"; return -1; }
        int unresolved = 0;
        long v = eval_directive_expr(ctx, rest, &unresolved);
        if (ctx->pass == 2 && unresolved) { r->err = "undefined symbol in EQU"; return -1; }
        if (!symtab_define(ctx->symtab, label, v)) {
            r->err = "label redefined with a different value";
            return -1;
        }
        return 0;
    }

    // Any other directive/instruction with a label defines it at the
    // current address (standard "label: instruction" form).
    if (label[0]) {
        if (!symtab_define(ctx->symtab, label, ctx->pc)) {
            r->err = "label redefined with a different value";
            return -1;
        }
    }

    if (strcasecmp(mnem, "ORG") == 0) {
        int unresolved = 0;
        long addr = eval_directive_expr(ctx, rest, &unresolved);
        if (ctx->pass == 2 && unresolved) { r->err = "undefined symbol in ORG"; return -1; }
        r->kind = LINE_ORG;
        r->org_addr = addr;
        return 0;
    }

    if (strcasecmp(mnem, "END") == 0) return 0;
    if (strcasecmp(mnem, "ASEG") == 0 || strcasecmp(mnem, "CSEG") == 0 || strcasecmp(mnem, "DSEG") == 0) {
        return 0; // segment selection - irrelevant to this single flat-image assembler
    }

    if (strcasecmp(mnem, "DB") == 0 || strcasecmp(mnem, "DEFB") == 0) return assemble_db(ctx, rest, r);
    if (strcasecmp(mnem, "DW") == 0 || strcasecmp(mnem, "DEFW") == 0) return assemble_dw(ctx, rest, r);
    if (strcasecmp(mnem, "DS") == 0 || strcasecmp(mnem, "DEFS") == 0) return assemble_ds(ctx, rest, r);

    // --- Instruction ---
    EncCtx ectx;
    ectx.symtab = ctx->symtab;
    ectx.pc = ctx->pc;
    ectx.pass = ctx->pass;
    ectx.unresolved = 0;

    EncOut out;
    encode_instruction(&ectx, mnem, rest, &out);
    if (out.err) { r->err = out.err; return -1; }

    for (int i = 0; i < out.nbytes; i++) asm_emit(ctx, out.bytes[i]);
    return 0;
}
