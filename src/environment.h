#ifndef ENVIRONMENT_H
#define ENVIRONMENT_H

#include "object.h"

/* Global workspace-variable table for the REPL. name must be interned
 * (see symbol.h intern()) -- lookups compare by pointer identity. */
void envSet(const char *name, oop value);
int envLookup(const char *name, oop *outValue); /* returns 0 if undeclared */

#endif
