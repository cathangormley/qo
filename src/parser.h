#pragma once

#include <stdint.h>

typedef struct QoObject *Qo;
struct Token;

/* Sentinel value for parse errors (distinct from NULL which represents a valid null value). */
#define PARSE_ERROR ((Qo)1)

/* Returns non-zero if the given value is a parse error sentinel. */
#define is_parse_error(v) ((v) == PARSE_ERROR)

typedef struct Parser {
    struct Token **tokens;
    int count;
    int pos;
    const char *input;
} Parser;

Parser *parser_new(struct Token **tokens, int count, const char *input);
void parser_free(Parser *parser);
Qo parser_parse(Parser *parser);
