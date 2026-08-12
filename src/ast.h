#ifndef AST_H
#define AST_H

typedef enum {
    AST_INT_LITERAL,
    AST_NIL_LITERAL,
    AST_TRUE_LITERAL,
    AST_FALSE_LITERAL,
    AST_UNARY_SEND,
    AST_BINARY_SEND,
    AST_KEYWORD_SEND
} AstNodeType;

typedef struct AstNode {
    AstNodeType type;
    union {
        long intValue;
        struct {
            struct AstNode *receiver;
            const char *selector;
        } unarySend;
        struct {
            struct AstNode *receiver;
            const char *selector;
            struct AstNode *arg;
        } binarySend;
        struct {
            struct AstNode *receiver;
            const char *selector; /* combined, e.g. "at:put:" */
            struct AstNode **args;
            int argCount;
        } keywordSend;
    } as;
} AstNode;

#endif
