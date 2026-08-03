#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "expr.h"

static void skip_ws(const char **s) {
    while (**s == ' ' || **s == '\t') (*s)++;
}

static int is_ident_start(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == '.';
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

// Parses a numeric literal at *s: decimal (123), hex with trailing h/H
// (0FFh - must start with a digit), hex with a 0x/0X prefix (0xFF), or
// binary with a trailing b/B (1100000b - a common vintage-assembler
// convention real CP/M-era source uses, e.g. Digital Research's own CCP).
static long parse_number(const char **s) {
    const char *p = *s;
    const char *start = p;

    if (p[0] == '0' && (p[1] == 'x' || p[1] == 'X')) {
        p += 2;
        const char *digits = p;
        while (isxdigit((unsigned char)*p)) p++;
        long val = strtol(digits, NULL, 16);
        *s = p;
        return val;
    }

    // Scan the *full* run of alphanumeric characters before deciding the
    // radix from what it ends with, rather than scanning hex digits
    // first and only then checking for a trailing 'h' (the previous
    // approach here): 'b'/'B' is itself a valid hex digit (11), so a
    // hex-digit-first scan greedily swallows a binary literal's own 'b'
    // suffix as if it were part of the number, then finds no 'h' after
    // it and silently falls through to decimal - misparsing e.g.
    // "1100000b" as the literal decimal 1100000, later truncated to a
    // wildly wrong byte value wherever it's used. Deciding from the full
    // token's *last* character first avoids that ambiguity entirely.
    while (isalnum((unsigned char)*p)) p++;
    size_t len = (size_t)(p - start);

    if (len > 1 && (start[len - 1] == 'h' || start[len - 1] == 'H')) {
        long val = strtol(start, NULL, 16); // stops at the 'h' - base 16 excludes it
        *s = p;
        return val;
    }

    if (len > 1 && (start[len - 1] == 'b' || start[len - 1] == 'B')) {
        int all_binary = 1;
        for (size_t i = 0; i + 1 < len; i++) {
            if (start[i] != '0' && start[i] != '1') { all_binary = 0; break; }
        }
        if (all_binary) {
            long val = strtol(start, NULL, 2); // stops at the 'b' - base 2 excludes it
            *s = p;
            return val;
        }
    }

    // Not a hex/binary-with-suffix literal (e.g. "ff" with no trailing
    // 'h' isn't valid decimal) - fall back to plain decimal digits only.
    p = start;
    while (isdigit((unsigned char)*p)) p++;
    long val = strtol(start, NULL, 10);
    *s = p;
    return val;
}

static long expr_or(const char **s, ExprEnv *env);

// primary := NUMBER | '$' | 'low' '(' expr ')' | 'high' '(' expr ')'
//          | IDENT | '\'' char '\'' | '(' expr ')'
static long parse_primary(const char **s, ExprEnv *env) {
    skip_ws(s);
    const char *p = *s;

    if (*p == '$') {
        // "$FF"-style dollar-prefixed hex (a common vintage-assembler
        // convention - SARGON's source uses it extensively for its
        // piece-graphics data) vs. plain "$" for the current address
        // (used elsewhere, e.g. "$-TBASE" or CCP's "$+800h") -
        // disambiguated by whether a hex digit immediately follows: two
        // adjacent primaries with no operator between them (a bare "$"
        // followed directly by more digits) would never be meaningful
        // otherwise, so it's safe to treat this as one dollar-hex token.
        // Without this, "$83" silently parsed as just "$" (the digits
        // discarded entirely, no error) - quietly scrambling every
        // piece-graphics byte using this syntax into a sequential
        // current-address pattern instead of the intended literal value.
        if (isxdigit((unsigned char)p[1])) {
            p++;
            const char *digits = p;
            while (isxdigit((unsigned char)*p)) p++;
            long val = strtol(digits, NULL, 16);
            *s = p;
            return val;
        }
        *s = p + 1;
        return env->pc;
    }

    if (*p == '\'') {
        p++;
        char c = *p ? *p++ : '\0';
        if (*p == '\'') p++;
        *s = p;
        return (unsigned char)c;
    }

    if (*p == '(') {
        p++;
        long v = expr_or(&p, env);
        skip_ws(&p);
        if (*p == ')') p++;
        else if (!env->err) env->err = "expected ')'";
        *s = p;
        return v;
    }

    if (isdigit((unsigned char)*p)) {
        long v = parse_number(&p);
        *s = p;
        return v;
    }

    if (is_ident_start(*p)) {
        const char *start = p;
        while (is_ident_char(*p)) p++;
        size_t len = (size_t)(p - start);

        char name[SYM_NAME_MAX];
        if (len >= sizeof(name)) len = sizeof(name) - 1;
        memcpy(name, start, len);
        name[len] = '\0';

        Symbol *sym = symtab_find(env->symtab, name);
        *s = p;
        if (sym && sym->defined) return sym->value;
        if (env->pass == 1) {
            env->unresolved = 1;
            return 0;
        }
        if (!env->err) env->err = "undefined symbol";
        return 0;
    }

    if (!env->err) env->err = "expected expression";
    return 0;
}

// unary := ('-' | '+' | '~' | 'low' | 'high')* primary
//
// low/high are unary prefix operators, not just function-call syntax -
// "low msbt" (no parens) is the common assembler form and needs to work
// alongside "low(msbt)"; treating them as a prefix operator here handles
// both, since parse_primary's '(' case already does generic grouping.
static long parse_unary(const char **s, ExprEnv *env) {
    skip_ws(s);
    if (**s == '-') { (*s)++; return -parse_unary(s, env); }
    if (**s == '+') { (*s)++; return parse_unary(s, env); }
    if (**s == '~') { (*s)++; return ~parse_unary(s, env); }

    if (is_ident_start(**s)) {
        const char *p = *s;
        const char *start = p;
        while (is_ident_char(*p)) p++;
        size_t len = (size_t)(p - start);
        if (len == 3 && strncasecmp(start, "low", 3) == 0) {
            *s = p;
            return parse_unary(s, env) & 0xFF;
        }
        if (len == 4 && strncasecmp(start, "high", 4) == 0) {
            *s = p;
            return (parse_unary(s, env) >> 8) & 0xFF;
        }
    }

    return parse_primary(s, env);
}

// term := unary (('*' | '/' | '%') unary)*
static long parse_term(const char **s, ExprEnv *env) {
    long v = parse_unary(s, env);
    for (;;) {
        skip_ws(s);
        char op = **s;
        if (op != '*' && op != '/' && op != '%') break;
        (*s)++;
        long rhs = parse_unary(s, env);
        if (op == '*') v *= rhs;
        else if (rhs == 0) { if (!env->err) env->err = "division by zero"; }
        else v = (op == '/') ? v / rhs : v % rhs;
    }
    return v;
}

// addsub := term (('+' | '-') term)*
static long parse_addsub(const char **s, ExprEnv *env) {
    long v = parse_term(s, env);
    for (;;) {
        skip_ws(s);
        char op = **s;
        if (op != '+' && op != '-') break;
        (*s)++;
        long rhs = parse_term(s, env);
        v = (op == '+') ? v + rhs : v - rhs;
    }
    return v;
}

// shift := addsub (('<<' | '>>') addsub)*
// Binds tighter than bitwise but looser than +/- (matching C's precedence,
// which real-world TASM-style sources like Tasty Basic's tastybasic.asm
// rely on: its keyword-dispatch table is built entirely with a macro
// expanding to "(addr >> 8) + 080h" / "addr & 0ffh"). Checking for a
// doubled '<'/'>' here, before parse_relational ever sees the input,
// keeps a lone '<'/'>'/'>=' unambiguous as a relational operator - only
// two of the same char in a row is ever treated as a shift.
static long parse_shift(const char **s, ExprEnv *env) {
    long v = parse_addsub(s, env);
    for (;;) {
        skip_ws(s);
        const char *p = *s;
        if (p[0] == '<' && p[1] == '<') {
            *s = p + 2;
            long rhs = parse_addsub(s, env);
            v <<= rhs;
        } else if (p[0] == '>' && p[1] == '>') {
            *s = p + 2;
            long rhs = parse_addsub(s, env);
            v >>= rhs;
        } else {
            break;
        }
    }
    return v;
}

// bitwise := shift (('&' | '|' | '^') shift)*
static long expr_or(const char **s, ExprEnv *env) {
    long v = parse_shift(s, env);
    for (;;) {
        skip_ws(s);
        char op = **s;
        if (op != '&' && op != '|' && op != '^') break;
        (*s)++;
        long rhs = parse_shift(s, env);
        if (op == '&') v &= rhs;
        else if (op == '|') v |= rhs;
        else v ^= rhs;
    }
    return v;
}

typedef enum { RELOP_NONE = -1, RELOP_EQ, RELOP_NE, RELOP_LT, RELOP_LE, RELOP_GT, RELOP_GE } RelOp;

// Peeks a relational operator at *s (symbolic: = <> < <= > >=, or word
// form: eq ne lt le gt ge) and consumes it if found; leaves *s untouched
// otherwise. Word forms only match a whole identifier, so a symbol named
// e.g. "ne_flag" is never mistaken for the "ne" operator.
static RelOp match_relop(const char **s) {
    const char *p = *s;
    skip_ws(&p);

    if (p[0] == '=') { *s = p + 1; return RELOP_EQ; }
    if (p[0] == '<' && p[1] == '>') { *s = p + 2; return RELOP_NE; }
    if (p[0] == '<' && p[1] == '=') { *s = p + 2; return RELOP_LE; }
    if (p[0] == '>' && p[1] == '=') { *s = p + 2; return RELOP_GE; }
    if (p[0] == '<') { *s = p + 1; return RELOP_LT; }
    if (p[0] == '>') { *s = p + 1; return RELOP_GT; }

    if (is_ident_start(*p)) {
        const char *start = p;
        const char *q = p;
        while (is_ident_char(*q)) q++;
        size_t len = (size_t)(q - start);
        if (len == 2) {
            char word[3];
            memcpy(word, start, 2);
            word[2] = '\0';
            RelOp op = RELOP_NONE;
            if (strcasecmp(word, "eq") == 0) op = RELOP_EQ;
            else if (strcasecmp(word, "ne") == 0) op = RELOP_NE;
            else if (strcasecmp(word, "lt") == 0) op = RELOP_LT;
            else if (strcasecmp(word, "le") == 0) op = RELOP_LE;
            else if (strcasecmp(word, "gt") == 0) op = RELOP_GT;
            else if (strcasecmp(word, "ge") == 0) op = RELOP_GE;
            if (op != RELOP_NONE) { *s = q; return op; }
        }
    }
    return RELOP_NONE;
}

// relational := bitwise [ relop bitwise ]  -- lowest precedence, and
// non-chaining (Z80 assemblers don't chain comparisons). True is -1 (all
// bits set, the traditional assembler convention seen elsewhere in this
// codebase's target dialect, e.g. shift-vector "-1" fields), false is 0.
static long parse_relational(const char **s, ExprEnv *env) {
    long v = expr_or(s, env);
    RelOp op = match_relop(s);
    if (op == RELOP_NONE) return v;
    long rhs = expr_or(s, env);

    int result;
    switch (op) {
        case RELOP_EQ: result = (v == rhs); break;
        case RELOP_NE: result = (v != rhs); break;
        case RELOP_LT: result = (v < rhs); break;
        case RELOP_LE: result = (v <= rhs); break;
        case RELOP_GT: result = (v > rhs); break;
        case RELOP_GE: result = (v >= rhs); break;
        default: result = 0; break;
    }
    return result ? -1 : 0;
}

long expr_parse(const char **s, ExprEnv *env) {
    return parse_relational(s, env);
}
