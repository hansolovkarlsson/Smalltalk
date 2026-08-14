#include "eval.h"
#include "activation.h"
#include "block.h"
#include "class.h"
#include "environment.h"
#include "gc.h"
#include "stringobj.h"
#include "symbol.h"

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Every activation is heap-allocated via gcAlloc(), not malloc()/a stack
 * local: a block literal evaluated inside one captures a pointer to it
 * (BlockObject.homeActivation, block.h), and that block can escape and be
 * invoked long after the activation that created it would otherwise have
 * returned -- e.g. stored in a variable and called later, or returned as
 * the method's result. A stack-local Activation (as before blocks
 * existed) would dangle in exactly that case; an unmanaged malloc'd one
 * (as before garbage collection existed) would never be reclaimed even
 * once truly unreachable. gc.c traces one via its own copy of this
 * struct's layout (activation.h) -- see gcMarkActivation() there, and
 * markRoots()'s use of evalCurrentActivation() below for how the
 * in-progress call chain becomes a root without scanning the C stack for
 * it specifically.
 *
 * lexicalParent is the closure chain: the activation that was current when
 * this one's code was *written* (for a method activation, always NULL --
 * methods aren't nested in other methods; for a block invocation, the
 * activation captured at the block literal's evaluation, which may itself
 * be another block's invocation for a nested block). activationLookup()/
 * activationStore() walk this chain, which is exactly how a block sees its
 * enclosing method's args/temps/self.
 *
 * homeMethodActivation is the *dynamic* return target for a non-local '^'
 * (see runStatementSequence()): the nearest lexically enclosing METHOD
 * activation, found by walking lexicalParent all the way up. For a method
 * activation it's always itself. caller is unrelated to either of these --
 * it's the ordinary dynamic call chain (who invoked this activation),
 * restored into currentActivation when this activation's call finishes. */

static Activation *currentActivation = NULL;

Activation *evalCurrentActivation(void) {
    return currentActivation;
}

static oop invokeCompiledMethod(CompiledMethod *cm, oop receiver, oop *args);

static oop dispatchFrom(STClass *startClass, oop receiver, const char *selector, oop *args, int argc) {
    MethodEntry *m = lookupMethod(startClass, selector);
    if (!m) {
        fprintf(stderr, "error: %s does not understand #%s\n", classOf(receiver)->name, selector);
        return nilObject;
    }
    if (m->kind == METHOD_PRIMITIVE) {
        return m->fn(receiver, args, argc);
    }
    return invokeCompiledMethod(m->compiled, receiver, args);
}

oop sendMessage(oop receiver, const char *selector, oop *args, int argc) {
    return dispatchFrom(classOf(receiver), receiver, selector, args, argc);
}

/* Runs a '.'-separated statement list shared by both a compiled method's
 * body and a block's body (invokeCompiledMethod()/invokeBlock() below).
 * They differ only in what "falling off the end without a '^'" answers:
 * a method answers self (defaultValue, ignoring the last statement's
 * value); a block answers its last statement's value (useLastAsDefault),
 * or nil if it has none. A '^' always means "return from the nearest
 * lexically enclosing method", never "return from this block" -- so
 * every '^', even one written directly in a method's own top-level
 * statement list, unwinds via longjmp to that method's own setjmp() in
 * invokeCompiledMethod(). Jumping to your own immediately-enclosing frame
 * this way is legal C and means '^' needs no separate "am I already at
 * the right frame?" fast path. */
static oop runStatementSequence(AstNode **stmts, int count, oop defaultValue, int useLastAsDefault) {
    oop result = defaultValue;
    for (int i = 0; i < count; i++) {
        AstNode *stmt = stmts[i];
        if (stmt->type == AST_RETURN) {
            oop value = eval(stmt->as.returnValue);
            Activation *home = currentActivation->homeMethodActivation;
            if (!home) {
                fprintf(stderr, "error: '^' used outside a method\n");
                return value;
            }
            home->returnValue = value;
            longjmp(home->returnPoint, 1);
        }
        oop value = eval(stmt);
        if (useLastAsDefault) result = value;
    }
    return result;
}

