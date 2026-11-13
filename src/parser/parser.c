








#include "parser/parser.h"
#include "lexer/lexer.h"
#include <stdio.h>
#include <stdlib.h>


static void error_word(const Token *token)
{
    printf("\033[31mError on line %u: '%s' is not a valid word.\033[0m\n", token->line, token->value);
}

static void error_expected(const Token *token, const char *what)
{
    printf("\033[31mError on line %u: expected %s.\033[0m\n", token->line, what);
}

static int is_value(const Token *token)
{
    return token->type == TOKEN_INTEGER_LITERAL
        || token->type == TOKEN_STRING_LITERAL
        || token->type == TOKEN_IDENTIFIER
        || token->type == TOKEN_TRUE
        || token->type == TOKEN_FALSE;
}

static int is_operator(TokenType type, int level)
{
    switch (level) {
    case 0:
        return type == TOKEN_OR;
    case 1:
        return type == TOKEN_AND;
    case 3:
        return type == TOKEN_EQ || type == TOKEN_NE || type == TOKEN_LT
            || type == TOKEN_LE || type == TOKEN_GT || type == TOKEN_GE;
    case 4:
        return type == TOKEN_PLUS || type == TOKEN_MINUS;
    case 5:
        return type == TOKEN_STAR || type == TOKEN_SLASH
            || type == TOKEN_FLOORDIV || type == TOKEN_PERCENT;
    }
    return 0;
}

static int is_header(TokenType type)
{
    return type == TOKEN_IF || type == TOKEN_ELIF || type == TOKEN_ELSE || type == TOKEN_WHILE;
}

static int is_bare(TokenType type)
{
    return type == TOKEN_BREAK || type == TOKEN_CONTINUE || type == TOKEN_PASS;
}

static Token *parse_expr(Lexer *lexer, Token *token, int level);

static Token *parse_atom(Lexer *lexer, Token *token)
{
    if (token->type == TOKEN_MINUS) {
        lexer_free_token(token);
        return parse_atom(lexer, lexer_next_token(lexer));
    }

    if (token->type == TOKEN_LPAREN) {
        lexer_free_token(token);
        token = parse_expr(lexer, lexer_next_token(lexer), 0);
        if (token == NULL)
            return NULL;

        if (token->type != TOKEN_RPAREN) {
            error_expected(token, "')'");
            lexer_free_token(token);
            return NULL;
        }
        lexer_free_token(token);
        return lexer_next_token(lexer);
    }

    if (token->type == TOKEN_SEMICOLON || token->type == TOKEN_EOF) {
        error_expected(token, "a value");
        lexer_free_token(token);
        return NULL;
    }

    if (!is_value(token)) {
        error_word(token);
        lexer_free_token(token);
        return NULL;
    }

    /* python recusa 08, e bash/batch leriam como octal */
    if (token->type == TOKEN_INTEGER_LITERAL && token->value[0] == '0' && token->value[1] != '\0') {
        printf("\033[31mError on line %u: leading zeros are not allowed in '%s'.\033[0m\n",
               token->line, token->value);
        lexer_free_token(token);
        return NULL;
    }

    lexer_free_token(token);
    return lexer_next_token(lexer);
}

/* nível 0 é or, 1 and, 2 not, 3 comparação, 4 + e -, 5 * // %, depois vêm os átomos */
static Token *parse_expr(Lexer *lexer, Token *token, int level)
{
    if (level == 6)
        return parse_atom(lexer, token);

    if (level == 2 && token->type == TOKEN_NOT) {
        lexer_free_token(token);
        return parse_expr(lexer, lexer_next_token(lexer), 2);
    }

    token = parse_expr(lexer, token, level + 1);

    while (token != NULL && is_operator(token->type, level)) {
        if (token->type == TOKEN_SLASH) {
            printf("\033[31mError on line %u: use // to divide, pish only has integers.\033[0m\n",
                   token->line);
            lexer_free_token(token);
            return NULL;
        }

        lexer_free_token(token);
        token = parse_expr(lexer, lexer_next_token(lexer), level + 1);

        if (level == 3 && token != NULL && is_operator(token->type, 3)) {
            printf("\033[31mError on line %u: chained comparisons like a < b < c are not supported yet.\033[0m\n",
                   token->line);
            lexer_free_token(token);
            return NULL;
        }
    }

    return token;
}

static Token *parse_input(Lexer *lexer, Token *token)
{
    lexer_free_token(token);

    token = lexer_next_token(lexer);
    if (token->type != TOKEN_LPAREN) {
        error_expected(token, "'(' after input");
        lexer_free_token(token);
        return NULL;
    }
    lexer_free_token(token);

    token = lexer_next_token(lexer);
    if (token->type == TOKEN_STRING_LITERAL) {
        lexer_free_token(token);
        token = lexer_next_token(lexer);
    }

    if (token->type != TOKEN_RPAREN) {
        error_expected(token, "')'");
        lexer_free_token(token);
        return NULL;
    }
    lexer_free_token(token);

    return lexer_next_token(lexer);
}

int Parse(const char *code){
    Lexer lexer;
    lexer_init(&lexer, code);
    Token *token;
loop:
    token = lexer_next_token(&lexer);
    if (token == NULL) {
        fprintf(stderr, "error on creating the token.\n");
        return 1;
    }
    if (token->type == TOKEN_EOF) {
        lexer_free_token(token);
        return 0;
    }
    if (token->type == TOKEN_SEMICOLON) {   /* linha em branco */
        lexer_free_token(token);
        goto loop;
    }

    if (token->type == TOKEN_PRINT) {
        #include "inc/print/inc.inc"
    } else if (is_header(token->type)) {
        #include "inc/header/inc.inc"
    } else if (is_bare(token->type)) {
        lexer_free_token(token);
        token = lexer_next_token(&lexer);
    } else {
        #include "inc/vardeclare_assign/inc.inc"
    }

    if (token->type == TOKEN_EOF) {      /* última linha sem '\n' */
        lexer_free_token(token);
        return 0;
    }
    if (token->type != TOKEN_SEMICOLON) {
        error_expected(token, "end of line");
        lexer_free_token(token);
        return 1;
    }
    lexer_free_token(token);
    goto loop;
}
