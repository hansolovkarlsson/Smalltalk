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
    /* strchr(set, '\0') always "matches" (the null terminator is part of
     * the search string per the standard), so '\0' must be excluded
     * explicitly or a scan can run straight past end-of-string. */
    return c != '\0' && strchr("+-*/~<>=&|@%,?!\\", c) != NULL;
}

static void skipWhitespace(Lexer *lx) {
    while (lx->src[lx->pos] && isspace((unsigned char)lx->src[lx->pos])) {
        lx->pos++;
    }
}

static void appendChar(char *buf, int *len, int capacity, char c) {
    if (*len < capacity - 1) {
        buf[(*len)++] = c;
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
    if (c == ';') {
        lx->pos++;
        tok.type = TOK_SEMICOLON;
        lx->expectOperand = 1;
        return tok;
    }
    if (c == '^') {
        lx->pos++;
        tok.type = TOK_CARET;
        lx->expectOperand = 1;
        return tok;
    }
    if (c == ':' && lx->src[lx->pos + 1] == '=') {
        lx->pos += 2;
        tok.type = TOK_ASSIGN;
        lx->expectOperand = 1;
        return tok;
    }
    if (c == ':' && (isalpha((unsigned char)lx->src[lx->pos + 1]) || lx->src[lx->pos + 1] == '_')) {
        lx->pos++; /* consume ':', leaving the parameter name to scan below */
        int start = lx->pos;
        while (isalnum((unsigned char)lx->src[lx->pos]) || lx->src[lx->pos] == '_') {
            lx->pos++;
        }
        int len = lx->pos - start;
        if (len >= (int)sizeof(tok.text)) len = (int)sizeof(tok.text) - 1;
        memcpy(tok.text, lx->src + start, len);
        tok.text[len] = '\0';
        tok.type = TOK_BLOCK_PARAM;
        lx->expectOperand = 0;
        return tok;
    }
    if (c == '[') {
        lx->pos++;
        tok.type = TOK_LBRACKET;
        lx->expectOperand = 1;
        return tok;
    }
    if (c == ']') {
        lx->pos++;
        tok.type = TOK_RBRACKET;
        lx->expectOperand = 0;
        return tok;
    }

    if (c == '\'') {
        lx->pos++; /* consume opening quote */
        int len = 0;
        while (1) {
            char ch = lx->src[lx->pos];
            if (ch == '\0') {
                tok.type = TOK_ERROR;
                strncpy(tok.text, "unterminated string literal", sizeof(tok.text) - 1);
                return tok;
            }
            if (ch == '\'') {
                if (lx->src[lx->pos + 1] == '\'') {
                    appendChar(tok.text, &len, (int)sizeof(tok.text), '\'');
                    lx->pos += 2;
                    continue;
                }
                lx->pos++; /* consume closing quote */
                break;
            }
            appendChar(tok.text, &len, (int)sizeof(tok.text), ch);
            lx->pos++;
        }
        tok.text[len] = '\0';
        tok.type = TOK_STRING;
        lx->expectOperand = 0;
        return tok;
    }

    if (c == '#') {
        lx->pos++;
        int start = lx->pos;
        if (isalpha((unsigned char)lx->src[lx->pos]) || lx->src[lx->pos] == '_') {
            while (isalnum((unsigned char)lx->src[lx->pos]) || lx->src[lx->pos] == '_') {
                lx->pos++;
            }
            /* Each keyword segment's ':' belongs to that segment, whether
             * or not another segment follows -- "at:" ends in its own
             * colon just like the first part of "at:put:" does. */
            while (lx->src[lx->pos] == ':') {
                lx->pos++;
                if (isalpha((unsigned char)lx->src[lx->pos]) || lx->src[lx->pos] == '_') {
                    while (isalnum((unsigned char)lx->src[lx->pos]) || lx->src[lx->pos] == '_') {
                        lx->pos++;
                    }
                } else {
                    break;
                }
            }
        } else if (isBinaryChar(lx->src[lx->pos])) {
            while (isBinaryChar(lx->src[lx->pos])) lx->pos++;
        } else {
            tok.type = TOK_ERROR;
            strncpy(tok.text, "expected symbol name after '#'", sizeof(tok.text) - 1);
            return tok;
        }
        int len = lx->pos - start;
        if (len >= (int)sizeof(tok.text)) len = (int)sizeof(tok.text) - 1;
        memcpy(tok.text, lx->src + start, len);
        tok.text[len] = '\0';
        tok.type = TOK_SYMBOL;
        lx->expectOperand = 0;
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
        /* ':' starts a keyword part only if it's not actually ":=" */
        int isKeyword = (lx->src[lx->pos] == ':' && lx->src[lx->pos + 1] != '=');
        if (isKeyword) lx->pos++;
        int textLen = lx->pos - start;
        if (textLen >= (int)sizeof(tok.text)) textLen = (int)sizeof(tok.text) - 1;
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
        if (len >= (int)sizeof(tok.text)) len = (int)sizeof(tok.text) - 1;
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
