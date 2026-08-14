#ifndef LEXER_H
#define LEXER_H

typedef enum {
    TOK_INT,
    TOK_IDENTIFIER,
    TOK_KEYWORD,   /* identifier immediately followed by ':', e.g. "at:" */
    TOK_BINARY,    /* one or more binary-selector characters, e.g. "<=" */
    TOK_STRING,    /* 'quoted', with '' as an escaped literal quote */
    TOK_SYMBOL,    /* #foo, #at:put:, or #+ */
    TOK_ASSIGN,    /* := */
    TOK_SEMICOLON, /* ; (cascade separator) */
    TOK_CARET,     /* ^ (method return, only meaningful in method bodies) */
    TOK_LPAREN,
    TOK_RPAREN,
    TOK_DOT,
    TOK_EOF,
    TOK_ERROR
} TokenType;

typedef struct {
    TokenType type;
    long intValue;
    char text[256];
} Token;

typedef struct {
    const char *src;
    int pos;
    int expectOperand; /* disambiguates leading '-' as negative literal vs binary selector */
} Lexer;

void lexerInit(Lexer *lx, const char *src);
Token lexerNext(Lexer *lx);

#endif
