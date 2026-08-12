#include "class.h"
#include "symbol.h"

#include <stdlib.h>

STClass *ObjectClass;
STClass *UndefinedObjectClass;
STClass *BooleanClass;
STClass *TrueClass;
STClass *FalseClass;
STClass *SmallIntegerClass;
STClass *StringClass;
STClass *SymbolClass;

oop nilObject;
oop trueObject;
oop falseObject;

static STClass *newClass(const char *name, STClass *superclass) {
    STClass *cls = calloc(1, sizeof(STClass));
    cls->name = name;
    cls->superclass = superclass;
    return cls;
}

void classAddPrimitive(STClass *cls, const char *selector, PrimitiveFn fn) {
    const char *interned = intern(selector);
    if (cls->methodCount == cls->methodCapacity) {
        cls->methodCapacity = cls->methodCapacity ? cls->methodCapacity * 2 : 8;
        cls->methods = realloc(cls->methods, sizeof(MethodEntry) * cls->methodCapacity);
    }
    cls->methods[cls->methodCount].selector = interned;
    cls->methods[cls->methodCount].fn = fn;
    cls->methodCount++;
}

PrimitiveFn lookupMethod(STClass *cls, const char *selector) {
    for (STClass *c = cls; c != NULL; c = c->superclass) {
        for (int i = 0; i < c->methodCount; i++) {
            /* selector is interned, so pointer equality is sufficient */
            if (c->methods[i].selector == selector) {
                return c->methods[i].fn;
            }
        }
    }
    return NULL;
}

STClass *classOf(oop o) {
    if (oopIsSmallInteger(o)) {
        return SmallIntegerClass;
    }
    Object *obj = (Object *)o;
    return obj->isa;
}

static oop allocInstance(STClass *cls) {
    Object *obj = calloc(1, sizeof(Object));
    obj->isa = cls;
    return (oop)obj;
}

void bootstrapClasses(void) {
    ObjectClass = newClass("Object", NULL);
    UndefinedObjectClass = newClass("UndefinedObject", ObjectClass);
    BooleanClass = newClass("Boolean", ObjectClass);
    TrueClass = newClass("True", BooleanClass);
    FalseClass = newClass("False", BooleanClass);
    SmallIntegerClass = newClass("SmallInteger", ObjectClass);
    StringClass = newClass("String", ObjectClass);
    /* Symbol is kept separate from String (not a subclass) since its heap
     * layout differs -- it wraps an already-interned name rather than
     * owning a byte buffer. Real Smalltalk makes Symbol a String subclass;
     * that needs a shared indexable representation, which is future work. */
    SymbolClass = newClass("Symbol", ObjectClass);

    nilObject = allocInstance(UndefinedObjectClass);
    trueObject = allocInstance(TrueClass);
    falseObject = allocInstance(FalseClass);

    installPrimitives();
}