/* Args arrive positionally from the send site (AST_*_SEND below), so argc
 * there always matches cm->argCount: the selector's arity is baked into
 * its shape (unary/binary/N-keyword) by construction, and lookupMethod()
 * matched on that same selector. Copies args/temps into the activation's
 * own storage rather than aliasing the caller's buffer -- the caller's
 * buffer (e.g. AST_KEYWORD_SEND's local `args`) is freed or goes out of
 * scope right after this call returns, but a block created in this method
 * body that captures this activation can outlive that. */
static oop invokeCompiledMethod(CompiledMethod *cm, oop receiver, oop *args) {
    /* gcAlloc() zero-initializes, so act is a safe (all-NULL/0) target for
     * gcMarkActivation() even in the narrow window before every field
     * below is set -- not that anything here currently triggers a nested
     * collection in that window (only plain malloc() calls follow, not
     * gcAlloc()), but future edits shouldn't have to reason about it. */
    Activation *act = gcAlloc(GC_KIND_ACTIVATION, sizeof(Activation));
    act->self = receiver;
    act->homeClass = cm->homeClass;
    act->argNames = cm->argNames;
    act->argCount = cm->argCount;
    act->argValues = NULL;
    if (act->argCount > 0) {
        act->argValues = malloc(sizeof(oop) * (size_t)act->argCount);
        memcpy(act->argValues, args, sizeof(oop) * (size_t)act->argCount);
    }
    act->tempNames = cm->tempNames;
    act->tempCount = cm->tempCount;
    act->tempValues = NULL;
    if (act->tempCount > 0) {
        act->tempValues = malloc(sizeof(oop) * (size_t)act->tempCount);
        for (int i = 0; i < act->tempCount; i++) act->tempValues[i] = nilObject;
    }
    act->lexicalParent = NULL; /* methods aren't lexically nested */
    act->homeMethodActivation = act;
    act->caller = currentActivation;
    currentActivation = act;

    oop result;
    if (setjmp(act->returnPoint) == 0) {
        result = runStatementSequence(cm->statements, cm->statementCount, receiver, 0);
    } else {
        result = act->returnValue; /* landed here via a '^' inside this method or a block it made */
    }

    currentActivation = act->caller;
    return result;
}

oop invokeBlock(oop blockOop, oop *args, int argc) {
    BlockObject *blk = (BlockObject *)blockOop;
    if (argc != blk->paramCount) {
        fprintf(stderr, "error: wrong number of block arguments (expected %d, got %d)\n", blk->paramCount,
                argc);
        return nilObject;
    }

    Activation *frame = gcAlloc(GC_KIND_ACTIVATION, sizeof(Activation));
    frame->self = blk->homeActivation ? blk->homeActivation->self : nilObject;
    frame->homeClass = NULL; /* not a method activation; see homeMethodActivation */
    frame->argNames = blk->paramNames;
    frame->argCount = blk->paramCount;
    frame->argValues = NULL;
    if (frame->argCount > 0) {
        frame->argValues = malloc(sizeof(oop) * (size_t)frame->argCount);
        memcpy(frame->argValues, args, sizeof(oop) * (size_t)frame->argCount);
    }
    frame->tempNames = NULL;
    frame->tempCount = 0;
    frame->tempValues = NULL;
    frame->lexicalParent = blk->homeActivation;
    frame->homeMethodActivation = blk->homeActivation ? blk->homeActivation->homeMethodActivation : NULL;
    frame->caller = currentActivation;
    currentActivation = frame;

    oop result = runStatementSequence(blk->statements, blk->statementCount, nilObject, 1);

    currentActivation = frame->caller;
    return result;
}

static int isSuperNode(AstNode *node) {
    return node->type == AST_SUPER;
}

/* The class a `super` send inside whatever's currently running should
 * start superclass lookup from -- the defining class of the nearest
 * lexically enclosing method, not classOf(self). NULL (with an error
 * already printed by the caller) if there is no enclosing method, e.g.
 * `super` used inside a block defined at the top level. */
