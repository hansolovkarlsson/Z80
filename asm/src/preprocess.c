#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include "preprocess.h"

#define MAX_MACROS 64
#define MAX_MACRO_PARAMS 16
#define MAX_MACRO_LINES 200
#define MAX_MACRO_LOCALS 16
#define MAX_INCLUDE_DEPTH 20

typedef struct {
    char name[64];
    char params[MAX_MACRO_PARAMS][40];
    int nparams;
    char *body[MAX_MACRO_LINES];
    int nbody;
} Macro;

typedef struct {
    Macro macros[MAX_MACROS];
    int nmacros;
} MacroTable;

typedef struct {
    PPLine *items;
    int count;
    int cap;
} PPBuilder;

static void pp_trim(char *s) {
    char *start = s;
    while (*start == ' ' || *start == '\t' || *start == '\r' || *start == '\n') start++;
    if (start != s) memmove(s, start, strlen(start) + 1);
    size_t len = strlen(s);
    while (len > 0 && (s[len - 1] == ' ' || s[len - 1] == '\t' ||
                       s[len - 1] == '\r' || s[len - 1] == '\n')) {
        s[--len] = '\0';
    }
}

static void pp_strip_comment(char *line) {
    int in_squote = 0, in_dquote = 0;
    for (char *p = line; *p; p++) {
        if (*p == '\'' && !in_dquote) in_squote = !in_squote;
        else if (*p == '"' && !in_squote) in_dquote = !in_dquote;
        else if (*p == ';' && !in_squote && !in_dquote) { *p = '\0'; return; }
    }
}

static void pp_push(PPBuilder *b, const char *text, const char *origin) {
    if (b->count >= b->cap) {
        b->cap = b->cap ? b->cap * 2 : 256;
        b->items = realloc(b->items, sizeof(PPLine) * (size_t)b->cap);
    }
    b->items[b->count].text = strdup(text);
    b->items[b->count].origin = strdup(origin);
    b->count++;
}

// Extracts the first whitespace-delimited word from `line` and returns
// the (trimmed) remainder in `rest`. Note: this doesn't handle a leading
// "label:" - by convention, MACRO/ENDM/LOCAL/INCLUDE and macro invocations
// must be the first thing on their line (no label prefix).
static void split_first_word(const char *line, char *word, size_t wordsz, char *rest, size_t restsz) {
    const char *p = line;
    while (*p == ' ' || *p == '\t') p++;
    const char *start = p;
    while (*p && *p != ' ' && *p != '\t') p++;
    size_t len = (size_t)(p - start);
    if (len >= wordsz) len = wordsz - 1;
    memcpy(word, start, len);
    word[len] = '\0';

    while (*p == ' ' || *p == '\t') p++;
    strncpy(rest, p, restsz - 1);
    rest[restsz - 1] = '\0';
    pp_trim(rest);
}

static Macro *find_macro(MacroTable *mt, const char *name) {
    for (int i = 0; i < mt->nmacros; i++) {
        if (strcasecmp(mt->macros[i].name, name) == 0) return &mt->macros[i];
    }
    return NULL;
}

static int is_ident_char(char c) {
    return isalnum((unsigned char)c) || c == '_' || c == '.';
}

// Replaces occurrences of "&param" (ampersand-prefixed) or a bare
// whole-identifier "param" in `line` with `value`, writing the result to
// `out`. zexall.z80's own tstr macro mixes both forms for the same
// parameter set ("&memop" but bare "insn") in one body, so both need to
// match - at the cost of a collision risk for future macros whose
// parameter names coincide with unrelated identifiers elsewhere in the
// body (see docs/ROADMAP.md).
static void substitute_amp_param(const char *line, const char *param, const char *value, char *out, size_t outsz) {
    size_t param_len = strlen(param);
    size_t oi = 0;
    for (const char *p = line; *p && oi + 1 < outsz;) {
        int has_amp = (*p == '&');
        const char *word = has_amp ? p + 1 : p;
        int boundary_before = has_amp || (p == line) || !is_ident_char(p[-1]);
        if (boundary_before && strncasecmp(word, param, param_len) == 0 && !is_ident_char(word[param_len])) {
            size_t vlen = strlen(value);
            for (size_t i = 0; i < vlen && oi + 1 < outsz; i++) out[oi++] = value[i];
            p = word + param_len;
        } else {
            out[oi++] = *p++;
        }
    }
    out[oi] = '\0';
}

