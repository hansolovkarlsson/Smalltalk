#include "eval.h"
#include "class.h"
#include "environment.h"
#include "stringobj.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>

/* One method activation: the receiver bound to "self", the argument and
 * temp bindings for a compiled method's current call, and a link to the
 * caller's activation (the C call stack already gives us real recursion;
 * this chain exists so "super" can find the class the running method was
 * defined in, not classOf(self)). There is no lexical scoping to speak of
 * yet -- args/temps/instance vars/self are the only non-global names. */
typedef struct Activation {
    oop self;
    STClass *homeClass; /* class the running method was compiled into */
    char **argNames;
    oop *argValues;
    int argCount;
    char **tempNames;
    oop *tempValues;
    int tempCount;
    struct Activation *caller;
} Activation;

static Activation *currentActivation = NULL;

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

/* Args arrive positionally from the send site (AST_*_SEND below), so argc
 * there always matches cm->argCount: the selector's arity is baked into
 * its shape (unary/binary/N-keyword) by construction, and lookupMethod()
 * matched on that same selector. */
static oop invokeCompiledMethod(CompiledMethod *cm, oop receiver, oop *args) {
    Activation act;
    act.self = receiver;
    act.homeClass = cm->homeClass;
    act.argNames = cm->argNames;
    act.argValues = args;
    act.argCount = cm->argCount;
    act.tempNames = cm->tempNames;
    act.tempCount = cm->tempCount;
    act.tempValues = NULL;
    if (act.tempCount > 0) {
        act.tempValues = malloc(sizeof(oop) * (size_t)act.tempCount);
        for (int i = 0; i < act.tempCount; i++) act.tempValues[i] = nilObject;
    }
    act.caller = currentActivation;
    currentActivation = &act;

    oop result = receiver; /* methods with no explicit '^' return self */
    for (int i = 0; i < cm->statementCount; i++) {
        AstNode *stmt = cm->statements[i];
        if (stmt->type == AST_RETURN) {
            result = eval(stmt->as.returnValue);
            break;
        }
        eval(stmt);
    }

    currentActivation = act.caller;
    free(act.tempValues);
    return result;
}

static int isSuperNode(AstNode *node) {
    return node->type == AST_SUPER;
}

/* Looks up name in the current method activation (args, then temps, then
 * self's instance variables), the only non-global scopes that exist.
 * Returns 1 and sets *outValue on a hit. */
static int activationLookup(const char *name, oop *outValue) {
    if (!currentActivation) return 0;
    for (int i = 0; i < currentActivation->argCount; i++) {
        if (currentActivation->argNames[i] == name) {
            *outValue = currentActivation->argValues[i];
            return 1;
        }
    }
    for (int i = 0; i < currentActivation->tempCount; i++) {
        if (currentActivation->tempNames[i] == name) {
            *outValue = currentActivation->tempValues[i];
            return 1;
        }
    }
    STClass *selfClass = classOf(currentActivation->self);
    for (int i = 0; i < selfClass->instanceVarCount; i++) {
        if (selfClass->instanceVarNames[i] == name) {
            *outValue = ((Object *)currentActivation->self)->fields[i];
            return 1;
        }
    }
    return 0;
}

/* Mirrors activationLookup(), but stores. Returns 1 on a hit. */
static int activationStore(const char *name, oop value) {
    if (!currentActivation) return 0;
    for (int i = 0; i < currentActivation->argCount; i++) {
        if (currentActivation->argNames[i] == name) {
            currentActivation->argValues[i] = value;
            return 1;
        }
    }
    for (int i = 0; i < currentActivation->tempCount; i++) {
        if (currentActivation->tempNames[i] == name) {
            currentActivation->tempValues[i] = value;
            return 1;
        }
    }
    STClass *selfClass = classOf(currentActivation->self);
    for (int i = 0; i < selfClass->instanceVarCount; i++) {
        if (selfClass->instanceVarNames[i] == name) {
            ((Object *)currentActivation->self)->fields[i] = value;
            return 1;
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
            /* Only meaningful as a top-level method statement, handled
             * directly by invokeCompiledMethodWithArgs(); reached here
             * only if '^' appeared somewhere nested, which the method
             * parser never produces. Fall back to just evaluating it. */
            return eval(node->as.returnValue);
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
                if (!currentActivation) {
                    fprintf(stderr, "error: 'super' used outside a method\n");
                    return nilObject;
                }
                return dispatchFrom(currentActivation->homeClass->superclass, currentActivation->self,
                                     node->as.unarySend.selector, NULL, 0);
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
                if (!currentActivation) {
                    fprintf(stderr, "error: 'super' used outside a method\n");
                    return nilObject;
                }
                return dispatchFrom(currentActivation->homeClass->superclass, currentActivation->self,
                                     node->as.binarySend.selector, args, 1);
            }
            oop receiver = eval(recvNode);
            return sendMessage(receiver, node->as.binarySend.selector, args, 1);
        }
        case AST_KEYWORD_SEND: {
            AstNode *recvNode = node->as.keywordSend.receiver;
            int argc = node->as.keywordSend.argCount;
            oop *args = malloc(sizeof(oop) * (size_t)argc);
            for (int i = 0; i < argc; i++) {
                args[i] = eval(node->as.keywordSend.args[i]);
            }
            oop result;
            if (isSuperNode(recvNode)) {
                if (!currentActivation) {
                    fprintf(stderr, "error: 'super' used outside a method\n");
                    free(args);
                    return nilObject;
                }
                result = dispatchFrom(currentActivation->homeClass->superclass, currentActivation->self,
                                       node->as.keywordSend.selector, args, argc);
            } else {
                oop receiver = eval(recvNode);
                result = sendMessage(receiver, node->as.keywordSend.selector, args, argc);
            }
            free(args);
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
                    args = malloc(sizeof(oop) * (size_t)m->argCount);
                    for (int j = 0; j < m->argCount; j++) {
                        args[j] = eval(m->args[j]);
                    }
                }
                result = sendMessage(receiver, m->selector, args, m->argCount);
                free(args);
            }
            return result;
        }
    }
    return nilObject;
}
