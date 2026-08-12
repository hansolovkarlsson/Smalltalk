#ifndef SYMBOL_H
#define SYMBOL_H

/* Interns a selector/identifier string so that equal names share one
 * pointer, letting method lookup compare selectors by pointer equality. */
const char *intern(const char *name);

#endif
