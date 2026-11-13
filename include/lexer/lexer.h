#ifndef LEXER_H
#define LEXER_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef enum TokenType
{
    TOKEN_INT_TYPE,
    TOKEN_VARIABLE_PREFIX,
    TOKEN_IDENTIFIER,
    TOKEN_ASSIGN,
    TOKEN_INTEGER_LITERAL,
    TOKEN_STRING_LITERAL,
    TOKEN_PRINT,
    TOKEN_INPUT,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_COMMA,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_FLOORDIV,
    TOKEN_PERCENT,
    TOKEN_COLON,
    TOKEN_EQ,
    TOKEN_NE,
    TOKEN_LT,
    TOKEN_LE,
    TOKEN_GT,
    TOKEN_GE,
    TOKEN_IF,
    TOKEN_ELIF,
    TOKEN_ELSE,
    TOKEN_WHILE,
    TOKEN_BREAK,
    TOKEN_CONTINUE,
    TOKEN_PASS,
    TOKEN_AND,
    TOKEN_OR,
    TOKEN_NOT,
    TOKEN_TRUE,
    TOKEN_FALSE,
    TOKEN_SEMICOLON,
    TOKEN_EOF,
    TOKEN_INVALID
} TokenType;

typedef struct Token
{
    TokenType type;
    char *value;
    uint32_t line;
    uint32_t column;
    uint32_t start;
    uint32_t end;
} Token;

typedef struct Lexer
{
    const char *source;
    char current_char;
    uint32_t position;
    uint32_t line;
    uint32_t column;
} Lexer;

void lexer_init(Lexer *lexer, const char *source);
Token *lexer_next_token(Lexer *lexer);
void lexer_free_token(Token *token);

#ifdef __cplusplus
}
#endif

#endif
