#ifndef _ENCODE_H
#define _ENCODE_H

#include <stdint.h>
#include "symtab.h"

typedef struct {
    uint8_t bytes[8];
    int nbytes;
    const char *err;
} EncOut;

typedef struct {
    SymTab *symtab;
    long pc;      // address of the start of this instruction
    int pass;     // 1 or 2
    int unresolved;
} EncCtx;

// Encodes one instruction: `mnemonic` (already uppercased/trimmed) and the
// raw, unparsed operand text following it (may be empty). Writes the
// encoded bytes and length into *out. On an unrecognized mnemonic/operand
// combination, out->err is set and out->nbytes is left at whatever was
// emitted before the error (normally 0).
void encode_instruction(EncCtx *ctx, const char *mnemonic, const char *operand_text, EncOut *out);

#endif
