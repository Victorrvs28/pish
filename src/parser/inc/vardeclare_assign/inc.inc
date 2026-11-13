if (token->type != TOKEN_IDENTIFIER) {
    error_word(token);
    lexer_free_token(token);
    return 1;
}
lexer_free_token(token);

token = lexer_next_token(&lexer);
if (token->type != TOKEN_ASSIGN) {
    error_expected(token, "'='");
    lexer_free_token(token);
    return 1;
}
lexer_free_token(token);

token = lexer_next_token(&lexer);
if (token->type == TOKEN_INPUT)
    token = parse_input(&lexer, token);
else
    token = parse_expr(&lexer, token, 0);

if (token == NULL)
    return 1;
