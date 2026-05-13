#pragma once

#include "qo_value.h"
#include "token.h"

typedef struct Parser {
    Token **tokens;
    int count;
    int pos;
    const char *input;
} Parser;

Parser *parser_new(Token **tokens, int count, const char *input);
void parser_free(Parser *parser);
Qo parser_parse(Parser *parser);
