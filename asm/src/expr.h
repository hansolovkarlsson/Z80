#ifndef _EXPR_H
#define _EXPR_H

#include "symtab.h"

typedef struct {
    SymTab *symtab;
    long pc;          // current address, referenced via '$'
    int pass;          // 1 or 2
    int unresolved;    // set if an undefined symbol was referenced (only tolerated on pass 1)
    const char *err;   // set to a static message on syntax error, else NULL
} ExprEnv;

// Parses an expression starting at *s, advancing *s past it. On a syntax
// error, env->err is set and 0 is returned. On an undefined symbol during
// pass 2, env->err is set. During pass 1, undefined symbols evaluate to 0
// and env->unresolved is set instead of erroring, since the symbol may be
// defined later in the source.
long expr_parse(const char **s, ExprEnv *env);

#endif
