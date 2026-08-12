#include "class.h"

#include <stdio.h>

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
}
