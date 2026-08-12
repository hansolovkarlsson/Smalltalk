#include "class.h"
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
    classAddPrimitive(UndefinedObjectClass, "printString", prim_nil_printString);
    classAddPrimitive(TrueClass, "printString", prim_true_printString);
    classAddPrimitive(FalseClass, "printString", prim_false_printString);

    classAddPrimitive(StringClass, "size", prim_string_size);
    classAddPrimitive(StringClass, ",", prim_string_comma);
    classAddPrimitive(StringClass, "=", prim_string_eq);
    classAddPrimitive(StringClass, "printString", prim_string_printString);

    classAddPrimitive(SymbolClass, "printString", prim_symbol_printString);
    classAddPrimitive(SymbolClass, "asString", prim_symbol_asString);
}
