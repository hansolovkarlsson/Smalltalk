#ifndef CLASS_H
#define CLASS_H

#include "ast.h"
#include "object.h"

typedef oop (*PrimitiveFn)(oop receiver, oop *args, int argc);

typedef enum { METHOD_PRIMITIVE, METHOD_COMPILED } MethodKind;

/* An AST-based method body installed by Class>>compile: (see primitives.c
 * and parser.c's parseMethod()). homeClass is the class it was compiled
 * into, which is where a "super" send inside it starts superclass lookup
 * -- not classOf(receiver), which may be a subclass. */
typedef struct CompiledMethod {
    char **argNames; /* interned, argCount entries */
    int argCount;
    char **tempNames; /* interned, tempCount entries */
    int tempCount;
    AstNode **statements;
    int statementCount;
    struct STClass *homeClass;
} CompiledMethod;

typedef struct {
    const char *selector; /* interned */
    MethodKind kind;
    PrimitiveFn fn;             /* kind == METHOD_PRIMITIVE */
    CompiledMethod *compiled; /* kind == METHOD_COMPILED */
} MethodEntry;

/* Classes are a distinguished C struct, not heap Objects -- there's no
 * full metaclass hierarchy. But a class still needs to be a valid message
 * receiver (for `Point new`, `Object subclass: ...`, etc.), so each STClass
 * owns one ClassObject wrapper (classOop) that *is* a heap oop and can flow
 * through sendMessage() like anything else; see classOf() and ClassClass. */
typedef struct STClass {
    const char *name; /* interned; stable for the process lifetime */
    struct STClass *superclass;
    MethodEntry *methods;
    int methodCount;
    int methodCapacity;

    /* Instance variable names, superclass's followed by this class's own,
     * in order -- so a field index computed against any class in a chain
     * stays valid for its subclasses. Only ever non-empty for classes
     * defined via subclass:instanceVariableNames: (the six bootstrap
     * classes below all have instanceVarCount 0 and use their own C
     * layouts instead). */
    char **instanceVarNames; /* interned, instanceVarCount entries */
    int instanceVarCount;

    oop classOop; /* this class's ClassObject wrapper, isa == ClassClass */
} STClass;

/* Wraps an STClass so it can be a normal oop -- the receiver of `new`,
 * `subclass:instanceVariableNames:`, `compile:`, etc. isa is always
 * ClassClass (see bootstrapClasses()), so classOf() reads it uniformly
 * like Object/StringObject/SymbolObject. */
typedef struct ClassObject {
    struct STClass *isa;
    struct STClass *thisClass;
} ClassObject;

extern STClass *ObjectClass;
extern STClass *UndefinedObjectClass;
extern STClass *BooleanClass;
extern STClass *TrueClass;
extern STClass *FalseClass;
extern STClass *SmallIntegerClass;
extern STClass *StringClass;
extern STClass *SymbolClass;
extern STClass *ClassClass;

extern oop nilObject;
extern oop trueObject;
extern oop falseObject;

void bootstrapClasses(void);
void classAddPrimitive(STClass *cls, const char *selector, PrimitiveFn fn);
void classAddCompiledMethod(STClass *cls, const char *selector, CompiledMethod *method);
MethodEntry *lookupMethod(STClass *cls, const char *selector);
STClass *classOf(oop o);

/* Allocates a new instance of cls with its instance variable slots all
 * initialized to nil. Used by both bootstrap (nil/true/false) and the
 * `new` primitive on ClassClass. */
oop instantiate(STClass *cls);

/* Creates a new class as a subclass of superclass with its own instance
 * variables (ivarNames entries must already be interned), registers its
 * ClassObject wrapper, and binds `name` as a global variable to it (see
 * environment.h) so plain identifiers can refer to it, e.g. `Point new`. */
STClass *defineSubclass(STClass *superclass, const char *name, char **ivarNames, int ivarCount);

/* Defined in primitives.c, called once from bootstrapClasses(). */
void installPrimitives(void);

#endif
