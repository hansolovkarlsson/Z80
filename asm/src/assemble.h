#ifndef _ASSEMBLE_H
#define _ASSEMBLE_H

#include <stdint.h>
#include "symtab.h"

#define IMAGE_SIZE 65536
#define MAX_COND_DEPTH 32

typedef struct {
    SymTab *symtab;
    uint8_t *image;    // IMAGE_SIZE bytes, owned by the caller
    long pc;             // current address (location counter)
    int pass;             // 1 or 2
    long min_addr;         // lowest address written so far
    long max_addr;          // highest address written so far, +1
    int had_output;
    const char *filename;

    // IF/ELSE/ENDIF nesting. cond_active[d] is whether lines at that
    // depth are actually processed (already ANDed with all enclosing
    // levels); cond_taken[d] is whether the IF (not ELSE) branch was
    // true, to decide what ELSE flips to; cond_parent_active[d] is
    // whether the enclosing scope was active when this level was pushed.
    int cond_depth;
    int cond_active[MAX_COND_DEPTH];
    int cond_taken[MAX_COND_DEPTH];
    int cond_parent_active[MAX_COND_DEPTH];
} AsmCtx;

typedef enum { LINE_NORMAL, LINE_ORG } LineKind;

typedef struct {
    LineKind kind;
    long org_addr; // valid when kind == LINE_ORG
    const char *err;
} LineResult;

// Assembles one source line: label definitions, directives (ORG/EQU/DB/
// DW/DS/END), and instructions (via encode_instruction). Bytes are written
// directly into ctx->image at ctx->pc, which this function also advances.
// Returns 0 on success, -1 on error (result->err set to a message).
int assemble_line(AsmCtx *ctx, const char *line, LineResult *result);

#endif
