#include "symbol.h"
#include "class.h"

#include <stdlib.h>
#include <string.h>

static char **table = NULL;
static oop *symbolOops = NULL; /* parallel to table, lazily filled */
static int count = 0;
static int capacity = 0;

static char *dupString(const char *s) {
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

/* Finds (or creates) the slot for name, growing both parallel arrays
 * together. Returns the index; table[index] is the interned C string. */
static int internIndex(const char *name) {
    for (int i = 0; i < count; i++) {
        if (strcmp(table[i], name) == 0) {
            return i;
        }
    }
    if (count == capacity) {
        capacity = capacity ? capacity * 2 : 32;
        table = realloc(table, sizeof(char *) * capacity);
        symbolOops = realloc(symbolOops, sizeof(oop) * capacity);
    }
    table[count] = dupString(name);
    symbolOops[count] = 0;
    return count++;
}

const char *intern(const char *name) {
    /* Two statements, not one: internIndex() can realloc (and move)
     * table, and "table[f()]" doesn't guarantee f() runs before table
     * itself is read -- that combined form previously indexed through a
     * stale, already-freed pointer once the table grew. */
    int idx = internIndex(name);
    return table[idx];
}

oop internSymbol(const char *name) {
    int idx = internIndex(name);
    if (!symbolOops[idx]) {
        SymbolObject *sym = malloc(sizeof(SymbolObject));
        sym->isa = SymbolClass;
        sym->name = table[idx];
        symbolOops[idx] = (oop)sym;
    }
    return symbolOops[idx];
}

const char *symbolName(oop symbol) {
    return ((SymbolObject *)symbol)->name;
}
