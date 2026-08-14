#ifndef EVAL_H
#define EVAL_H

#include "ast.h"
#include "object.h"

struct Activation;

/* selector must be an interned string (see symbol.h) -- method lookup
 * compares selectors by pointer identity. */
oop sendMessage(oop receiver, const char *selector, oop *args, int argc);
oop eval(AstNode *node);

/* The activation of whatever's currently executing (NULL if nothing is --
 * e.g. between top-level REPL statements), for gc.c's markRoots() to walk
 * via its ->caller chain. Kept as an opaque `struct Activation *` here
 * (full layout in activation.h, which only eval.c and gc.c need) so this
 * header doesn't drag activation.h's definition into every file that
 * merely sends messages. */
struct Activation *evalCurrentActivation(void);

/* Invokes a BlockObject (block.h) with argc arguments, used by the
 * value/value:/value:value:/whileTrue:/ifTrue:ifFalse:/etc primitives
 * (primitives.c). argc must equal the block's own parameter count --
 * mismatches print an error and answer nil rather than crash, matching
 * this codebase's usual "errors degrade to nil plus stderr" convention. */
oop invokeBlock(oop block, oop *args, int argc);

#endif
