#ifndef AST_H
#define AST_H

typedef enum {
    AST_INT_LITERAL,
    AST_NIL_LITERAL,
    AST_TRUE_LITERAL,
    AST_FALSE_LITERAL,
    AST_STRING_LITERAL,
    AST_SYMBOL_LITERAL,
    AST_VARIABLE_REF,
    AST_ASSIGNMENT,
    AST_UNARY_SEND,
    AST_BINARY_SEND,
    AST_KEYWORD_SEND,
    AST_CASCADE
} AstNodeType;

/* One ';'-separated message pattern in a cascade, e.g. the "add: 2" part
 * of `OrderedCollection new add: 1; add: 2`. */
typedef struct CascadeMessage {
    const char *selector;
    struct AstNode **args;
    int argCount;
} CascadeMessage;

typedef struct AstNode {
    AstNodeType type;
    union {
        long intValue;
        const char *stringValue;  /* AST_STRING_LITERAL, owned copy */
        const char *symbolName;   /* AST_SYMBOL_LITERAL, interned */
        const char *variableName; /* AST_VARIABLE_REF, interned */
        struct {
            const char *name; /* interned */
            struct AstNode *value;
        } assignment;
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
        struct {
            struct AstNode *receiver; /* evaluated once, shared by all messages */
            CascadeMessage *messages;
            int messageCount;
        } cascade;
    } as;
} AstNode;

#endif
