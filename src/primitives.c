#include "class.h"
#include "eval.h"
#include "parser.h"
#include "stringobj.h"
#include "symbol.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static oop prim_add(oop receiver, oop *args, int argc) {
    (void)argc;
    return makeSmallInteger(smallIntegerValue(receiver) + smallIntegerValue(args[0]));
}

static oop prim_sub(oop receiver, oop *args, int argc) {
    (void)argc;
    return makeSmallInteger(smallIntegerValue(receiver) - smallIntegerValue(args[0]));
}

static oop prim_mul(oop receiver, oop *args, int argc) {
    (void)argc;
    return makeSmallInteger(smallIntegerValue(receiver) * smallIntegerValue(args[0]));
}

static oop prim_div(oop receiver, oop *args, int argc) {
    (void)argc;
    long divisor = smallIntegerValue(args[0]);
    if (divisor == 0) {
        fprintf(stderr, "error: division by zero\n");
        return nilObject;
    }
    return makeSmallInteger(smallIntegerValue(receiver) / divisor);
}

static oop prim_eq(oop receiver, oop *args, int argc) {
    (void)argc;
    return (smallIntegerValue(receiver) == smallIntegerValue(args[0])) ? trueObject : falseObject;
}

static oop prim_lt(oop receiver, oop *args, int argc) {
    (void)argc;
    return (smallIntegerValue(receiver) < smallIntegerValue(args[0])) ? trueObject : falseObject;
}

static oop prim_gt(oop receiver, oop *args, int argc) {
    (void)argc;
    return (smallIntegerValue(receiver) > smallIntegerValue(args[0])) ? trueObject : falseObject;
}

static oop prim_le(oop receiver, oop *args, int argc) {
    (void)argc;
    return (smallIntegerValue(receiver) <= smallIntegerValue(args[0])) ? trueObject : falseObject;
}

static oop prim_ge(oop receiver, oop *args, int argc) {
    (void)argc;
    return (smallIntegerValue(receiver) >= smallIntegerValue(args[0])) ? trueObject : falseObject;
}

static oop prim_negated(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return makeSmallInteger(-smallIntegerValue(receiver));
}

static oop prim_factorial(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    long n = smallIntegerValue(receiver);
    if (n < 0) {
        fprintf(stderr, "error: factorial of a negative number\n");
        return nilObject;
    }
    long result = 1;
    for (long i = 2; i <= n; i++) {
        result *= i;
    }
    return makeSmallInteger(result);
}

static oop prim_smallint_printString(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    char buf[32];
    snprintf(buf, sizeof(buf), "%ld", smallIntegerValue(receiver));
    return makeString(buf);
}

/* Default #printString, inherited by any class that doesn't override it:
 * "a ClassName" / "an ClassName" depending on the leading letter. */
static int startsWithVowel(const char *s) {
    if (!s || !s[0]) return 0;
    char c = (char)tolower((unsigned char)s[0]);
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

static oop prim_object_printString(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    const char *name = classOf(receiver)->name;
    const char *article = startsWithVowel(name) ? "an" : "a";
    size_t len = strlen(article) + 1 + strlen(name);
    char *buf = malloc(len + 1);
    snprintf(buf, len + 1, "%s %s", article, name);
    oop result = makeString(buf);
    free(buf);
    return result;
}

static oop prim_nil_printString(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)args;
    (void)argc;
    return makeString("nil");
}

static oop prim_true_printString(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)args;
    (void)argc;
    return makeString("true");
}

static oop prim_false_printString(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)args;
    (void)argc;
    return makeString("false");
}

static oop prim_string_size(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return makeSmallInteger(((StringObject *)receiver)->length);
}

static oop prim_string_comma(oop receiver, oop *args, int argc) {
    (void)argc;
    StringObject *a = (StringObject *)receiver;
    StringObject *b = (StringObject *)args[0];
    long newLen = a->length + b->length;
    char *buf = malloc((size_t)newLen);
    memcpy(buf, a->bytes, (size_t)a->length);
    memcpy(buf + a->length, b->bytes, (size_t)b->length);
    oop result = makeStringN(buf, newLen);
    free(buf);
    return result;
}

static oop prim_string_eq(oop receiver, oop *args, int argc) {
    (void)argc;
    StringObject *a = (StringObject *)receiver;
    StringObject *b = (StringObject *)args[0];
    if (a->length != b->length) return falseObject;
    return memcmp(a->bytes, b->bytes, (size_t)a->length) == 0 ? trueObject : falseObject;
}

/* Renders as Smalltalk source would read it back: quoted, with embedded
 * quotes doubled (the standard Smalltalk string-literal escape). */
static oop prim_string_printString(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    StringObject *s = (StringObject *)receiver;
    long extra = 2;
    for (long i = 0; i < s->length; i++) {
        if (s->bytes[i] == '\'') extra++;
    }
    char *buf = malloc((size_t)(s->length + extra));
    long j = 0;
    buf[j++] = '\'';
    for (long i = 0; i < s->length; i++) {
        char c = s->bytes[i];
        buf[j++] = c;
        if (c == '\'') buf[j++] = '\'';
    }
    buf[j++] = '\'';
    oop result = makeStringN(buf, j);
    free(buf);
    return result;
}

