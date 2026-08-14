#ifndef ACTIVATION_H
#define ACTIVATION_H

#include <setjmp.h>

#include "object.h"

struct STClass;

/* One method or block-invocation activation. Lives in its own header (not
 * eval.c, where the rest of this design is explained) purely so gc.c can
 * trace self/argValues/tempValues/lexicalParent/caller without eval.c
 * exposing anything else -- see CLAUDE.md for the full mechanism (why
 * activations are heap-allocated, what lexicalParent vs
 * homeMethodActivation vs caller each mean, and why '^' uses
 * setjmp/longjmp into returnPoint). gc.c is the only file besides eval.c
 * that ever dereferences one of these directly. */
typedef struct Activation {
    oop self;
    struct STClass *homeClass; /* method activations only; see homeMethodActivation */
    char **argNames;
    oop *argValues;
    int argCount;
    char **tempNames;
    oop *tempValues;
    int tempCount;
    struct Activation *lexicalParent;
    struct Activation *homeMethodActivation;
    struct Activation *caller;
    jmp_buf returnPoint; /* method activations only; longjmp target for '^' */
    oop returnValue;     /* set just before longjmp into returnPoint */
} Activation;

#endif
