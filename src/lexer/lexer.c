#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "lexer/lexer.h"

static const struct
{
    const char *text;
    TokenType type;
} keywords[] = {
    {"int", TOKEN_INT_TYPE},
    {"print", TOKEN_PRINT},
    {"input", TOKEN_INPUT},
    {"if", TOKEN_IF},
    {"elif", TOKEN_ELIF},
    {"else", TOKEN_ELSE},
    {"while", TOKEN_WHILE},
    {"break", TOKEN_BREAK},
    {"continue", TOKEN_CONTINUE},
    {"pass", TOKEN_PASS},
    {"and", TOKEN_AND},
    {"or", TOKEN_OR},
    {"not", TOKEN_NOT},
    {"True", TOKEN_TRUE},
    {"False", TOKEN_FALSE},
};

static const struct
{
    const char *text;
    TokenType type;
} operators[] = {
    {"//", TOKEN_FLOORDIV},
    {"==", TOKEN_EQ},
    {"!=", TOKEN_NE},
    {"<=", TOKEN_LE},
    {">=", TOKEN_GE},
};

static void lexer_advance(Lexer *lexer)
{
    if (lexer->current_char == '\n')
    {
        lexer->line++;
        lexer->column = 0;
    }
    else
    {
        lexer->column++;
    }
    lexer->position++;
    lexer->current_char = lexer->source[lexer->position];
}

static void lexer_skip_whitespace(Lexer *lexer)
{
    while (isspace((unsigned char)lexer->current_char) && lexer->current_char != '\n')
    {
        lexer_advance(lexer);
    }
}

static char *lexer_strdup_range(const char *source, size_t start, size_t length)
{
    char *value = (char *)malloc(length + 1);
    if (value == NULL)
        return NULL;
    memcpy(value, source + start, length);
    value[length] = '\0';
    return value;
}

static Token *lexer_create_token(Lexer *lexer, TokenType type)
{
    Token *token = (Token *)malloc(sizeof(Token));
    if (token == NULL)
        return NULL;
    token->type = type;
    token->value = NULL;
    token->line = lexer->line;
    token->column = lexer->column;
    token->start = lexer->position;
    token->end = lexer->position;
    return token;
}

static Token *lexer_get_identifier(Lexer *lexer)
{
    size_t start_position = lexer->position;
    Token *token = lexer_create_token(lexer, TOKEN_IDENTIFIER);
    if (token == NULL)
        return NULL;

    while (isalnum((unsigned char)lexer->current_char) || lexer->current_char == '_')
    {
        lexer_advance(lexer);
    }

    size_t length = lexer->position - start_position;
    token->end = lexer->position;
    token->value = lexer_strdup_range(lexer->source, start_position, length);

    if (token->value != NULL)
    {
        for (size_t i = 0; i < sizeof(keywords) / sizeof(keywords[0]); i++)
        {
            if (strcmp(token->value, keywords[i].text) == 0)
            {
                token->type = keywords[i].type;
                break;
            }
        }
    }

    return token;
}

static Token *lexer_get_number(Lexer *lexer)
{
    size_t start_position = lexer->position;
    Token *token = lexer_create_token(lexer, TOKEN_INTEGER_LITERAL);
    if (token == NULL)
        return NULL;

    while (isdigit((unsigned char)lexer->current_char))
    {
        lexer_advance(lexer);
    }

    size_t length = lexer->position - start_position;
    token->end = lexer->position;
    token->value = lexer_strdup_range(lexer->source, start_position, length);

    return token;
}

static char *lexer_unescape(const char *text, size_t length)
{
    char *value = (char *)malloc(length + 1);
    if (value == NULL)
        return NULL;

    size_t out = 0;
    for (size_t i = 0; i < length; i++)
    {
        char c = text[i];
        if (c == '\\' && i + 1 < length && strchr("\\'\"", text[i + 1]))
        {
            c = text[i + 1];
            i++;
        }
        value[out++] = c;
    }
    value[out] = '\0';
    return value;
}

