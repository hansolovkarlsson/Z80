#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "assemble.h"
#include "symtab.h"

static char *default_output_name(const char *input) {
    const char *slash = strrchr(input, '/');
    const char *base = slash ? slash + 1 : input;
    const char *dot = strrchr(base, '.');

    size_t base_len = dot ? (size_t)(dot - base) : strlen(base);
    char *out = malloc(base_len + 5); // ".com" + NUL
    memcpy(out, base, base_len);
    memcpy(out + base_len, ".com", 5);
    return out;
}

// Runs one full pass over the source file. Returns the number of errors
// encountered (each printed to stderr as it's found).
static int run_pass(const char *input_path, AsmCtx *ctx, int pass) {
    FILE *f = fopen(input_path, "r");
    if (!f) {
        fprintf(stderr, "z80asm: cannot open '%s': %s\n", input_path, strerror(errno));
        return 1;
    }

    ctx->pass = pass;
    ctx->pc = 0;
    ctx->line_no = 0;
    int errors = 0;

    char line[512];
    while (fgets(line, sizeof(line), f)) {
        ctx->line_no++;
        LineResult r;
        if (assemble_line(ctx, line, &r) != 0) {
            fprintf(stderr, "%s:%d: error: %s\n", ctx->filename, ctx->line_no, r.err);
            errors++;
            continue;
        }
        if (r.kind == LINE_ORG) ctx->pc = r.org_addr;
    }

    fclose(f);
    return errors;
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "usage: %s <input.asm> [-o output.com]\n", argv[0]);
        return EXIT_FAILURE;
    }

    const char *input_path = argv[1];
    char *output_path = NULL;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-o") == 0 && i + 1 < argc) {
            output_path = argv[++i];
        }
    }
    char *default_output = NULL;
    if (!output_path) {
        default_output = default_output_name(input_path);
        output_path = default_output;
    }

    SymTab symtab;
    symtab_init(&symtab);

    uint8_t *image = calloc(1, IMAGE_SIZE);

    AsmCtx ctx;
    ctx.symtab = &symtab;
    ctx.image = image;
    ctx.min_addr = 0;
    ctx.max_addr = 0;
    ctx.had_output = 0;
    ctx.filename = input_path;

    int errors = run_pass(input_path, &ctx, 1);
    if (errors == 0) {
        ctx.had_output = 0;
        ctx.min_addr = 0;
        ctx.max_addr = 0;
        errors = run_pass(input_path, &ctx, 2);
    } else {
        fprintf(stderr, "z80asm: %d error(s) in pass 1, aborting\n", errors);
    }

    if (errors > 0) {
        if (errors > 0 && ctx.pass == 2) {
            fprintf(stderr, "z80asm: %d error(s) in pass 2\n", errors);
        }
        symtab_free(&symtab);
        free(image);
        free(default_output);
        return EXIT_FAILURE;
    }

    if (!ctx.had_output) {
        fprintf(stderr, "z80asm: warning: no bytes assembled, not writing '%s'\n", output_path);
        symtab_free(&symtab);
        free(image);
        free(default_output);
        return EXIT_SUCCESS;
    }

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "z80asm: cannot write '%s': %s\n", output_path, strerror(errno));
        symtab_free(&symtab);
        free(image);
        free(default_output);
        return EXIT_FAILURE;
    }
    long len = ctx.max_addr - ctx.min_addr;
    fwrite(image + ctx.min_addr, 1, (size_t)len, out);
    fclose(out);

    printf("z80asm: wrote %ld bytes to '%s' (origin 0x%04lX)\n", len, output_path, ctx.min_addr);

    symtab_free(&symtab);
    free(image);
    free(default_output);
    return EXIT_SUCCESS;
}
