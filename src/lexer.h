#pragma once
#include "token.h"

typedef struct {
    const char *input;
    int pos;
} Lexer;

Lexer* lexer_new(const char *input);
Token* lexer_next_token(Lexer *lexer);
void token_free(Token *token);
void lexer_free(Lexer *lexer);

/* Tokenizer buffering and management */
typedef struct TokenBuffer {
    Token **tokens;
    int count;
} TokenBuffer;

TokenBuffer tokenize_input(const char *input);
void free_token_buffer(TokenBuffer buffer);
