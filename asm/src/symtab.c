#include <stdlib.h>
#include <string.h>
#include "symtab.h"

void symtab_init(SymTab *t) {
    t->head = NULL;
}

void symtab_free(SymTab *t) {
    Symbol *s = t->head;
    while (s) {
        Symbol *next = s->next;
        free(s);
        s = next;
    }
    t->head = NULL;
}

Symbol *symtab_find(SymTab *t, const char *name) {
    for (Symbol *s = t->head; s; s = s->next) {
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

int symtab_define(SymTab *t, const char *name, long value) {
    Symbol *s = symtab_find(t, name);
    if (s) {
        if (s->defined && s->value != value) return 0; // conflicting redefinition
        s->value = value;
        s->defined = 1;
        return 1;
    }

    s = malloc(sizeof(Symbol));
    strncpy(s->name, name, SYM_NAME_MAX - 1);
    s->name[SYM_NAME_MAX - 1] = '\0';
    s->value = value;
    s->defined = 1;
    s->next = t->head;
    t->head = s;
    return 1;
}
