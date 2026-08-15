#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "assemble.h"
#include "expr.h"
#include "preprocess.h"
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

// Peeks the first whitespace-delimited word of `line` (word only, no
// remainder - callers that need the rest use split_first_word below).
static void peek_word(const char *line, char *word, size_t wordsz) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    const char *start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t len = (size_t)(p - start);
    if (len >= wordsz) len = wordsz - 1;
    memcpy(word, start, len);
    word[len] = '\0';
}

// Like peek_word, but also returns a pointer (into `line`) at the start
// of whatever follows the word.
static void split_first_word(const char *line, char *word, size_t wordsz, const char **rest_out) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    const char *start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t len = (size_t)(p - start);
    if (len >= wordsz) len = wordsz - 1;
    memcpy(word, start, len);
    word[len] = '\0';
    while (*p == ' ' || *p == '\t') p++;
    *rest_out = p;
}

// REPT's own body may itself contain a nested REPT, so this tracks
// nesting depth to find the ENDM that actually closes the one at
// `start_idx` (not an inner REPT's). Returns -1 if unmatched.
static int find_matching_endm(PPResult *pp, int start_idx) {
    int depth = 1;
    for (int j = start_idx + 1; j < pp->count; j++) {
        char word[16];
        peek_word(pp->lines[j].text, word, sizeof(word));
        if (strcasecmp(word, "REPT") == 0) depth++;
        else if (strcasecmp(word, "ENDM") == 0) {
            depth--;
            if (depth == 0) return j;
        }
    }
    return -1;
}

// Evaluates a REPT count expression using the current pass's live $/symbol
// state - this is exactly why REPT is handled here rather than as a
// text-preprocessing step: a count like "&lab+4-$" depends on the
// location counter, which isn't known until real assembly is underway.
static long eval_rept_count(AsmCtx *ctx, const char *expr_text, const char *origin, int *errors) {
    ExprEnv env;
    env.symtab = ctx->symtab;
    env.pc = ctx->pc;
    env.pass = ctx->pass;
    env.unresolved = 0;
    env.err = NULL;

    const char *p = expr_text;
    long v = expr_parse(&p, &env);

    if (env.err) {
        fprintf(stderr, "%s: error: %s in REPT count\n", origin, env.err);
        (*errors)++;
        return 0;
    }
    if (ctx->pass == 2 && env.unresolved) {
        fprintf(stderr, "%s: error: undefined symbol in REPT count\n", origin);
        (*errors)++;
        return 0;
    }
    return v;
}

static int process_one_line(AsmCtx *ctx, PPResult *pp, int i) {
    LineResult r;
    if (assemble_line(ctx, pp->lines[i].text, &r) != 0) {
        fprintf(stderr, "%s: error: %s\n", pp->lines[i].origin, r.err);
        return 1;
    }
    if (r.kind == LINE_ORG) ctx->pc = r.org_addr;
    return 0;
}

// Runs one full pass over the already-preprocessed (macro-expanded,
// INCLUDE-spliced) line list. Returns the number of errors encountered
// (each printed to stderr as it's found). REPT/ENDM blocks are expanded
// here, structurally, by re-running the enclosed line range - this is a
// driver-level concern (which line runs next), not something
// assemble_line() can do on its own since it only ever sees one line at
// a time.
static int run_pass(PPResult *pp, AsmCtx *ctx, int pass) {
    ctx->pass = pass;
    ctx->pc = 0;
    ctx->cond_depth = 0;
    int errors = 0;

    int i = 0;
    while (i < pp->count) {
        char word[16];
        peek_word(pp->lines[i].text, word, sizeof(word));

        if (strcasecmp(word, "REPT") == 0) {
            int end_idx = find_matching_endm(pp, i);
            if (end_idx < 0) {
                fprintf(stderr, "%s: error: REPT without matching ENDM\n", pp->lines[i].origin);
                errors++;
                break;
            }

            char w2[16];
            const char *rest;
            split_first_word(pp->lines[i].text, w2, sizeof(w2), &rest);
            long count = eval_rept_count(ctx, rest, pp->lines[i].origin, &errors);
            if (count < 0) count = 0;

            for (long rep = 0; rep < count; rep++) {
                for (int j = i + 1; j < end_idx; j++) {
                    errors += process_one_line(ctx, pp, j);
                }
            }

            i = end_idx + 1;
            continue;
        }

        errors += process_one_line(ctx, pp, i);
        i++;
    }

    if (ctx->cond_depth > 0) {
        fprintf(stderr, "%s: error: %d unclosed IF block(s) at end of file\n", ctx->filename, ctx->cond_depth);
        errors++;
    }

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

    PPResult pp = preprocess(input_path);
    if (pp.count < 0) {
        fprintf(stderr, "z80asm: preprocessing failed\n");
        free(default_output);
        return EXIT_FAILURE;
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

    int errors = run_pass(&pp, &ctx, 1);
    if (errors == 0) {
        ctx.had_output = 0;
        ctx.min_addr = 0;
        ctx.max_addr = 0;
        errors = run_pass(&pp, &ctx, 2);
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
        pp_free(&pp);
        return EXIT_FAILURE;
    }

    if (!ctx.had_output) {
        fprintf(stderr, "z80asm: warning: no bytes assembled, not writing '%s'\n", output_path);
        symtab_free(&symtab);
        free(image);
        free(default_output);
        pp_free(&pp);
        return EXIT_SUCCESS;
    }

    FILE *out = fopen(output_path, "wb");
    if (!out) {
        fprintf(stderr, "z80asm: cannot write '%s': %s\n", output_path, strerror(errno));
        symtab_free(&symtab);
        free(image);
        free(default_output);
        pp_free(&pp);
        return EXIT_FAILURE;
    }
    long len = ctx.max_addr - ctx.min_addr;
    fwrite(image + ctx.min_addr, 1, (size_t)len, out);
    fclose(out);

    printf("z80asm: wrote %ld bytes to '%s' (origin 0x%04lX)\n", len, output_path, ctx.min_addr);

    symtab_free(&symtab);
    free(image);
    free(default_output);
    pp_free(&pp);
    return EXIT_SUCCESS;
}
