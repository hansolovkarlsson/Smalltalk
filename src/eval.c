#include "eval.h"
#include "class.h"

#include <stdio.h>
#include <stdlib.h>

oop sendMessage(oop receiver, const char *selector, oop *args, int argc) {
    STClass *cls = classOf(receiver);
    PrimitiveFn fn = lookupMethod(cls, selector);
    if (!fn) {
        fprintf(stderr, "error: %s does not understand #%s\n", cls->name, selector);
        return nilObject;
    }
    return fn(receiver, args, argc);
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
        case AST_UNARY_SEND: {
            oop receiver = eval(node->as.unarySend.receiver);
            return sendMessage(receiver, node->as.unarySend.selector, NULL, 0);
        }
        case AST_BINARY_SEND: {
            oop receiver = eval(node->as.binarySend.receiver);
            oop arg = eval(node->as.binarySend.arg);
            oop args[1];
            args[0] = arg;
            return sendMessage(receiver, node->as.binarySend.selector, args, 1);
        }
        case AST_KEYWORD_SEND: {
            oop receiver = eval(node->as.keywordSend.receiver);
            int argc = node->as.keywordSend.argCount;
            oop *args = malloc(sizeof(oop) * (size_t)argc);
            for (int i = 0; i < argc; i++) {
                args[i] = eval(node->as.keywordSend.args[i]);
            }
            oop result = sendMessage(receiver, node->as.keywordSend.selector, args, argc);
            free(args);
            return result;
        }
    }
    return nilObject;
}
