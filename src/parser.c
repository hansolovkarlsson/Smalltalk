#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void advance(Parser *p) {
    p->current = lexerNext(&p->lexer);
}

void parserInit(Parser *p, const char *src) {
    lexerInit(&p->lexer, src);
    p->hasError = 0;
    p->errorMsg[0] = '\0';
    advance(p);
}

static void setError(Parser *p, const char *msg) {
    if (!p->hasError) {
        strncpy(p->errorMsg, msg, sizeof(p->errorMsg) - 1);
        p->errorMsg[sizeof(p->errorMsg) - 1] = '\0';
        p->hasError = 1;
    }
}

static AstNode *newNode(AstNodeType type) {
    AstNode *n = calloc(1, sizeof(AstNode));
    n->type = type;
    return n;
}

static AstNode *parsePrimary(Parser *p) {
    if (p->current.type == TOK_INT) {
        AstNode *n = newNode(AST_INT_LITERAL);
        n->as.intValue = p->current.intValue;
        advance(p);
        return n;
    }

    if (p->current.type == TOK_LPAREN) {
        advance(p);
        AstNode *inner = parseExpression(p);
        if (!inner) return NULL;
        if (p->current.type != TOK_RPAREN) {
            setError(p, "expected ')'");
            return NULL;
        }
        advance(p);
        return inner;
    }

    if (p->current.type == TOK_IDENTIFIER) {
        if (strcmp(p->current.text, "nil") == 0) {
            advance(p);
            return newNode(AST_NIL_LITERAL);
        }
        if (strcmp(p->current.text, "true") == 0) {
            advance(p);
            return newNode(AST_TRUE_LITERAL);
        }
        if (strcmp(p->current.text, "false") == 0) {
            advance(p);
            return newNode(AST_FALSE_LITERAL);
        }
        char msg[128];
        snprintf(msg, sizeof(msg), "undefined variable '%s'", p->current.text);
        setError(p, msg);
        return NULL;
    }

    setError(p, "expected an expression");
    return NULL;
}

static AstNode *parseUnary(Parser *p) {
    AstNode *recv = parsePrimary(p);
    if (!recv) return NULL;
    while (p->current.type == TOK_IDENTIFIER) {
        const char *selector = intern(p->current.text);
        advance(p);
        AstNode *n = newNode(AST_UNARY_SEND);
        n->as.unarySend.receiver = recv;
        n->as.unarySend.selector = selector;
        recv = n;
    }
    return recv;
}

static AstNode *parseBinary(Parser *p) {
    AstNode *left = parseUnary(p);
    if (!left) return NULL;
    while (p->current.type == TOK_BINARY) {
        const char *selector = intern(p->current.text);
        advance(p);
        AstNode *right = parseUnary(p);
        if (!right) return NULL;
        AstNode *n = newNode(AST_BINARY_SEND);
        n->as.binarySend.receiver = left;
        n->as.binarySend.selector = selector;
        n->as.binarySend.arg = right;
        left = n;
    }
    return left;
}

AstNode *parseExpression(Parser *p) {
    AstNode *recv = parseBinary(p);
    if (!recv) return NULL;
    if (p->current.type != TOK_KEYWORD) {
        return recv;
    }

    char combined[256];
    combined[0] = '\0';
    AstNode **args = NULL;
    int argCount = 0;
    int argCapacity = 0;

    while (p->current.type == TOK_KEYWORD) {
        strncat(combined, p->current.text, sizeof(combined) - strlen(combined) - 1);
        advance(p);
        AstNode *arg = parseBinary(p);
        if (!arg) return NULL;
        if (argCount == argCapacity) {
            argCapacity = argCapacity ? argCapacity * 2 : 4;
            args = realloc(args, sizeof(AstNode *) * argCapacity);
        }
        args[argCount++] = arg;
    }

    AstNode *n = newNode(AST_KEYWORD_SEND);
    n->as.keywordSend.receiver = recv;
    n->as.keywordSend.selector = intern(combined);
    n->as.keywordSend.args = args;
    n->as.keywordSend.argCount = argCount;
    return n;
}