static char *strip_quotes(char *path) {
    pp_trim(path);
    size_t len = strlen(path);
    if (len >= 2 && (path[0] == '"' || path[0] == '\'') && path[len - 1] == path[0]) {
        memmove(path, path + 1, len - 2);
        path[len - 2] = '\0';
    }
    return path;
}

static void path_dirname(const char *path, char *out, size_t outsz) {
    const char *slash = strrchr(path, '/');
    if (!slash) {
        strncpy(out, ".", outsz - 1);
        out[outsz - 1] = '\0';
        return;
    }
    size_t len = (size_t)(slash - path);
    if (len >= outsz) len = outsz - 1;
    memcpy(out, path, len);
    out[len] = '\0';
}

// INCLUDE paths are resolved relative to the including file's own
// directory (the standard convention), not the process's working
// directory - so an include works the same regardless of where z80asm is
// invoked from. Absolute paths (leading '/') pass through unchanged.
static void path_resolve(const char *base_dir, const char *rel_path, char *out, size_t outsz) {
    if (rel_path[0] == '/') {
        strncpy(out, rel_path, outsz - 1);
        out[outsz - 1] = '\0';
    } else {
        snprintf(out, outsz, "%s/%s", base_dir, rel_path);
    }
}

static int process_file(const char *filename, PPBuilder *out, MacroTable *mt, int *local_counter, int depth);

