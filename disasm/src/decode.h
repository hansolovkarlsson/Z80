#ifndef _DECODE_H
#define _DECODE_H

#include <stdint.h>

typedef struct {
    char text[64];       // formatted "MNEM operands", e.g. "LD A,(1234h)"
    int length;           // instruction length in bytes, always >= 1
    int has_ref;           // 1 if this instruction references an absolute address
    uint16_t ref_addr;      // the referenced address, valid when has_ref
    int ref_is_code;         // 1 if ref_addr is a jump/call target, 0 if a data (nn) reference
} DecodedInsn;

// Decodes one instruction from `mem` (a full 64KB image) at `addr`. Never
// fails - any byte pattern not recognized as a real instruction decodes
// as a single-byte "DB nnh" so the disassembler always makes forward
// progress and always produces valid, reassemblable output.
DecodedInsn decode_instruction(const uint8_t *mem, uint16_t addr);

#endif
