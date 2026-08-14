#include "stringobj.h"
#include "class.h"
#include "gc.h"

#include <stdlib.h>
#include <string.h>

oop makeStringN(const char *bytes, long length) {
    StringObject *s = gcAlloc(GC_KIND_OOP, sizeof(StringObject) + (size_t)length + 1);
    s->isa = StringClass;
    s->length = length;
    memcpy(s->bytes, bytes, (size_t)length);
    s->bytes[length] = '\0';
    return (oop)s;
}

oop makeString(const char *cstr) {
    return makeStringN(cstr, (long)strlen(cstr));
}