static oop prim_symbol_printString(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    const char *name = symbolName(receiver);
    size_t len = strlen(name);
    char *buf = malloc(len + 1);
    buf[0] = '#';
    memcpy(buf + 1, name, len);
    oop result = makeStringN(buf, (long)(len + 1));
    free(buf);
    return result;
}

static oop prim_symbol_asString(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return makeString(symbolName(receiver));
}

/* Any object's class, e.g. `3 class` -> SmallInteger. classOop is set for
 * every STClass (bootstrap or user-defined) by class.c's registerClass(),
 * so this is total. */
static oop prim_object_class(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return classOf(receiver)->classOop;
}

static oop prim_class_new(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return instantiate(((ClassObject *)receiver)->thisClass);
}

static oop prim_class_printString(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return makeString(((ClassObject *)receiver)->thisClass->name);
}

/* Object subclass: #Point instanceVariableNames: 'x y'
 * args[0] is expected to be a Symbol (the new class's name), args[1] a
 * String of space-separated instance variable names. Neither is
 * type-checked before casting, matching this codebase's existing
 * primitive convention (see CLAUDE.md). */
static oop prim_class_subclass_instanceVariableNames(oop receiver, oop *args, int argc) {
    (void)argc;
    STClass *super = ((ClassObject *)receiver)->thisClass;
    const char *className = symbolName(args[0]);
    StringObject *ivarStr = (StringObject *)args[1];

    char *copy = malloc((size_t)ivarStr->length + 1);
    memcpy(copy, ivarStr->bytes, (size_t)ivarStr->length);
    copy[ivarStr->length] = '\0';

    char **names = NULL;
    int count = 0, capacity = 0;
    char *tok = strtok(copy, " \t");
    while (tok) {
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 4;
            names = realloc(names, sizeof(char *) * capacity);
        }
        names[count++] = (char *)intern(tok);
        tok = strtok(NULL, " \t");
    }
    free(copy);

    STClass *cls = defineSubclass(super, className, names, count);
    free(names);
    return cls->classOop;
}

/* Point compile: 'setX: ax setY: ay  x := ax. y := ay. ^self'
 * Parses args[0] (a String) as a method (see parser.c's parseMethod()) and
 * installs it on the receiver class, replacing any existing method with
 * the same selector. Answers true on success, false (and a stderr
 * message) on a parse error -- mirroring this codebase's "errors degrade
 * to a value plus stderr, not an exception" convention. */
static oop prim_class_compile(oop receiver, oop *args, int argc) {
    (void)argc;
    STClass *cls = ((ClassObject *)receiver)->thisClass;
    StringObject *src = (StringObject *)args[0];

    char errorMsg[256];
    MethodNode *m = parseMethod(src->bytes, errorMsg, sizeof(errorMsg));
    if (!m) {
        fprintf(stderr, "error: compile: %s\n", errorMsg);
        return falseObject;
    }

    CompiledMethod *cm = malloc(sizeof(CompiledMethod));
    cm->argNames = m->argNames;
    cm->argCount = m->argCount;
    cm->tempNames = m->tempNames;
    cm->tempCount = m->tempCount;
    cm->statements = m->statements;
    cm->statementCount = m->statementCount;
    cm->homeClass = cls;

    classAddCompiledMethod(cls, m->selector, cm);
    free(m);
    return trueObject;
}

static oop prim_block_value0(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return invokeBlock(receiver, NULL, 0);
}

static oop prim_block_value1(oop receiver, oop *args, int argc) {
    (void)argc;
    return invokeBlock(receiver, args, 1);
}

static oop prim_block_value2(oop receiver, oop *args, int argc) {
    (void)argc;
    return invokeBlock(receiver, args, 2);
}

/* [cond] whileTrue: [body] -- the receiver (a 0-arg block) is re-evaluated
 * before each iteration; the loop runs while it answers true. Doesn't
 * type-check that receiver's value is actually a Boolean, matching this
 * codebase's usual primitive convention: anything other than true/false
 * (including a non-Boolean that happens to share a bit pattern) just fails
 * the `== trueObject` check and ends the loop rather than erroring. */
static oop prim_block_whileTrue(oop receiver, oop *args, int argc) {
    (void)argc;
    while (invokeBlock(receiver, NULL, 0) == trueObject) {
        invokeBlock(args[0], NULL, 0);
    }
    return nilObject;
}

static oop prim_block_whileFalse(oop receiver, oop *args, int argc) {
    (void)argc;
    while (invokeBlock(receiver, NULL, 0) == falseObject) {
        invokeBlock(args[0], NULL, 0);
    }
    return nilObject;
}

