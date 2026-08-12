#ifndef OBJECT_H
#define OBJECT_H

#include <stdint.h>

/* A tagged pointer: low bit 1 means SmallInteger, low bit 0 means a
 * pointer to a heap-allocated Object. malloc'd memory is aligned to at
 * least 2 bytes, so the tag bit never collides with a real pointer. */
typedef intptr_t oop;

struct STClass;

typedef struct Object {
    struct STClass *isa;
    oop fields[]; /* instance variables / indexed slots, for future use */
} Object;

#define TAG_MASK 1
#define SMALLINT_TAG 1

static inline int oopIsSmallInteger(oop o) {
    return (o & TAG_MASK) == SMALLINT_TAG;
}

static inline oop makeSmallInteger(long value) {
    return (oop)(((intptr_t)value << 1) | SMALLINT_TAG);
}

static inline long smallIntegerValue(oop o) {
    return (long)(o >> 1);
}

#endif
