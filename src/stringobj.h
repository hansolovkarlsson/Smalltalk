#ifndef STRINGOBJ_H
#define STRINGOBJ_H

#include "object.h"

/* A String owns a private copy of its bytes (unlike Symbol, which points
 * into the shared intern table). Starts with STClass *isa like Object, so
 * classOf() can read it uniformly regardless of the concrete heap layout. */
typedef struct StringObject {
    struct STClass *isa;
    long length;
    char bytes[]; /* NUL-terminated for convenience; length is authoritative */
} StringObject;

oop makeString(const char *cstr);
oop makeStringN(const char *bytes, long length);

#endif
