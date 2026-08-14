#include "eval.h"
#include "activation.h"
#include "block.h"
#include "class.h"
#include "compiler.h"
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
 * existed) would dangle in exactly that case. gc.c traces one via its own
 * copy of this struct's layout (activation.h) -- see gcMarkActivation()
 * there, and markRoots()'s use of evalCurrentActivation() below for how
 * the in-progress call chain becomes a root without scanning the C stack
 * for it specifically.
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
 * (see vmRun()'s OP_RETURN): the nearest lexically enclosing METHOD
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

/* Generous headroom for how deep a single expression's evaluation can
 * nest sends/cascade args before spilling values onto this stack -- no
 * realistic Smalltalk source (including every method/block this project
 * has ever compiled) comes remotely close. A bounds check keeps a
 * pathological case a graceful stderr-and-nil error (this codebase's
 * usual convention) instead of silently corrupting memory past the array. */
#define VM_STACK_MAX 1024

/* The dispatch loop: fetches one Instr at a time from a CompiledCode
 * (bytecode.h) and executes it against an explicit operand stack, in
 * place of what used to be eval()'s recursive AST walk. `stack` is a
 * plain C local array, which is what makes it automatically visible to
 * gc.c's conservative stack scanning for as long as this call (and any
 * nested vmRun() calls it triggers via OP_SEND) remains on the C call
 * stack -- exactly the same mechanism that already covered eval()'s old
 * `oop receiver = eval(...)`-style locals, no new GC design needed. This
 * is also why message-send arguments no longer need the GC_KIND_OOP_ARRAY
 * treatment gc.c used to require (see CLAUDE.md): OP_SEND's args point
 * directly into this stack-resident array, not a separately heap-malloc'd
 * buffer a conservative scan can't see the contents of. */
static oop vmRun(CompiledCode *code) {
    oop stack[VM_STACK_MAX];
    int sp = 0;

/* Only used at push sites: SEND/SEND_SUPER/POP/STORE_VAR never grow sp
 * (SEND/SEND_SUPER pop more than they push, POP shrinks, STORE_VAR only
 * peeks), so only the handful of opcodes that actually push a new value
 * need the bounds check at all. */
#define VM_PUSH(value)                                                            \
    do {                                                                          \
        if (sp >= VM_STACK_MAX) {                                                 \
            fprintf(stderr, "error: expression too complex (VM stack overflow)\n"); \
            return nilObject;                                                     \
        }                                                                         \
        stack[sp++] = (value);                                                    \
    } while (0)

    for (int pc = 0; pc < code->count; pc++) {
        Instr *ins = &code->instrs[pc];

        switch (ins->op) {
            case OP_PUSH_LITERAL:
                VM_PUSH(ins->operand.literal);
                break;
            case OP_PUSH_STRING_LITERAL:
                VM_PUSH(makeString(ins->operand.stringValue));
                break;
            case OP_PUSH_SYMBOL_LITERAL:
                VM_PUSH(internSymbol(ins->operand.symbolName));
                break;
            case OP_PUSH_NIL:
                VM_PUSH(nilObject);
                break;
            case OP_PUSH_TRUE:
                VM_PUSH(trueObject);
                break;
            case OP_PUSH_FALSE:
                VM_PUSH(falseObject);
                break;
            case OP_PUSH_SELF:
                if (currentActivation) {
                    VM_PUSH(currentActivation->self);
                } else {
                    fprintf(stderr, "error: 'self' used outside a method\n");
                    VM_PUSH(nilObject);
                }
                break;
            case OP_PUSH_VAR: {
                oop value;
                if (activationLookup(ins->operand.varName, &value)) {
                    VM_PUSH(value);
                } else if (envLookup(ins->operand.varName, &value)) {
                    VM_PUSH(value);
                } else {
                    fprintf(stderr, "error: undefined variable '%s'\n", ins->operand.varName);
                    VM_PUSH(nilObject);
                }
                break;
            }
            case OP_STORE_VAR: {
                oop value = stack[sp - 1]; /* peek: assignment is an expression, its value stays on TOS */
                if (!activationStore(ins->operand.varName, value)) {
                    envSet(ins->operand.varName, value);
                }
                break;
            }
            case OP_PUSH_BLOCK: {
                BlockTemplate *tmpl = ins->operand.block;
                BlockObject *blk = gcAlloc(GC_KIND_OOP, sizeof(BlockObject));
                blk->isa = BlockClass;
                blk->paramNames = tmpl->paramNames;
                blk->paramCount = tmpl->paramCount;
                blk->code = tmpl->code;
                blk->homeActivation = currentActivation; /* the entire closure mechanism */
                VM_PUSH((oop)blk);
                break;
            }
            case OP_SEND: {
                int argc = ins->operand.send.argc;
                oop *args = &stack[sp - argc];
                oop receiver = stack[sp - argc - 1];
                oop result = sendMessage(receiver, ins->operand.send.selector, args, argc);
                sp -= (argc + 1);
                stack[sp++] = result;
                break;
            }
            case OP_SEND_SUPER: {
                int argc = ins->operand.send.argc;
                oop *args = &stack[sp - argc];
                STClass *start = currentSuperclass();
                oop result;
                if (!start) {
                    fprintf(stderr, "error: 'super' used outside a method\n");
                    result = nilObject;
                } else {
                    result = dispatchFrom(start, currentActivation->self, ins->operand.send.selector, args,
                                           argc);
                }
                sp -= argc;
                stack[sp++] = result;
                break;
            }
            case OP_POP:
                sp--;
                break;
            case OP_DUP: {
                /* Not VM_PUSH(stack[sp - 1]) directly: that macro-expands
                 * to stack[sp++] = (stack[sp - 1]), reading and modifying
                 * sp with no sequence point between them -- undefined
                 * behavior, caught by the compiler. */
                oop top = stack[sp - 1];
                VM_PUSH(top);
                break;
            }
            case OP_RETURN: {
                oop value = stack[sp - 1];
                Activation *home = currentActivation ? currentActivation->homeMethodActivation : NULL;
                if (!home) {
                    fprintf(stderr, "error: '^' used outside a method\n");
                    return value;
                }
                home->returnValue = value;
                longjmp(home->returnPoint, 1);
            }
        }
    }

    return sp > 0 ? stack[sp - 1] : nilObject;

#undef VM_PUSH
}

oop eval(AstNode *node) {
    CompiledCode *code = compileTopLevelExpression(node);
    return vmRun(code);
}

/* Args arrive positionally from the send site (OP_SEND/OP_SEND_SUPER
 * above), so argc there always matches cm->argCount: the selector's
 * arity is baked into its shape (unary/binary/N-keyword) by construction,
 * and lookupMethod() matched on that same selector. Copies args/temps
 * into the activation's own storage rather than aliasing the caller's
 * buffer -- the caller's buffer is a slice of *its own* vmRun() operand
 * stack, which stays perfectly valid for the duration of this call (it's
 * still on the C stack), but a block created in this method body that
 * captures this activation can outlive that caller entirely. */
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
        result = vmRun(cm->code);
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

    oop result = vmRun(blk->code);

    currentActivation = frame->caller;
    return result;
}
