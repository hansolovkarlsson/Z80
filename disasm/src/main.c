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

    // Pass 1: linear decode, collecting jump/call targets and (nn)
    // memory references that fall inside the loaded range as labels.
    for (uint32_t addr = origin; addr < end_addr;) {
        DecodedInsn d = decode_instruction(mem, (uint16_t)addr);
        if (d.has_ref && d.ref_addr >= origin && d.ref_addr < end_addr) {
            add_label(d.ref_addr, d.ref_is_code);
        }
        addr += (uint32_t)(d.length > 0 ? d.length : 1);
    }
    qsort(labels, (size_t)nlabels, sizeof(Label), label_cmp);

    // Pass 2: linear decode again, printing a label line whenever the
    // current address was referenced in pass 1.
    printf("        org %04Xh\n\n", origin);
    for (uint32_t addr = origin; addr < end_addr;) {
        int lidx = find_label((uint16_t)addr);
        if (lidx >= 0) {
            char name[16];
            label_name(name, sizeof(name), labels[lidx].addr, labels[lidx].is_code);
            printf("%s:\n", name);
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