static Token *lexer_get_string(Lexer *lexer)
{
    char quote = lexer->current_char;
    Token *token = lexer_create_token(lexer, TOKEN_STRING_LITERAL);
    if (token == NULL)
        return NULL;

    lexer_advance(lexer);

    while (lexer->current_char != quote)
    {
        if (lexer->current_char == '\0' || lexer->current_char == '\n')
        {
            token->type = TOKEN_INVALID;
            token->end = lexer->position;
            token->value = lexer_strdup_range(lexer->source, token->start, token->end - token->start);
            return token;
        }

        char next = lexer->source[lexer->position + 1];
        if (lexer->current_char == '\\' && next != '\0' && next != '\n')
            lexer_advance(lexer);

        lexer_advance(lexer);
    }

    lexer_advance(lexer);
    token->end = lexer->position;
    token->value = lexer_unescape(lexer->source + token->start + 1, token->end - token->start - 2);

    return token;
}

void lexer_init(Lexer *lexer, const char *source)
{
    lexer->source = source;
    lexer->current_char = source[0];
    lexer->position = 0;
    lexer->column = 0;
    lexer->line = 1;
}

Token *lexer_next_token(Lexer *lexer)
{
    while (lexer->current_char != '\0')
    {
        if (isspace((unsigned char)lexer->current_char) && lexer->current_char != '\n')
        {
            lexer_skip_whitespace(lexer);
            continue;
        }

        if (lexer->current_char == '#')
        {
            while (lexer->current_char != '\n' && lexer->current_char != '\0')
                lexer_advance(lexer);
            continue;
        }

        if (lexer->current_char == '"' || lexer->current_char == '\'')
        {
            return lexer_get_string(lexer);
        }

        if (isalpha((unsigned char)lexer->current_char) || lexer->current_char == '_')
        {
            return lexer_get_identifier(lexer);
        }

        if (isdigit((unsigned char)lexer->current_char))
        {
            return lexer_get_number(lexer);
        }

        for (size_t i = 0; i < sizeof(operators) / sizeof(operators[0]); i++)
        {
            if (strncmp(lexer->source + lexer->position, operators[i].text, 2) == 0)
            {
                Token *token = lexer_create_token(lexer, operators[i].type);
                if (token == NULL)
                    return NULL;
                lexer_advance(lexer);
                lexer_advance(lexer);
                token->end = lexer->position;
                token->value = lexer_strdup_range(lexer->source, token->start, 2);
                return token;
            }
        }

            if (lexer->current_char == '=')
        {
            Token *token = lexer_create_token(lexer, TOKEN_ASSIGN);
            if (token == NULL)
                return NULL;
            lexer_advance(lexer);
            token->end = lexer->position;
            token->value = lexer_strdup_range(lexer->source, token->start, 1);
            return token;
        }

        TokenType type;
        switch (lexer->current_char)
        {
        case '\n':
            type = TOKEN_SEMICOLON;
            break;
        case '(':
            type = TOKEN_LPAREN;
            break;
        case ')':
            type = TOKEN_RPAREN;
            break;
        case ',':
            type = TOKEN_COMMA;
            break;
        case '+':
            type = TOKEN_PLUS;
            break;
        case '-':
            type = TOKEN_MINUS;
            break;
        case '*':
            type = TOKEN_STAR;
            break;
        case '/':
            type = TOKEN_SLASH;
            break;
        case '%':
            type = TOKEN_PERCENT;
            break;
        case ':':
            type = TOKEN_COLON;
            break;
        case '<':
            type = TOKEN_LT;
            break;
        case '>':
            type = TOKEN_GT;
            break;
        default:
            type = TOKEN_INVALID;
            break;
        }

        Token *token = lexer_create_token(lexer, type);
        if (token == NULL)
            return NULL;
        lexer_advance(lexer);
        token->end = lexer->position;
        token->value = lexer_strdup_range(lexer->source, token->start, 1);
        return token;
    }

    return lexer_create_token(lexer, TOKEN_EOF);
}

void lexer_free_token(Token *token)
{
    if (token == NULL)
        return;
    free(token->value);
    free(token);
}