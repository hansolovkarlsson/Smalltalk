#include "parser.h"
#include "symbol.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static AstNode *parseKeywordSend(Parser *p);

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

static char *dupCString(const char *s) {
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    memcpy(copy, s, len + 1);
    return copy;
}

static AstNode *parsePrimary(Parser *p) {
    if (p->current.type == TOK_INT) {
        AstNode *n = newNode(AST_INT_LITERAL);
        n->as.intValue = p->current.intValue;
        advance(p);
        return n;
    }

    if (p->current.type == TOK_STRING) {
        AstNode *n = newNode(AST_STRING_LITERAL);
        n->as.stringValue = dupCString(p->current.text);
        advance(p);
        return n;
    }

    if (p->current.type == TOK_SYMBOL) {
        AstNode *n = newNode(AST_SYMBOL_LITERAL);
        n->as.symbolName = intern(p->current.text);
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
        AstNode *n = newNode(AST_VARIABLE_REF);
        n->as.variableName = intern(p->current.text);
        advance(p);
        return n;
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

static AstNode *parseKeywordSend(Parser *p) {
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

/* Splits a just-parsed send node into its receiver and the message it
 * sent, so a cascade can resend further messages to that same receiver.
 * Returns 0 (receiver = whole node, no initial message) if node isn't a
 * message send at all, e.g. a bare literal. */
static int decomposeSend(AstNode *node, AstNode **outReceiver, CascadeMessage *outMsg) {
    if (node->type == AST_UNARY_SEND) {
        *outReceiver = node->as.unarySend.receiver;
        outMsg->selector = node->as.unarySend.selector;
        outMsg->args = NULL;
        outMsg->argCount = 0;
        return 1;
    }
    if (node->type == AST_BINARY_SEND) {
        *outReceiver = node->as.binarySend.receiver;
        outMsg->selector = node->as.binarySend.selector;
        outMsg->args = malloc(sizeof(AstNode *));
        outMsg->args[0] = node->as.binarySend.arg;
        outMsg->argCount = 1;
        return 1;
    }
    if (node->type == AST_KEYWORD_SEND) {
        *outReceiver = node->as.keywordSend.receiver;
        outMsg->selector = node->as.keywordSend.selector;
        outMsg->args = node->as.keywordSend.args;
        outMsg->argCount = node->as.keywordSend.argCount;
        return 1;
    }
    return 0;
}

static CascadeMessage parseCascadeMessage(Parser *p) {
    CascadeMessage msg;
    msg.selector = NULL;
    msg.args = NULL;
    msg.argCount = 0;

    if (p->current.type == TOK_IDENTIFIER) {
        msg.selector = intern(p->current.text);
        advance(p);
        return msg;
    }
    if (p->current.type == TOK_BINARY) {
        const char *selector = intern(p->current.text);
        advance(p);
        AstNode *arg = parseUnary(p);
        if (!arg) return msg; /* selector left NULL signals error */
        msg.selector = selector;
        msg.args = malloc(sizeof(AstNode *));
        msg.args[0] = arg;
        msg.argCount = 1;
        return msg;
    }
    if (p->current.type == TOK_KEYWORD) {
        char combined[256];
        combined[0] = '\0';
        AstNode **args = NULL;
        int argCount = 0, argCapacity = 0;
        while (p->current.type == TOK_KEYWORD) {
            strncat(combined, p->current.text, sizeof(combined) - strlen(combined) - 1);
            advance(p);
            AstNode *arg = parseBinary(p);
            if (!arg) return msg;
            if (argCount == argCapacity) {
                argCapacity = argCapacity ? argCapacity * 2 : 4;
                args = realloc(args, sizeof(AstNode *) * argCapacity);
            }
            args[argCount++] = arg;
        }
        msg.selector = intern(combined);
        msg.args = args;
        msg.argCount = argCount;
        return msg;
    }

    setError(p, "expected a message after ';'");
    return msg;
}

static AstNode *parseCascade(Parser *p) {
    AstNode *expr = parseKeywordSend(p);
    if (!expr) return NULL;
    if (p->current.type != TOK_SEMICOLON) {
        return expr;
    }

    AstNode *receiver;
    CascadeMessage firstMsg;
    int hasFirst = decomposeSend(expr, &receiver, &firstMsg);
    if (!hasFirst) {
        receiver = expr;
    }

    CascadeMessage *messages = NULL;
    int count = 0, capacity = 0;
    if (hasFirst) {
        capacity = 4;
        messages = malloc(sizeof(CascadeMessage) * capacity);
        messages[count++] = firstMsg;
    }

    while (p->current.type == TOK_SEMICOLON) {
        advance(p);
        CascadeMessage m = parseCascadeMessage(p);
        if (!m.selector) return NULL;
        if (count == capacity) {
            capacity = capacity ? capacity * 2 : 4;
            messages = realloc(messages, sizeof(CascadeMessage) * capacity);
        }
        messages[count++] = m;
    }

    AstNode *n = newNode(AST_CASCADE);
    n->as.cascade.receiver = receiver;
    n->as.cascade.messages = messages;
    n->as.cascade.messageCount = count;
    return n;
}

AstNode *parseExpression(Parser *p) {
    if (p->current.type == TOK_IDENTIFIER && strcmp(p->current.text, "nil") != 0 &&
        strcmp(p->current.text, "true") != 0 && strcmp(p->current.text, "false") != 0) {
        Parser saved = *p;
        const char *name = intern(p->current.text);
        advance(p);
        if (p->current.type == TOK_ASSIGN) {
            advance(p);
            AstNode *value = parseExpression(p); /* right-associative: x := y := 3 */
            if (!value) return NULL;
            AstNode *n = newNode(AST_ASSIGNMENT);
            n->as.assignment.name = name;
            n->as.assignment.value = value;
            return n;
        }
        *p = saved; /* not an assignment; re-parse normally */
    }

    return parseCascade(p);
}
