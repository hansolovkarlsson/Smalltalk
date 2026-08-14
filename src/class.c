#include "class.h"
#include "environment.h"
#include "gc.h"
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
STClass *ClassClass;
STClass *BlockClass;

oop nilObject;
oop trueObject;
oop falseObject;

static STClass *allocClass(const char *name, STClass *superclass) {
    STClass *cls = calloc(1, sizeof(STClass));
    cls->name = intern(name);
    cls->superclass = superclass;
    return cls;
}

/* Wraps cls in a ClassObject so it can be a message receiver, and binds
 * its name as a global variable (e.g. `Point`) so ordinary identifier
 * lookup resolves it -- see environment.h. ClassClass must already point
 * at a real STClass before this is first called (it registers itself,
 * self-referentially: its own classOop.isa is ClassClass). */
static void registerClass(STClass *cls) {
    ClassObject *co = gcAlloc(GC_KIND_OOP, sizeof(ClassObject));
    co->isa = ClassClass;
    co->thisClass = cls;
    cls->classOop = (oop)co;
    envSet(intern(cls->name), cls->classOop);
}

static STClass *newClass(const char *name, STClass *superclass) {
    STClass *cls = allocClass(name, superclass);
    registerClass(cls);
    return cls;
}

STClass *defineSubclass(STClass *superclass, const char *name, char **ivarNames, int ivarCount) {
    STClass *cls = allocClass(name, superclass);

    int inherited = superclass ? superclass->instanceVarCount : 0;
    cls->instanceVarCount = inherited + ivarCount;
    if (cls->instanceVarCount > 0) {
        cls->instanceVarNames = malloc(sizeof(char *) * (size_t)cls->instanceVarCount);
        for (int i = 0; i < inherited; i++) {
            cls->instanceVarNames[i] = superclass->instanceVarNames[i];
        }
        for (int i = 0; i < ivarCount; i++) {
            cls->instanceVarNames[inherited + i] = ivarNames[i];
        }
    }

    registerClass(cls);
    return cls;
}

/* Finds selector's existing slot in cls's OWN method table (not the
 * superclass chain) so that redefining a method -- expected, routine
 * behavior when iterating on a method via compile: at the REPL -- replaces
 * it in place. Without this, lookupMethod()'s front-to-back scan would
 * keep finding the stale first copy forever after a redefinition. */
static MethodEntry *findOrAddSlot(STClass *cls, const char *selector) {
    for (int i = 0; i < cls->methodCount; i++) {
        if (cls->methods[i].selector == selector) {
            return &cls->methods[i];
        }
    }
    if (cls->methodCount == cls->methodCapacity) {
        cls->methodCapacity = cls->methodCapacity ? cls->methodCapacity * 2 : 8;
        cls->methods = realloc(cls->methods, sizeof(MethodEntry) * cls->methodCapacity);
    }
    MethodEntry *slot = &cls->methods[cls->methodCount++];
    slot->selector = selector;
    return slot;
}

void classAddPrimitive(STClass *cls, const char *selector, PrimitiveFn fn) {
    MethodEntry *slot = findOrAddSlot(cls, intern(selector));
    slot->kind = METHOD_PRIMITIVE;
    slot->fn = fn;
    slot->compiled = NULL;
}

void classAddCompiledMethod(STClass *cls, const char *selector, CompiledMethod *method) {
    /* selector comes from parseMethod(), already interned. */
    MethodEntry *slot = findOrAddSlot(cls, selector);
    slot->kind = METHOD_COMPILED;
    slot->fn = NULL;
    slot->compiled = method;
}

MethodEntry *lookupMethod(STClass *cls, const char *selector) {
    for (STClass *c = cls; c != NULL; c = c->superclass) {
        for (int i = 0; i < c->methodCount; i++) {
            /* selector is interned, so pointer equality is sufficient */
            if (c->methods[i].selector == selector) {
                return &c->methods[i];
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

oop instantiate(STClass *cls) {
    Object *obj = gcAlloc(GC_KIND_OOP, sizeof(Object) + sizeof(oop) * (size_t)cls->instanceVarCount);
    obj->isa = cls;
    for (int i = 0; i < cls->instanceVarCount; i++) {
        obj->fields[i] = nilObject;
    }
    return (oop)obj;
}

void bootstrapClasses(void) {
    /* ClassClass registers itself: its own classOop.isa is ClassClass. */
    ClassClass = allocClass("Class", NULL);
    registerClass(ClassClass);

    ObjectClass = newClass("Object", NULL);
    /* Class couldn't be a subclass of Object above -- Object didn't exist
     * yet, and Object has to exist before anything else can subclass it.
     * Patched in now so class objects (Point, String, ...) inherit the
     * default protocol too, e.g. `Point class` via Object's #class. */
    ClassClass->superclass = ObjectClass;
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
    /* A block literal's runtime value (block.h) -- new instances are only
     * ever created by eval.c's AST_BLOCK_LITERAL case, never via `new`. */
    BlockClass = newClass("Block", ObjectClass);

    nilObject = instantiate(UndefinedObjectClass);
    trueObject = instantiate(TrueClass);
    falseObject = instantiate(FalseClass);

    installPrimitives();
}
