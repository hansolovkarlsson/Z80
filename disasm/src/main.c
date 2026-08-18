#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "decode.h"

#define IMAGE_SIZE 65536
#define MAX_LABELS 4096

typedef struct {
    uint16_t addr;
    int is_code;
} Label;

static Label labels[MAX_LABELS];
static int nlabels = 0;

static int find_label(uint16_t addr) {
    for (int i = 0; i < nlabels; i++) {
        if (labels[i].addr == addr) return i;
    }
    return -1;
}

// Records `addr` as a label target if it isn't already one, or upgrades
// an existing data label to a code label (code labels win - an address
// that's both jumped to and referenced as data is still "the" code
// entry point for naming purposes).
static void add_label(uint16_t addr, int is_code) {
    int idx = find_label(addr);
    if (idx >= 0) {
        if (is_code) labels[idx].is_code = 1;
        return;
    }
    if (nlabels >= MAX_LABELS) return;
    labels[nlabels].addr = addr;
    labels[nlabels].is_code = is_code;
    nlabels++;
}

static int label_cmp(const void *a, const void *b) {
    return ((const Label *)a)->addr - ((const Label *)b)->addr;
}

// Parses a CLI numeric argument accepting the same conventions as the
// rest of this toolchain (0xNN, NNh) in addition to plain C literal forms
// (0x200 hex, 0777 octal, 512 decimal) that strtol(...,0) already
// understands on its own.
static long parse_number_arg(const char *s) {
    size_t len = strlen(s);
    if (len > 0 && (s[len - 1] == 'h' || s[len - 1] == 'H')) {
        return strtol(s, NULL, 16);
    }
    return strtol(s, NULL, 0);
}

static void label_name(char *buf, size_t n, uint16_t addr, int is_code) {
    snprintf(buf, n, "%s%04X", is_code ? "L" : "D", addr);
}

// Same "DB nnh" hex-literal convention decode.c's own hex8()/hex16()
// already use (leading 0 if the first nibble would otherwise start with
// a hex letter) - applied here for a whole byte at a time, for addresses
// pass 1 below never reaches as code.
static void format_data_byte(char *buf, size_t n, uint8_t v) {
    if (((v >> 4) & 0xF) >= 0xA) snprintf(buf, n, "DB 0%02Xh", v);
    else snprintf(buf, n, "DB %02Xh", v);
}

#define STATUS_UNVISITED  0
#define STATUS_CODE_START 1
#define STATUS_CODE_CONT  2

static uint8_t status[IMAGE_SIZE];

// Peeks the raw opcode byte(s) at addr to decide whether this instruction
// unconditionally transfers control elsewhere, so the worklist-driven
// traversal below knows not to also treat addr+length as reachable.
// Mirrors an existing pattern in this codebase - abc80/emu/src/step.c
// peeks raw opcode bytes ahead of execution to predict control flow for
// the identical reason - rather than adding a speculative new field to
// DecodedInsn for this one, narrow purpose. Conditional jumps/DJNZ/CALL/
// RST are deliberately not terminators here: fall-through after them is
// genuinely reachable, matching cpm/docs/ROADMAP.md's own "stopping at
// unconditional jumps/RET" phrasing exactly.
static int is_unconditional_terminator(const uint8_t *mem, uint16_t addr) {
    uint8_t op = mem[addr];
    if (op == 0xC3) return 1; // JP nn
    if (op == 0x18) return 1; // JR e
    if (op == 0xC9) return 1; // RET
    if (op == 0xE9) return 1; // JP (HL)
    if (op == 0xDD || op == 0xFD) return mem[(uint16_t)(addr + 1)] == 0xE9; // JP (IX)/JP (IY)
    if (op == 0xED) {
        uint8_t op2 = mem[(uint16_t)(addr + 1)];
        return op2 == 0x4D || op2 == 0x45; // RETI / RETN
    }
    return 0;
}

#define WORKLIST_SIZE (2 * IMAGE_SIZE)
static uint16_t worklist[WORKLIST_SIZE];
static int worklist_top = 0;