static int expand_macro(Macro *m, const char *args_text, PPBuilder *out, MacroTable *mt,
                         int *local_counter, int depth, const char *call_origin, const char *base_dir) {
    if (depth > MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "%s: error: macro expansion nested too deep\n", call_origin);
        return -1;
    }

    char args[MAX_MACRO_PARAMS][200];
    int nargs = 0;
    {
        const char *p = args_text;
        int paren_depth = 0;
        int in_squote = 0, in_dquote = 0;
        const char *start = p;
        for (;; p++) {
            char c = *p;
            if (c == '\'' && !in_dquote) in_squote = !in_squote;
            else if (c == '"' && !in_squote) in_dquote = !in_dquote;
            else if (!in_squote && !in_dquote) {
                if (c == '(') paren_depth++;
                else if (c == ')') paren_depth--;
                else if (c == '<') paren_depth++; // <a,b,c> groups a comma-list into one argument
                else if (c == '>') paren_depth--;
            }
            if ((c == ',' && paren_depth == 0 && !in_squote && !in_dquote) || c == '\0') {
                if (nargs < MAX_MACRO_PARAMS) {
                    size_t len = (size_t)(p - start);
                    if (len >= sizeof(args[0])) len = sizeof(args[0]) - 1;
                    memcpy(args[nargs], start, len);
                    args[nargs][len] = '\0';
                    pp_trim(args[nargs]);
                    // Strip one enclosing <...> pair - it's a grouping
                    // marker for the call, not part of the value itself
                    // (e.g. tstr <0edh,042h>,... substitutes &insn as the
                    // bare text "0edh,042h" into "db &insn").
                    size_t alen = strlen(args[nargs]);
                    if (alen >= 2 && args[nargs][0] == '<' && args[nargs][alen - 1] == '>') {
                        memmove(args[nargs], args[nargs] + 1, alen - 2);
                        args[nargs][alen - 2] = '\0';
                    }
                    nargs++;
                }
                start = p + 1;
                if (c == '\0') break;
            }
        }
    }

    int this_expansion = (*local_counter)++;

    char local_names[MAX_MACRO_LOCALS][40];
    char local_renamed[MAX_MACRO_LOCALS][64];
    int nlocals = 0;

    for (int i = 0; i < m->nbody; i++) {
        char word[64], rest[512];
        split_first_word(m->body[i], word, sizeof(word), rest, sizeof(rest));
        if (strcasecmp(word, "LOCAL") != 0) continue;

        const char *p = rest;
        while (*p) {
            while (*p == ' ' || *p == '\t' || *p == ',') p++;
            if (!*p) break;
            const char *start = p;
            while (*p && *p != ',') p++;
            size_t len = (size_t)(p - start);
            while (len > 0 && (start[len - 1] == ' ' || start[len - 1] == '\t')) len--;
            if (len > 0 && nlocals < MAX_MACRO_LOCALS) {
                size_t cl = len < sizeof(local_names[0]) - 1 ? len : sizeof(local_names[0]) - 1;
                memcpy(local_names[nlocals], start, cl);
                local_names[nlocals][cl] = '\0';
                snprintf(local_renamed[nlocals], sizeof(local_renamed[0]), "__%s_L%d", local_names[nlocals], this_expansion);
                nlocals++;
            }
        }
    }

    for (int i = 0; i < m->nbody; i++) {
        char word[64], rest_unused[512];
        split_first_word(m->body[i], word, sizeof(word), rest_unused, sizeof(rest_unused));
        if (strcasecmp(word, "LOCAL") == 0) continue; // consumed above

        char buf1[600], buf2[600];
        strncpy(buf1, m->body[i], sizeof(buf1) - 1);
        buf1[sizeof(buf1) - 1] = '\0';

        for (int pi = 0; pi < m->nparams; pi++) {
            const char *val = (pi < nargs) ? args[pi] : "";
            substitute_amp_param(buf1, m->params[pi], val, buf2, sizeof(buf2));
            strncpy(buf1, buf2, sizeof(buf1) - 1);
            buf1[sizeof(buf1) - 1] = '\0';
        }
        for (int li = 0; li < nlocals; li++) {
            // LOCAL names are referenced the same way as parameters, with
            // an & prefix (matches zexall.z80's own "local lab" / "&lab:"
            // usage - & isn't parameter-specific, it marks any
            // macro-scoped substitution).
            substitute_amp_param(buf1, local_names[li], local_renamed[li], buf2, sizeof(buf2));
            strncpy(buf1, buf2, sizeof(buf1) - 1);
            buf1[sizeof(buf1) - 1] = '\0';
        }

        char origin[400];
        snprintf(origin, sizeof(origin), "%s (macro %s)", call_origin, m->name);

        char word2[64], rest2[512];
        split_first_word(buf1, word2, sizeof(word2), rest2, sizeof(rest2));

        if (strcasecmp(word2, "INCLUDE") == 0) {
            char path[400], resolved[400];
            strncpy(path, rest2, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            path_resolve(base_dir, strip_quotes(path), resolved, sizeof(resolved));
            if (process_file(resolved, out, mt, local_counter, depth + 1) != 0) return -1;
            continue;
        }

        Macro *nested = find_macro(mt, word2);
        if (nested) {
            if (expand_macro(nested, rest2, out, mt, local_counter, depth + 1, origin, base_dir) != 0) return -1;
            continue;
        }

        pp_push(out, buf1, origin);
    }

    return 0;
}

