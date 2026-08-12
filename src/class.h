#ifndef CLASS_H
#define CLASS_H

#include "object.h"

typedef oop (*PrimitiveFn)(oop receiver, oop *args, int argc);

typedef struct {
    const char *selector; /* interned */
    PrimitiveFn fn;
} MethodEntry;

/* Classes are a distinguished C struct for now, not heap Objects with a
 * metaclass -- that's a later milestone. */
typedef struct STClass {
    const char *name;
    struct STClass *superclass;
    MethodEntry *methods;
    int methodCount;
    int methodCapacity;
} STClass;

extern STClass *ObjectClass;
extern STClass *UndefinedObjectClass;
extern STClass *BooleanClass;
extern STClass *TrueClass;
extern STClass *FalseClass;
extern STClass *SmallIntegerClass;
extern STClass *StringClass;
extern STClass *SymbolClass;

extern oop nilObject;
extern oop trueObject;
extern oop falseObject;

void bootstrapClasses(void);
void classAddPrimitive(STClass *cls, const char *selector, PrimitiveFn fn);
PrimitiveFn lookupMethod(STClass *cls, const char *selector);
STClass *classOf(oop o);

/* Defined in primitives.c, called once from bootstrapClasses(). */
void installPrimitives(void);

#endif
