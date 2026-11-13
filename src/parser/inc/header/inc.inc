/* aqui só a linha do if/elif/else/while, quem confere os blocos é o transpiler */
TokenType kind = token->type;
lexer_free_token(token);

token = lexer_next_token(&lexer);
if (kind != TOKEN_ELSE) {
    token = parse_expr(&lexer, token, 0);
    if (token == NULL)
        return 1;
}

if (token->type != TOKEN_COLON) {
    error_expected(token, "':'");
    lexer_free_token(token);
    return 1;
}
lexer_free_token(token);

token = lexer_next_token(&lexer);
if (token->type != TOKEN_SEMICOLON && token->type != TOKEN_EOF) {
    printf("\033[31mError on line %u: put the block on the next line, indented.\033[0m\n", token->line);
    lexer_free_token(token);
    return 1;
}
