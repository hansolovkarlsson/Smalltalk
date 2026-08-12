#include "lexer.h"

#include <ctype.h>
#include <stdlib.h>
#include <string.h>

void lexerInit(Lexer *lx, const char *src) {
    lx->src = src;
    lx->pos = 0;
    lx->expectOperand = 1;
}

static int isBinaryChar(char c) {
    return strchr("+-*/~<>=&|@%,?!\\", c) != NULL;
}

static void skipWhitespace(Lexer *lx) {
    while (lx->src[lx->pos] && isspace((unsigned char)lx->src[lx->pos])) {
        lx->pos++;
    }
}

Token lexerNext(Lexer *lx) {
    skipWhitespace(lx);

    Token tok;
    memset(&tok, 0, sizeof(tok));
    char c = lx->src[lx->pos];

    if (c == '\0') {
        tok.type = TOK_EOF;
        return tok;
    }

    if (c == '(') {
        lx->pos++;
        tok.type = TOK_LPAREN;
        lx->expectOperand = 1;
        return tok;
    }
    if (c == ')') {
        lx->pos++;
        tok.type = TOK_RPAREN;
        lx->expectOperand = 0;
        return tok;
    }
    if (c == '.') {
        lx->pos++;
        tok.type = TOK_DOT;
        lx->expectOperand = 1;
        return tok;
    }

    int negativeLiteral = (c == '-' && lx->expectOperand &&
                            isdigit((unsigned char)lx->src[lx->pos + 1]));
    if (isdigit((unsigned char)c) || negativeLiteral) {
        int start = lx->pos;
        if (negativeLiteral) lx->pos++;
        while (isdigit((unsigned char)lx->src[lx->pos])) lx->pos++;
        int len = lx->pos - start;
        char buf[32];
        if (len >= (int)sizeof(buf)) len = sizeof(buf) - 1;
        memcpy(buf, lx->src + start, len);
        buf[len] = '\0';
        tok.type = TOK_INT;
        tok.intValue = atol(buf);
        lx->expectOperand = 0;
        return tok;
    }

    if (isalpha((unsigned char)c) || c == '_') {
        int start = lx->pos;
        while (isalnum((unsigned char)lx->src[lx->pos]) || lx->src[lx->pos] == '_') {
            lx->pos++;
        }
        int isKeyword = (lx->src[lx->pos] == ':');
        if (isKeyword) lx->pos++;
        int textLen = lx->pos - start;
        if (textLen >= (int)sizeof(tok.text)) textLen = sizeof(tok.text) - 1;
        memcpy(tok.text, lx->src + start, textLen);
        tok.text[textLen] = '\0';
        tok.type = isKeyword ? TOK_KEYWORD : TOK_IDENTIFIER;
        lx->expectOperand = isKeyword ? 1 : 0;
        return tok;
    }

    if (isBinaryChar(c)) {
        int start = lx->pos;
        while (isBinaryChar(lx->src[lx->pos])) lx->pos++;
        int len = lx->pos - start;
        if (len >= (int)sizeof(tok.text)) len = sizeof(tok.text) - 1;
        memcpy(tok.text, lx->src + start, len);
        tok.text[len] = '\0';
        tok.type = TOK_BINARY;
        lx->expectOperand = 1;
        return tok;
    }

    lx->pos++;
    tok.type = TOK_ERROR;
    tok.text[0] = c;
    tok.text[1] = '\0';
    return tok;
}
