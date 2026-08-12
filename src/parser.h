#ifndef PARSER_H
#define PARSER_H

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

#endif
