#ifndef SYMBOL_H
#define SYMBOL_H

#include "object.h"

/* Interns a selector/identifier string so that equal names share one
 * pointer, letting method lookup compare selectors by pointer equality. */
const char *intern(const char *name);

/* Symbol wraps an already-interned name rather than owning a byte buffer.
 * Starts with STClass *isa like Object, so classOf() reads it uniformly. */
typedef struct SymbolObject {
    struct STClass *isa;
    const char *name;
} SymbolObject;

/* Returns the unique Symbol object for name, creating it on first use, so
 * that Symbol identity matches name identity: #foo == #foo. */
oop internSymbol(const char *name);

const char *symbolName(oop symbol);

/* Marks every interned Symbol as a GC root (gc.h). In practice this means
 * a Symbol, once created, is never collected -- same as a real
 * Smalltalk's SymbolTable, which is exactly what this table already is. */
void symbolMarkRoots(void);

#endif
