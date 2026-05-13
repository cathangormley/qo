#pragma once
#include <stdbool.h>

/* Token kinds emitted by the lexer and consumed by the parser. */
typedef enum {
    TOKEN_EOF,
    TOKEN_NUMBER,
    TOKEN_BOOLEAN,
    TOKEN_HEX,
    TOKEN_IDENTIFIER,
    TOKEN_SYMBOL,
    TOKEN_CHAR,
    TOKEN_STRING,
    TOKEN_PLUS,
    TOKEN_MINUS,
    TOKEN_STAR,
    TOKEN_SLASH,
    TOKEN_PERCENT,
    TOKEN_BANG,
    TOKEN_COMMA,
    TOKEN_HASH,
    TOKEN_UNDERSCORE,
    TOKEN_EQUAL,
    TOKEN_LESS,
    TOKEN_GREATER,
    TOKEN_LE,
    TOKEN_GE,
    TOKEN_PIPE,
    TOKEN_AMP,
    TOKEN_COLON,
    TOKEN_SEMICOLON,
    TOKEN_LPAREN,
    TOKEN_RPAREN,
    TOKEN_LBRACKET,
    TOKEN_RBRACKET,
    TOKEN_LBRACE,
    TOKEN_RBRACE,
    TOKEN_AT,
    TOKEN_DOT,
    TOKEN_ERROR,
} TokenType;

/* A token with source text and minimal numeric metadata for parser decisions. */
typedef struct {
    TokenType type;
    char *lexeme;
    /* True when the number token contains a decimal point. */
    bool is_float;
    char type_suffix;  /* 'h', 'i', 'j', 'f', or '\0' */
    /* Byte offset of token start in the original input string. */
    int start_pos;
} Token;
