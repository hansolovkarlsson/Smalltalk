#ifndef EVAL_H
#define EVAL_H

#include "ast.h"
#include "object.h"

/* selector must be an interned string (see symbol.h) -- method lookup
 * compares selectors by pointer identity. */
oop sendMessage(oop receiver, const char *selector, oop *args, int argc);
oop eval(AstNode *node);

/* Invokes a BlockObject (block.h) with argc arguments, used by the
 * value/value:/value:value:/whileTrue:/ifTrue:ifFalse:/etc primitives
 * (primitives.c). argc must equal the block's own parameter count --
 * mismatches print an error and answer nil rather than crash, matching
 * this codebase's usual "errors degrade to nil plus stderr" convention. */
oop invokeBlock(oop block, oop *args, int argc);

#endif
