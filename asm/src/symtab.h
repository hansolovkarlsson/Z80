#ifndef _SYMTAB_H
#define _SYMTAB_H

#define SYM_NAME_MAX 64

typedef struct Symbol {
    char name[SYM_NAME_MAX];
    long value;
    int defined;
    struct Symbol *next;
} Symbol;

typedef struct {
    Symbol *head;
} SymTab;

void symtab_init(SymTab *t);
void symtab_free(SymTab *t);
Symbol *symtab_find(SymTab *t, const char *name);
// Defines (or redefines) a symbol's value. Returns 1 on success, 0 if the
// symbol was already defined to a different value (duplicate-label error).
int symtab_define(SymTab *t, const char *name, long value);

#endif
