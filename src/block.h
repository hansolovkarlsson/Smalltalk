#ifndef BLOCK_H
#define BLOCK_H

#include "bytecode.h"
#include "object.h"

/* Opaque here on purpose: only eval.c constructs/walks Activations (they
 * hold the closed-over variables). block.h just needs a pointer to one so
 * a BlockObject can carry the lexical context it was created in --
 * primitives.c never dereferences homeActivation itself, it only passes
 * the whole BlockObject to eval.c's invokeBlock(). */
struct Activation;

/* A block literal's runtime value: its own compiled body (shared with
 * every invocation of this literal, built once by compiler.c's
 * compileBlockBody() and never copied) plus the Activation that was
 * current when the literal was evaluated -- that's the entire closure
 * mechanism. isa is always BlockClass, and comes first so classOf() reads
 * it uniformly with every other heap layout (see object.h). */
typedef struct BlockObject {
    struct STClass *isa;
    char **paramNames; /* interned, paramCount entries */
    int paramCount;
    CompiledCode *code;
    struct Activation *homeActivation; /* NULL if defined outside any method/block */
} BlockObject;

#endif