static void push_worklist(uint16_t addr, uint16_t origin, uint16_t end_addr) {
    if (addr < origin || addr >= end_addr) return;
    if (status[addr] != STATUS_UNVISITED) return;
    if (worklist_top >= WORKLIST_SIZE) return; // can't happen: bounded by 2x decoded instructions
    worklist[worklist_top++] = addr;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <input.com> [-o origin] [-l length]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_path = NULL;
    uint16_t origin = 0x0100; // CP/M convention, matches the assembler's default
    long limit = -1;          // -1 = disassemble to end of file

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            origin = (uint16_t)parse_number_arg(argv[++i]);
        } else if (strcmp(argv[i], "-l") == 0 && i + 1 < argc) {
            limit = parse_number_arg(argv[++i]);
        } else if (!input_path) {
            input_path = argv[i];
        }
    }
    if (!input_path) {
        fprintf(stderr, "usage: %s <input.com> [-o origin] [-l length]\n", argv[0]);
        return EXIT_FAILURE;
    }

    FILE *f = fopen(input_path, "rb");
    if (!f) {
        fprintf(stderr, "z80dasm: cannot open '%s': %s\n", input_path, strerror(errno));
        return EXIT_FAILURE;
    }

    static uint8_t mem[IMAGE_SIZE];
    long max_len = IMAGE_SIZE - origin;
    long file_len = (long)fread(mem + origin, 1, (size_t)max_len, f);
    fclose(f);

    long end_len = (limit >= 0 && limit < file_len) ? limit : file_len;
    uint16_t end_addr = (uint16_t)(origin + end_len);

    // Pass 1: worklist-driven reachability decode, starting from `origin`
    // (the CP/M .com entry point / ROM reset vector - this tool's two
    // real use cases both have exactly one natural entry point there).
    // Follows JP/CALL/JR/RST targets (ref_is_code) as real code; a (nn)
    // data reference still gets a label via add_label() but is never
    // pushed as an entry point - this is the actual code/data separation.
    // Stops following the current thread at an unconditional terminator
    // (is_unconditional_terminator() above) but keeps draining the rest
    // of the worklist, so other reachable branches still get decoded.
    push_worklist(origin, origin, end_addr);
    while (worklist_top > 0) {
        uint16_t addr = worklist[--worklist_top];
        if (status[addr] != STATUS_UNVISITED) continue; // already decoded via another path

        DecodedInsn d = decode_instruction(mem, addr);
        int len = d.length > 0 ? d.length : 1;

        status[addr] = STATUS_CODE_START;
        for (int i = 1; i < len; i++) {
            uint16_t byte_addr = (uint16_t)(addr + i);
            if (byte_addr >= origin && byte_addr < end_addr) status[byte_addr] = STATUS_CODE_CONT;
        }

        if (d.has_ref && d.ref_addr >= origin && d.ref_addr < end_addr) {
            add_label(d.ref_addr, d.ref_is_code);
            if (d.ref_is_code) push_worklist(d.ref_addr, origin, end_addr);
        }

        if (!is_unconditional_terminator(mem, addr)) {
            uint32_t fallthrough = (uint32_t)addr + (uint32_t)len;
            if (fallthrough < IMAGE_SIZE) push_worklist((uint16_t)fallthrough, origin, end_addr);
        }
    }
    qsort(labels, (size_t)nlabels, sizeof(Label), label_cmp);

    // Pass 2: linear walk over the same [origin, end_addr) range, same as
    // before - the change is entirely in *what* gets printed at each
    // address. A reachable instruction (status == CODE_START) decodes and
    // prints exactly as before; anything else (never reached as code -
    // true data, e.g. embedded strings) prints as a single labeled DB
    // byte instead of being mis-decoded as an instruction.
    printf("        org %04Xh\n\n", origin);
    for (uint32_t addr = origin; addr < end_addr;) {
        int lidx = find_label((uint16_t)addr);
        if (lidx >= 0) {
            char name[16];
            label_name(name, sizeof(name), labels[lidx].addr, labels[lidx].is_code);
            printf("%s:\n", name);
        }

        if (status[addr] != STATUS_CODE_START) {
            char operand_line[32];
            format_data_byte(operand_line, sizeof(operand_line), mem[addr]);
            printf("        %-24s; %04X: %02X \n", operand_line, addr, mem[addr]);
            addr += 1;
            continue;
        }

        DecodedInsn d = decode_instruction(mem, (uint16_t)addr);
        if (d.length <= 0) d.length = 1;

        char operand_line[96];
        strncpy(operand_line, d.text, sizeof(operand_line) - 1);
        operand_line[sizeof(operand_line) - 1] = '\0';

        // If this instruction's text contains the raw hex form of a
        // referenced address that got a label, swap in the label name so
        // the output stays meaningful (and still reassembles).
        if (d.has_ref && d.ref_addr >= origin && d.ref_addr < end_addr) {
            int ridx = find_label(d.ref_addr);
            if (ridx >= 0) {
                char name[16], hexbuf[8];
                label_name(name, sizeof(name), d.ref_addr, labels[ridx].is_code);
                if (((d.ref_addr >> 12) & 0xF) >= 0xA) snprintf(hexbuf, sizeof(hexbuf), "0%04Xh", d.ref_addr);
                else snprintf(hexbuf, sizeof(hexbuf), "%04Xh", d.ref_addr);
                char *pos = strstr(operand_line, hexbuf);
                if (pos) {
                    char rest[96];
                    strncpy(rest, pos + strlen(hexbuf), sizeof(rest) - 1);
                    rest[sizeof(rest) - 1] = '\0';
                    snprintf(pos, sizeof(operand_line) - (size_t)(pos - operand_line), "%s%s", name, rest);
                }
            }
        }

        printf("        %-24s; %04X: ", operand_line, addr);
        for (int i = 0; i < d.length; i++) printf("%02X ", mem[addr + (uint32_t)i]);
        printf("\n");

        addr += (uint32_t)d.length;
    }

    return EXIT_SUCCESS;
}
