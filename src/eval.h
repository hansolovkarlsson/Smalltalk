#ifndef EVAL_H
#define EVAL_H

#include "ast.h"
#include "object.h"

/* selector must be an interned string (see symbol.h) -- method lookup
 * compares selectors by pointer identity. */
oop sendMessage(oop receiver, const char *selector, oop *args, int argc);
oop eval(AstNode *node);

#endif
