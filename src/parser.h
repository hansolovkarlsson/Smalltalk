#ifndef PARSER_H
#define PARSER_H

#include <stddef.h>

#include "ast.h"
#include "lexer.h"

typedef struct {
    Lexer lexer;
    Token current;
    char errorMsg[128];
    int hasError;
} Parser;

void parserInit(Parser *p, const char *src);

/* Parses one full expression (unary/binary/keyword sends with correct
 * precedence). Returns NULL and sets p->errorMsg on a parse error. */
AstNode *parseExpression(Parser *p);

/* Parses a whole method source string as passed to Class>>compile: --
 * pattern, optional "| temp1 temp2 |" declarations, and a '.'-separated
 * statement sequence where any statement may be "^expr" to return early.
 * Returns NULL and writes a message into errorMsg on failure. */
MethodNode *parseMethod(const char *src, char *errorMsg, size_t errorMsgSize);

#endif
