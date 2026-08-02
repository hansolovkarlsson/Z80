#ifndef _PREPROCESS_H
#define _PREPROCESS_H

// One expanded source line, tagged with where it actually came from (a
// file:line, or "file:line (macro NAME)" for a macro-expanded line) so
// error messages stay meaningful after INCLUDE splicing and macro
// expansion flatten everything into one line list.
typedef struct {
    char *text;
    char *origin;
} PPLine;

typedef struct {
    PPLine *lines;
    int count; // -1 if preprocessing failed (errors already printed to stderr)
} PPResult;

// Reads `filename`, recursively splicing in INCLUDE'd files and expanding
// MACRO/ENDM definitions and invocations (with &param substitution and
// LOCAL-label renaming), and returns the flattened line list ready to feed
// into assemble_line() for both assembler passes.
PPResult preprocess(const char *filename);

void pp_free(PPResult *r);

#endif