static oop prim_true_ifTrue(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)argc;
    return invokeBlock(args[0], NULL, 0);
}

static oop prim_true_ifFalse(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)args;
    (void)argc;
    return nilObject;
}

static oop prim_true_ifTrueIfFalse(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)argc;
    return invokeBlock(args[0], NULL, 0);
}

static oop prim_false_ifTrue(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)args;
    (void)argc;
    return nilObject;
}

static oop prim_false_ifFalse(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)argc;
    return invokeBlock(args[0], NULL, 0);
}

static oop prim_false_ifTrueIfFalse(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)argc;
    return invokeBlock(args[1], NULL, 0);
}

/* and:/or: are the short-circuiting, block-argument counterparts to the
 * (still unimplemented, see LANGUAGE.md) eager `&`/`|`: the block is only
 * ever evaluated when its value could actually change the result. */
static oop prim_true_and(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)argc;
    return invokeBlock(args[0], NULL, 0);
}

static oop prim_true_or(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return receiver;
}

static oop prim_false_and(oop receiver, oop *args, int argc) {
    (void)args;
    (void)argc;
    return receiver;
}

static oop prim_false_or(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)argc;
    return invokeBlock(args[0], NULL, 0);
}

static oop prim_true_not(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)args;
    (void)argc;
    return falseObject;
}

static oop prim_false_not(oop receiver, oop *args, int argc) {
    (void)receiver;
    (void)args;
    (void)argc;
    return trueObject;
}

void installPrimitives(void) {
    classAddPrimitive(SmallIntegerClass, "+", prim_add);
    classAddPrimitive(SmallIntegerClass, "-", prim_sub);
    classAddPrimitive(SmallIntegerClass, "*", prim_mul);
    classAddPrimitive(SmallIntegerClass, "/", prim_div);
    classAddPrimitive(SmallIntegerClass, "=", prim_eq);
    classAddPrimitive(SmallIntegerClass, "<", prim_lt);
    classAddPrimitive(SmallIntegerClass, ">", prim_gt);
    classAddPrimitive(SmallIntegerClass, "<=", prim_le);
    classAddPrimitive(SmallIntegerClass, ">=", prim_ge);
    classAddPrimitive(SmallIntegerClass, "negated", prim_negated);
    classAddPrimitive(SmallIntegerClass, "factorial", prim_factorial);
    classAddPrimitive(SmallIntegerClass, "printString", prim_smallint_printString);

    classAddPrimitive(ObjectClass, "printString", prim_object_printString);
    classAddPrimitive(ObjectClass, "class", prim_object_class);
    classAddPrimitive(UndefinedObjectClass, "printString", prim_nil_printString);
    classAddPrimitive(TrueClass, "printString", prim_true_printString);
    classAddPrimitive(FalseClass, "printString", prim_false_printString);

    classAddPrimitive(StringClass, "size", prim_string_size);
    classAddPrimitive(StringClass, ",", prim_string_comma);
    classAddPrimitive(StringClass, "=", prim_string_eq);
    classAddPrimitive(StringClass, "printString", prim_string_printString);

    classAddPrimitive(SymbolClass, "printString", prim_symbol_printString);
    classAddPrimitive(SymbolClass, "asString", prim_symbol_asString);

    classAddPrimitive(ClassClass, "new", prim_class_new);
    classAddPrimitive(ClassClass, "printString", prim_class_printString);
    classAddPrimitive(ClassClass, "subclass:instanceVariableNames:",
                       prim_class_subclass_instanceVariableNames);
    classAddPrimitive(ClassClass, "compile:", prim_class_compile);

    classAddPrimitive(BlockClass, "value", prim_block_value0);
    classAddPrimitive(BlockClass, "value:", prim_block_value1);
    classAddPrimitive(BlockClass, "value:value:", prim_block_value2);
    classAddPrimitive(BlockClass, "whileTrue:", prim_block_whileTrue);
    classAddPrimitive(BlockClass, "whileFalse:", prim_block_whileFalse);

    classAddPrimitive(TrueClass, "ifTrue:", prim_true_ifTrue);
    classAddPrimitive(TrueClass, "ifFalse:", prim_true_ifFalse);
    classAddPrimitive(TrueClass, "ifTrue:ifFalse:", prim_true_ifTrueIfFalse);
    classAddPrimitive(TrueClass, "and:", prim_true_and);
    classAddPrimitive(TrueClass, "or:", prim_true_or);
    classAddPrimitive(TrueClass, "not", prim_true_not);

    classAddPrimitive(FalseClass, "ifTrue:", prim_false_ifTrue);
    classAddPrimitive(FalseClass, "ifFalse:", prim_false_ifFalse);
    classAddPrimitive(FalseClass, "ifTrue:ifFalse:", prim_false_ifTrueIfFalse);
    classAddPrimitive(FalseClass, "and:", prim_false_and);
    classAddPrimitive(FalseClass, "or:", prim_false_or);
    classAddPrimitive(FalseClass, "not", prim_false_not);
}
