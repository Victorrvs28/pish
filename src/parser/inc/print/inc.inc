lexer_free_token(token);

token = lexer_next_token(&lexer);
if (token->type != TOKEN_LPAREN) {
    error_expected(token, "'(' after print");
    lexer_free_token(token);
    return 1;
}
lexer_free_token(token);

token = lexer_next_token(&lexer);
while (token->type != TOKEN_RPAREN) {      /* print() sem nada também vale */
    token = parse_expr(&lexer, token, 0);
    if (token == NULL)
        return 1;

    if (token->type == TOKEN_COMMA) {
        lexer_free_token(token);
        token = lexer_next_token(&lexer);
    } else if (token->type != TOKEN_RPAREN) {
        error_expected(token, "')'");
        lexer_free_token(token);
        return 1;
    }
}
lexer_free_token(token);

token = lexer_next_token(&lexer);