static STClass *currentSuperclass(void) {
    if (!currentActivation || !currentActivation->homeMethodActivation) return NULL;
    return currentActivation->homeMethodActivation->homeClass->superclass;
}

/* Looks up name across the lexical closure chain (this activation's own
 * args/temps, then its lexicalParent's, and so on out to the enclosing
 * method), then self's instance variables -- the only non-global scopes
 * that exist. self is identical across the whole chain by construction
 * (see invokeBlock()), so instance variables only need checking once,
 * against currentActivation->self, not once per frame. Returns 1 and sets
 * *outValue on a hit. */
static int activationLookup(const char *name, oop *outValue) {
    for (Activation *a = currentActivation; a != NULL; a = a->lexicalParent) {
        for (int i = 0; i < a->argCount; i++) {
            if (a->argNames[i] == name) {
                *outValue = a->argValues[i];
                return 1;
            }
        }
        for (int i = 0; i < a->tempCount; i++) {
            if (a->tempNames[i] == name) {
                *outValue = a->tempValues[i];
                return 1;
            }
        }
    }
    if (currentActivation) {
        STClass *selfClass = classOf(currentActivation->self);
        for (int i = 0; i < selfClass->instanceVarCount; i++) {
            if (selfClass->instanceVarNames[i] == name) {
                *outValue = ((Object *)currentActivation->self)->fields[i];
                return 1;
            }
        }
    }
    return 0;
}

/* Mirrors activationLookup(), but stores. Returns 1 on a hit. */
static int activationStore(const char *name, oop value) {
    for (Activation *a = currentActivation; a != NULL; a = a->lexicalParent) {
        for (int i = 0; i < a->argCount; i++) {
            if (a->argNames[i] == name) {
                a->argValues[i] = value;
                return 1;
            }
        }
        for (int i = 0; i < a->tempCount; i++) {
            if (a->tempNames[i] == name) {
                a->tempValues[i] = value;
                return 1;
            }
        }
    }
    if (currentActivation) {
        STClass *selfClass = classOf(currentActivation->self);
        for (int i = 0; i < selfClass->instanceVarCount; i++) {
            if (selfClass->instanceVarNames[i] == name) {
                ((Object *)currentActivation->self)->fields[i] = value;
                return 1;
            }
        }
    }
    return 0;
}