static int process_file(const char *filename, PPBuilder *out, MacroTable *mt, int *local_counter, int depth) {
    if (depth > MAX_INCLUDE_DEPTH) {
        fprintf(stderr, "z80asm: '%s': include nesting too deep\n", filename);
        return -1;
    }

    FILE *f = fopen(filename, "r");
    if (!f) {
        fprintf(stderr, "z80asm: cannot open '%s': %s\n", filename, strerror(errno));
        return -1;
    }

    char my_dir[400];
    path_dirname(filename, my_dir, sizeof(my_dir));

    char raw[512];
    int line_no = 0;
    int errors = 0;

    int in_macro_def = 0;
    Macro *cur_macro = NULL;

    while (fgets(raw, sizeof(raw), f)) {
        line_no++;
        char line[512];
        strncpy(line, raw, sizeof(line) - 1);
        line[sizeof(line) - 1] = '\0';
        pp_strip_comment(line);
        pp_trim(line);

        char origin[300];
        snprintf(origin, sizeof(origin), "%s:%d", filename, line_no);

        if (in_macro_def) {
            char word[64], rest[512];
            split_first_word(line, word, sizeof(word), rest, sizeof(rest));
            if (strcasecmp(word, "ENDM") == 0) {
                in_macro_def = 0;
                cur_macro = NULL;
                continue;
            }
            if (line[0] == '\0') continue;
            if (cur_macro->nbody >= MAX_MACRO_LINES) {
                fprintf(stderr, "%s: error: macro '%s' body too long\n", origin, cur_macro->name);
                errors++;
                continue;
            }
            cur_macro->body[cur_macro->nbody++] = strdup(line);
            continue;
        }

        if (line[0] == '\0') continue;

        char word[64], rest[512];
        split_first_word(line, word, sizeof(word), rest, sizeof(rest));

        // "name MACRO param1,param2,..." - the common convention (matches
        // M80/ZSM4-style assemblers, including zexall.z80's own macros):
        // the macro's name comes first, then the MACRO keyword. The name
        // may or may not have a trailing ':' like a label (zexall.z80
        // uses "tstr:	macro	...").
        char word2[64], params_text[400];
        split_first_word(rest, word2, sizeof(word2), params_text, sizeof(params_text));
        if (word[0] != '\0' && strcasecmp(word2, "MACRO") == 0) {
            if (mt->nmacros >= MAX_MACROS) {
                fprintf(stderr, "%s: error: too many macro definitions\n", origin);
                errors++;
                continue;
            }

            size_t wlen = strlen(word);
            if (wlen > 0 && word[wlen - 1] == ':') word[wlen - 1] = '\0';

            Macro *m = &mt->macros[mt->nmacros++];
            memset(m, 0, sizeof(*m));
            strncpy(m->name, word, sizeof(m->name) - 1);

            const char *pp = params_text;
            while (*pp) {
                while (*pp == ' ' || *pp == '\t' || *pp == ',') pp++;
                if (!*pp) break;
                const char *pstart = pp;
                while (*pp && *pp != ',') pp++;
                size_t plen = (size_t)(pp - pstart);
                while (plen > 0 && (pstart[plen - 1] == ' ' || pstart[plen - 1] == '\t')) plen--;
                if (plen > 0 && m->nparams < MAX_MACRO_PARAMS) {
                    size_t copy_len = plen < sizeof(m->params[0]) - 1 ? plen : sizeof(m->params[0]) - 1;
                    memcpy(m->params[m->nparams], pstart, copy_len);
                    m->params[m->nparams][copy_len] = '\0';
                    m->nparams++;
                }
            }

            in_macro_def = 1;
            cur_macro = m;
            continue;
        }

        if (strcasecmp(word, "INCLUDE") == 0) {
            char path[400], resolved[400];
            strncpy(path, rest, sizeof(path) - 1);
            path[sizeof(path) - 1] = '\0';
            path_resolve(my_dir, strip_quotes(path), resolved, sizeof(resolved));
            if (process_file(resolved, out, mt, local_counter, depth + 1) != 0) errors++;
            continue;
        }

        Macro *m = find_macro(mt, word);
        if (m) {
            if (expand_macro(m, rest, out, mt, local_counter, depth, origin, my_dir) != 0) errors++;
            continue;
        }

        pp_push(out, line, origin);
    }

    if (in_macro_def) {
        fprintf(stderr, "%s: error: MACRO '%s' missing ENDM\n", filename, cur_macro->name);
        errors++;
    }

    fclose(f);
    return errors ? -1 : 0;
}

PPResult preprocess(const char *filename) {
    PPBuilder b = {0};
    MacroTable mt = {0};
    int local_counter = 0;

    int rc = process_file(filename, &b, &mt, &local_counter, 0);

    PPResult r;
    r.lines = b.items;
    r.count = (rc == 0) ? b.count : -1;
    return r;
}

void pp_free(PPResult *r) {
    if (r->count < 0) return;
    for (int i = 0; i < r->count; i++) {
        free(r->lines[i].text);
        free(r->lines[i].origin);
    }
    free(r->lines);
}
