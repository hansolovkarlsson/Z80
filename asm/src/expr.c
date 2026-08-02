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
// (0FFh - must start with a digit), or hex with a 0x/0X prefix (0xFF).
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

    while (isxdigit((unsigned char)*p)) p++;
    if (*p == 'h' || *p == 'H') {
        long val = strtol(start, NULL, 16);
        *s = p + 1;
        return val;
    }

    // Not a hex-with-suffix literal (e.g. "ff" with no trailing 'h' isn't
    // valid decimal) - fall back to plain decimal digits only.
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

        if (strcasecmp(name, "low") == 0 || strcasecmp(name, "high") == 0) {
            int want_high = (strcasecmp(name, "high") == 0);
            const char *after = p;
            skip_ws(&after);
            if (*after == '(') {
                after++;
                long v = expr_or(&after, env);
                skip_ws(&after);
                if (*after == ')') after++;
                else if (!env->err) env->err = "expected ')'";
                *s = after;
                return want_high ? ((v >> 8) & 0xFF) : (v & 0xFF);
            }
        }

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

// unary := ('-' | '+' | '~')* primary
static long parse_unary(const char **s, ExprEnv *env) {
    skip_ws(s);
    if (**s == '-') { (*s)++; return -parse_unary(s, env); }
    if (**s == '+') { (*s)++; return parse_unary(s, env); }
    if (**s == '~') { (*s)++; return ~parse_unary(s, env); }
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

// bitwise := addsub (('&' | '|' | '^') addsub)*
static long expr_or(const char **s, ExprEnv *env) {
    long v = parse_addsub(s, env);
    for (;;) {
        skip_ws(s);
        char op = **s;
        if (op != '&' && op != '|' && op != '^') break;
        (*s)++;
        long rhs = parse_addsub(s, env);
        if (op == '&') v &= rhs;
        else if (op == '|') v |= rhs;
        else v ^= rhs;
    }
    return v;
}

long expr_parse(const char **s, ExprEnv *env) {
    return expr_or(s, env);
}