oop eval(AstNode *node) {
    switch (node->type) {
        case AST_INT_LITERAL:
            return makeSmallInteger(node->as.intValue);
        case AST_NIL_LITERAL:
            return nilObject;
        case AST_TRUE_LITERAL:
            return trueObject;
        case AST_FALSE_LITERAL:
            return falseObject;
        case AST_STRING_LITERAL:
            return makeString(node->as.stringValue);
        case AST_SYMBOL_LITERAL:
            return internSymbol(node->as.symbolName);
        case AST_SELF:
        case AST_SUPER:
            if (currentActivation) return currentActivation->self;
            fprintf(stderr, "error: '%s' used outside a method\n",
                    node->type == AST_SELF ? "self" : "super");
            return nilObject;
        case AST_RETURN:
            /* Only meaningful as a top-level statement in a method or
             * block body, handled directly by runStatementSequence();
             * reached here only if '^' appeared somewhere nested, which
             * the grammar never produces. Fall back to just evaluating it. */
            return eval(node->as.returnValue);
        case AST_BLOCK_LITERAL: {
            BlockObject *blk = gcAlloc(GC_KIND_OOP, sizeof(BlockObject));
            blk->isa = BlockClass;
            blk->paramNames = node->as.blockLiteral.paramNames;
            blk->paramCount = node->as.blockLiteral.paramCount;
            blk->statements = node->as.blockLiteral.statements;
            blk->statementCount = node->as.blockLiteral.statementCount;
            blk->homeActivation = currentActivation; /* the entire closure mechanism */
            return (oop)blk;
        }
        case AST_VARIABLE_REF: {
            oop value;
            if (activationLookup(node->as.variableName, &value)) {
                return value;
            }
            if (envLookup(node->as.variableName, &value)) {
                return value;
            }
            fprintf(stderr, "error: undefined variable '%s'\n", node->as.variableName);
            return nilObject;
        }
        case AST_ASSIGNMENT: {
            oop value = eval(node->as.assignment.value);
            if (!activationStore(node->as.assignment.name, value)) {
                envSet(node->as.assignment.name, value);
            }
            return value;
        }
        case AST_UNARY_SEND: {
            AstNode *recvNode = node->as.unarySend.receiver;
            if (isSuperNode(recvNode)) {
                STClass *start = currentSuperclass();
                if (!start) {
                    fprintf(stderr, "error: 'super' used outside a method\n");
                    return nilObject;
                }
                return dispatchFrom(start, currentActivation->self, node->as.unarySend.selector, NULL, 0);
            }
            oop receiver = eval(recvNode);
            return sendMessage(receiver, node->as.unarySend.selector, NULL, 0);
        }
        case AST_BINARY_SEND: {
            AstNode *recvNode = node->as.binarySend.receiver;
            oop arg = eval(node->as.binarySend.arg);
            oop args[1];
            args[0] = arg;
            if (isSuperNode(recvNode)) {
                STClass *start = currentSuperclass();
                if (!start) {
                    fprintf(stderr, "error: 'super' used outside a method\n");
                    return nilObject;
                }
                return dispatchFrom(start, currentActivation->self, node->as.binarySend.selector, args, 1);
            }
            oop receiver = eval(recvNode);
            return sendMessage(receiver, node->as.binarySend.selector, args, 1);
        }
        case AST_KEYWORD_SEND: {
            AstNode *recvNode = node->as.keywordSend.receiver;
            int argc = node->as.keywordSend.argCount;
            /* GC_KIND_OOP_ARRAY, not a plain malloc: a primitive can hold
             * onto this args pointer across several nested calls that may
             * each trigger a collection (Block>>whileTrue:/whileFalse:
             * re-reading args[0] every iteration) -- see gc.h's doc
             * comment on GC_KIND_OOP_ARRAY for the bug this fixes. Never
             * manually freed for the same reason a BlockObject's fields
             * aren't: gcAlloc'd memory is only ever reclaimed by a
             * collection once unreachable. */
            oop *args = gcAlloc(GC_KIND_OOP_ARRAY, sizeof(oop) * (size_t)argc);
            for (int i = 0; i < argc; i++) {
                args[i] = eval(node->as.keywordSend.args[i]);
            }
            oop result;
            if (isSuperNode(recvNode)) {
                STClass *start = currentSuperclass();
                if (!start) {
                    fprintf(stderr, "error: 'super' used outside a method\n");
                    return nilObject;
                }
                result = dispatchFrom(start, currentActivation->self, node->as.keywordSend.selector, args,
                                       argc);
            } else {
                oop receiver = eval(recvNode);
                result = sendMessage(receiver, node->as.keywordSend.selector, args, argc);
            }
            return result;
        }
        case AST_CASCADE: {
            /* Limitation: if the cascade's receiver expression is literally
             * `super` (e.g. "super foo; bar"), eval(AST_SUPER) below still
             * yields self's value, but every cascaded message is then sent
             * via plain sendMessage() (classOf(self)-based), not super's
             * superclass-starting lookup -- cascading directly off `super`
             * is rare enough that this narrow gap is left as a known
             * simplification rather than threading super-dispatch through
             * decomposeSend()'s cascade rewriting too. */
            oop receiver = eval(node->as.cascade.receiver);
            oop result = receiver;
            for (int i = 0; i < node->as.cascade.messageCount; i++) {
                CascadeMessage *m = &node->as.cascade.messages[i];
                oop *args = NULL;
                if (m->argCount > 0) {
                    /* See the AST_KEYWORD_SEND case above: GC_KIND_OOP_ARRAY, not malloc. */
                    args = gcAlloc(GC_KIND_OOP_ARRAY, sizeof(oop) * (size_t)m->argCount);
                    for (int j = 0; j < m->argCount; j++) {
                        args[j] = eval(m->args[j]);
                    }
                }
                result = sendMessage(receiver, m->selector, args, m->argCount);
            }
            return result;
        }
    }
    return nilObject;
}
