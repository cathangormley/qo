#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include "token.h"
#include "lexer.h"
#include "internal.h"

static int is_type_suffix_char(char c) {
    return c == 'h' || c == 'i' || c == 'j' || c == 'f';
}

static int is_char_in_set(char c, const char *set) {
    return strchr(set, (unsigned char)c) != NULL;
}

/*
 * Returns 1 when '-' at input[pos] should start a signed numeric literal
 * (e.g. -2, -.5) instead of being tokenized as binary subtraction.
 *
 * Rules:
 * - Start of input => unary.
 * - Immediately after whitespace => unary (supports compact vector forms like "1 -2 4").
 * - After delimiters/operators ("(", "[", ";", "+", etc.) => unary.
 * - Otherwise => binary minus.
 */
static int is_unary_sign_context(const char *input, int pos) {
    int i = pos - 1;

    if (i >= 0 && (input[i] == ' ' || input[i] == '\n' || input[i] == '\r' || input[i] == '\t'))
        return 1;

    if (i < 0) return 1;

    return is_char_in_set(input[i], "([{;:,+-*/%!<>=#@.|&$");
}

Lexer* lexer_new(const char *input) {
    Lexer *lexer = malloc(sizeof(Lexer));
    lexer->input = input;
    lexer->pos = 0;
    return lexer;
}

static Token* token_new(TokenType type, char *lexeme, bool is_float, char type_suffix, int start_pos) {
    Token *token = malloc(sizeof(Token));
    token->type = type;
    token->lexeme = lexeme ? strdup(lexeme) : NULL;
    token->is_float = is_float;
    token->type_suffix = type_suffix;
    token->start_pos = start_pos;
    return token;
}

Token* lexer_next_token(Lexer *lexer) {
    while (1) {
        /* Skip whitespace and newlines */
        while (lexer->input[lexer->pos] == ' ' || lexer->input[lexer->pos] == '\n' ||
               lexer->input[lexer->pos] == '\r' || lexer->input[lexer->pos] == '\t')
            lexer->pos++;

        /* Skip // line comments */
        if (lexer->input[lexer->pos] == '/' && lexer->input[lexer->pos + 1] == '/') {
            lexer->pos += 2;
            while (lexer->input[lexer->pos] != '\0' && lexer->input[lexer->pos] != '\n')
                lexer->pos++;
            continue;
        }

        /* Skip block comments (slash-star ... star-slash) */
        if (lexer->input[lexer->pos] == '/' && lexer->input[lexer->pos + 1] == '*') {
            lexer->pos += 2;
            while (lexer->input[lexer->pos] != '\0') {
                if (lexer->input[lexer->pos] == '*' && lexer->input[lexer->pos + 1] == '/') {
                    lexer->pos += 2;
                    break;
                }
                lexer->pos++;
            }
            continue;
        }

        break;  /* not a comment — proceed to tokenize */
    }

    int start_pos = lexer->pos;
    if (lexer->input[lexer->pos] == '\0')
        return token_new(TOKEN_EOF, NULL, false, '\0', start_pos);

    char ch = lexer->input[lexer->pos];

     /* Special int/long/float sentinels: 0N, 0Ni, 0Nj, 0Nf, 0W, 0Wi, 0Wj, 0Wf,
         -0W, -0Wi, -0Wj, -0Wf. Unsuffixed sentinels default to long (suffix j). */
    {
        int p = lexer->pos;
        int has_sign = 0;
        if (lexer->input[p] == '-' &&
            is_unary_sign_context(lexer->input, lexer->pos) &&
            lexer->input[p + 1] == '0') {
            has_sign = 1;
            p++;
        }
        if (lexer->input[p] == '0' &&
            (lexer->input[p + 1] == 'N' || lexer->input[p + 1] == 'W')) {
            char marker = lexer->input[p + 1];
            char suffix = '\0';
            int end = p + 2;

            if (is_type_suffix_char(lexer->input[end])) {
                suffix = lexer->input[end];
                end++;
            } else {
                suffix = '\0';
            }

            if (suffix == 'h' || suffix == 'i' || suffix == 'j' || suffix == 'f' || suffix == '\0') {
                char next = lexer->input[end];
                if (!(isalnum((unsigned char)next) || next == '_' || next == '.')) {
                    char value[4];
                    int len = 0;
                    if (has_sign) value[len++] = '-';
                    value[len++] = '0';
                    value[len++] = marker;
                    value[len] = '\0';
                    lexer->pos = end;
                    return token_new(TOKEN_NUMBER, value, suffix == 'f', suffix, start_pos);
                }
            }
        }
    }

    // Identifiers (variable names)
    // Note: '_' alone is an operator; '_' starts an identifier only if followed by alphanumeric
    if (isalpha(ch) || (ch == '_' && isalnum(lexer->input[lexer->pos + 1]))) {
        size_t capacity = 64;
        int len = 0;
        char *value = xmalloc(capacity);
        while (isalnum(lexer->input[lexer->pos]) || lexer->input[lexer->pos] == '_' || lexer->input[lexer->pos] == '.') {
            if (lexer->input[lexer->pos] == '.') {
                if (lexer->input[lexer->pos + 1] == '.' || lexer->input[lexer->pos + 1] == '\0')
                    break;
            }
            if ((size_t)len + 1 >= capacity) {
                capacity *= 2;
                value = xrealloc(value, capacity);
            }
            value[len++] = lexer->input[lexer->pos++];
        }
        value[len] = '\0';
        Token *t = token_new(TOKEN_IDENTIFIER, value, false, '\0', start_pos);
        free(value);
        return t;
    }

    // Symbols (`name)
    if (ch == '`') {
        lexer->pos++;  // Skip backtick
        size_t capacity = 64;
        int len = 0;
        char *value = xmalloc(capacity);

        while (isalnum(lexer->input[lexer->pos]) || lexer->input[lexer->pos] == '_'
               || lexer->input[lexer->pos] == '.') {
            if ((size_t)len + 1 >= capacity) {
                capacity *= 2;
                value = xrealloc(value, capacity);
            }
            value[len++] = lexer->input[lexer->pos++];
        }
        value[len] = '\0';
        Token *t = token_new(TOKEN_SYMBOL, value, false, '\0', start_pos);
        free(value);
        return t;
    }

    // String literals (double-quoted)
    if (ch == '"') {
        lexer->pos++;
        size_t capacity = 128;
        int len = 0;
        char *value = xmalloc(capacity);
        
        while (lexer->input[lexer->pos] != '"' && lexer->input[lexer->pos] != '\0') {
            if ((size_t)len + 2 >= capacity) {
                capacity *= 2;
                value = xrealloc(value, capacity);
            }
            if (lexer->input[lexer->pos] == '\\') {
                value[len++] = lexer->input[lexer->pos++];
                if (lexer->input[lexer->pos] != '\0') {
                    value[len++] = lexer->input[lexer->pos++];
                }
            } else {
                value[len++] = lexer->input[lexer->pos++];
            }
        }
        
        if (lexer->input[lexer->pos] == '"') {
            lexer->pos++;
        } else {
            fprintf(stderr, "Error: unterminated string literal\n");
            free(value);
            return token_new(TOKEN_ERROR, NULL, false, '\0', start_pos);
        }
        
        value[len] = '\0';
        Token *t = token_new(TOKEN_STRING, value, false, '\0', start_pos);
        free(value);
        return t;
    }

    /* Date literal: YYYY.MM.DD without trailing 'T' */
    if (isdigit((unsigned char)ch)) {
        const char *in = lexer->input;
        int p = lexer->pos;
        int date_ok =
            isdigit((unsigned char)in[p+0]) && isdigit((unsigned char)in[p+1]) &&
            isdigit((unsigned char)in[p+2]) && isdigit((unsigned char)in[p+3]) &&
            in[p+4] == '.' &&
            isdigit((unsigned char)in[p+5]) && isdigit((unsigned char)in[p+6]) &&
            in[p+7] == '.' &&
            isdigit((unsigned char)in[p+8]) && isdigit((unsigned char)in[p+9]) &&
            !(isdigit((unsigned char)in[p+10]) || in[p+10] == 'T');
        if (date_ok) {
            int end = p + 10;
            int len = end - p;
            char *value = xmalloc((size_t)len + 1);
            memcpy(value, in + p, (size_t)len);
            value[len] = '\0';
            lexer->pos = end;
            Token *t = token_new(TOKEN_DATE, value, false, '\0', start_pos);
            free(value);
            return t;
        }

        /* Timestamp literal: required prefix YYYY.MM.DDT, then optional
           HH, :MM, :SS, .<1..9 fractional digits> appended greedily. */
        int prefix_ok =
            isdigit((unsigned char)in[p+0]) && isdigit((unsigned char)in[p+1]) &&
            isdigit((unsigned char)in[p+2]) && isdigit((unsigned char)in[p+3]) &&
            in[p+4] == '.' &&
            isdigit((unsigned char)in[p+5]) && isdigit((unsigned char)in[p+6]) &&
            in[p+7] == '.' &&
            isdigit((unsigned char)in[p+8]) && isdigit((unsigned char)in[p+9]) &&
            in[p+10] == 'T';
        if (prefix_ok) {
            int end = p + 11;  /* past the 'T' */
            if (isdigit((unsigned char)in[end]) && isdigit((unsigned char)in[end+1])) {
                end += 2;                                                     /* HH */
                if (in[end] == ':' &&
                    isdigit((unsigned char)in[end+1]) && isdigit((unsigned char)in[end+2])) {
                    end += 3;                                                 /* :MM */
                    if (in[end] == ':' &&
                        isdigit((unsigned char)in[end+1]) && isdigit((unsigned char)in[end+2])) {
                        end += 3;                                             /* :SS */
                        if (in[end] == '.' && isdigit((unsigned char)in[end+1])) {
                            int frac = end + 1;
                            int digits = 0;
                            while (digits < 9 && isdigit((unsigned char)in[frac + digits])) digits++;
                            end = frac + digits;                              /* .<1..9 digits> */
                        }
                    }
                }
            }
            int len = end - p;
            char *value = xmalloc((size_t)len + 1);
            memcpy(value, in + p, (size_t)len);
            value[len] = '\0';
            lexer->pos = end;
            Token *t = token_new(TOKEN_TIMESTAMP, value, false, '\0', start_pos);
            free(value);
            return t;
        }

        /* Timespan literal: <digits>T, then optional HH:MM:SS.FFF. */
        {
            int ts_digits = 0;
            while (isdigit((unsigned char)in[p + ts_digits])) ts_digits++;
            if (ts_digits > 0 && in[p + ts_digits] == 'T') {
                int end = p + ts_digits + 1;          /* past the 'T' */
                if (isdigit((unsigned char)in[end]) && isdigit((unsigned char)in[end+1])) {
                    end += 2;                         /* HH */
                    if (in[end] == ':' &&
                        isdigit((unsigned char)in[end+1]) && isdigit((unsigned char)in[end+2])) {
                        end += 3;                     /* :MM */
                        if (in[end] == ':' &&
                            isdigit((unsigned char)in[end+1]) && isdigit((unsigned char)in[end+2])) {
                            end += 3;                 /* :SS */
                            if (in[end] == '.' && isdigit((unsigned char)in[end+1])) {
                                int frac = end + 1;
                                int digits = 0;
                                while (digits < 9 && isdigit((unsigned char)in[frac + digits])) digits++;
                                end = frac + digits;  /* .<1..9 digits> */
                            }
                        }
                    }
                }
                int len = end - p;
                char *value = xmalloc((size_t)len + 1);
                memcpy(value, in + p, (size_t)len);
                value[len] = '\0';
                lexer->pos = end;
                Token *t = token_new(TOKEN_TIMESPAN, value, false, '\0', start_pos);
                free(value);
                return t;
            }
        }
    }

    // Booleans (0b, 1b, or bit patterns like 1010011b)
    if ((isdigit(ch) || (ch == '-' && is_unary_sign_context(lexer->input, lexer->pos) && isdigit(lexer->input[lexer->pos + 1])))) {
        // Look ahead to check if this is a boolean
        int temp_pos = lexer->pos;
        if (ch == '-') temp_pos++;
        
        // Check if all digits are 0 or 1 and followed by 'b'
        int all_binary = 1;
        while (isdigit(lexer->input[temp_pos])) {
            if (lexer->input[temp_pos] != '0' && lexer->input[temp_pos] != '1') {
                all_binary = 0;
                break;
            }
            temp_pos++;
        }
        
        if (all_binary && lexer->input[temp_pos] == 'b') {
            // This is a boolean
            size_t capacity = 64;
            int len = 0;
            char *value = xmalloc(capacity);
            
            if (ch == '-') {
                value[len++] = lexer->input[lexer->pos++];
            }
            
            while (isdigit(lexer->input[lexer->pos])) {
                if ((size_t)len + 2 >= capacity) {
                    capacity *= 2;
                    value = xrealloc(value, capacity);
                }
                value[len++] = lexer->input[lexer->pos++];
            }
            
            value[len++] = lexer->input[lexer->pos++];  // consume 'b'
            value[len] = '\0';
            Token *t = token_new(TOKEN_BOOLEAN, value, false, '\0', start_pos);
            free(value);
            return t;
        }
    }

    // Hex byte / byte-vector literals: 0x01, 0x0102, ...
    if (ch == '0' && lexer->input[lexer->pos + 1] == 'x') {
        int p = lexer->pos + 2;
        int hex_len = 0;
        while (isxdigit((unsigned char)lexer->input[p])) {
            p++;
            hex_len++;
        }
        if (hex_len > 0) {
            char *value = xmalloc((size_t)hex_len + 3);
            int len = 0;
            value[len++] = lexer->input[lexer->pos++];
            value[len++] = lexer->input[lexer->pos++];
            while (isxdigit((unsigned char)lexer->input[lexer->pos])) {
                value[len++] = lexer->input[lexer->pos++];
            }
            value[len] = '\0';
            Token *t = token_new(TOKEN_HEX, value, false, '\0', start_pos);
            free(value);
            return t;
        }
    }

    // Numbers (integers and floats), including unary signed negatives like -2 and -.5
    if (isdigit(ch) || (ch == '.' && isdigit(lexer->input[lexer->pos + 1])) ||
        (ch == '-' && is_unary_sign_context(lexer->input, lexer->pos) &&
         (isdigit(lexer->input[lexer->pos + 1]) ||
          (lexer->input[lexer->pos + 1] == '.' && isdigit(lexer->input[lexer->pos + 2]))))) {
        size_t capacity = 64;
        int len = 0;
        char *value = xmalloc(capacity);
        bool is_float = false;
        char type_suffix = '\0';

        if (ch == '-') {
            value[len++] = lexer->input[lexer->pos++];
        }
        
        // Read integer part
        while (isdigit(lexer->input[lexer->pos])) {
            if ((size_t)len + 1 >= capacity) {
                capacity *= 2;
                value = xrealloc(value, capacity);
            }
            value[len++] = lexer->input[lexer->pos++];
        }
        
        // Check for decimal point (but not `..` range operator)
        if (lexer->input[lexer->pos] == '.' && lexer->input[lexer->pos + 1] != '.') {
            is_float = true;
            if ((size_t)len + 1 >= capacity) {
                capacity *= 2;
                value = xrealloc(value, capacity);
            }
            value[len++] = lexer->input[lexer->pos++];
            // Read fractional part
            while (isdigit(lexer->input[lexer->pos])) {
                if ((size_t)len + 1 >= capacity) {
                    capacity *= 2;
                    value = xrealloc(value, capacity);
                }
                value[len++] = lexer->input[lexer->pos++];
            }
        }
        
        // Check for type suffix (h=short, i=int, j=long, f=float)
        if (is_type_suffix_char(lexer->input[lexer->pos])) {
            type_suffix = lexer->input[lexer->pos++];
        }
        
        value[len] = '\0';
        Token *t = token_new(TOKEN_NUMBER, value, is_float, type_suffix, start_pos);
        free(value);
        return t;
    }

    lexer->pos++;

    // Operators and parentheses
    switch (ch) {
        case '+': return token_new(TOKEN_PLUS, "+", false, '\0', start_pos);
        case '-': return token_new(TOKEN_MINUS, "-", false, '\0', start_pos);
        case '*':
            if (lexer->input[lexer->pos] == '*') {
                lexer->pos++;
                return token_new(TOKEN_STAR_STAR, "**", false, '\0', start_pos);
            }
            return token_new(TOKEN_STAR, "*", false, '\0', start_pos);
        case '/':
            if (lexer->input[lexer->pos] == ':') {
                lexer->pos++;
                return token_new(TOKEN_EACHRIGHT, "/:", false, '\0', start_pos);
            }
            return token_new(TOKEN_SLASH, "/", false, '\0', start_pos);
        case '%': return token_new(TOKEN_DIVIDE, "%", false, '\0', start_pos);
        case '!': return token_new(TOKEN_BANG, "!", false, '\0', start_pos);
        case ',': return token_new(TOKEN_COMMA, ",", false, '\0', start_pos);
        case '#': return token_new(TOKEN_HASH, "#", false, '\0', start_pos);
        case '_': return token_new(TOKEN_UNDERSCORE, "_", false, '\0', start_pos);
        case '=': return token_new(TOKEN_EQUAL, "=", false, '\0', start_pos);
        case '<': {
            if (lexer->input[lexer->pos] == '=') {
                lexer->pos++;
                return token_new(TOKEN_LE, "<=", false, '\0', start_pos);
            }
            return token_new(TOKEN_LESS, "<", false, '\0', start_pos);
        }
        case '>': {
            if (lexer->input[lexer->pos] == '=') {
                lexer->pos++;
                return token_new(TOKEN_GE, ">=", false, '\0', start_pos);
            }
            return token_new(TOKEN_GREATER, ">", false, '\0', start_pos);
        }
        case '|': return token_new(TOKEN_PIPE, "|", false, '\0', start_pos);
        case '&': return token_new(TOKEN_AMP, "&", false, '\0', start_pos);
        case '^': return token_new(TOKEN_CARET, "^", false, '\0', start_pos);
        case ':': {
            if (lexer->input[lexer->pos] == ':') {
                lexer->pos++;
                return token_new(TOKEN_NULL_OP, "::", false, '\0', start_pos);
            }
            return token_new(TOKEN_COLON, ":", false, '\0', start_pos);
        }
        case ';': return token_new(TOKEN_SEMICOLON, ";", false, '\0', start_pos);
        case '(': return token_new(TOKEN_LPAREN, "(", false, '\0', start_pos);
        case ')': return token_new(TOKEN_RPAREN, ")", false, '\0', start_pos);
        case '[': return token_new(TOKEN_LBRACKET, "[", false, '\0', start_pos);
        case ']': return token_new(TOKEN_RBRACKET, "]", false, '\0', start_pos);
        case '{': return token_new(TOKEN_LBRACE, "{", false, '\0', start_pos);
        case '}': return token_new(TOKEN_RBRACE, "}", false, '\0', start_pos);
        case '@': return token_new(TOKEN_AT,     "@", false, '\0', start_pos);
        case '$': return token_new(TOKEN_DOLLAR, "$", false, '\0', start_pos);
        case '.': {
            if (lexer->input[lexer->pos] == '.') {
                lexer->pos++;
                if (lexer->input[lexer->pos] == '=') {
                    lexer->pos++;
                    return token_new(TOKEN_DOT_DOT_EQ, "..=", false, '\0', start_pos);
                }
                return token_new(TOKEN_DOT_DOT, "..", false, '\0', start_pos);
            }
            return token_new(TOKEN_DOT,    ".", false, '\0', start_pos);
        }
        case '?': return token_new(TOKEN_QUESTION, "?", false, '\0', start_pos);
        case '\'': return token_new(TOKEN_EACH,   "'", false, '\0', start_pos);
        case '\\':
            if (lexer->input[lexer->pos] == ':') {
                lexer->pos++;
                return token_new(TOKEN_EACHLEFT, "\\:", false, '\0', start_pos);
            }
            {
                char lexeme[2] = {ch, '\0'};
                return token_new(TOKEN_ERROR, lexeme, false, '\0', start_pos);
            }
        default: {
            char lexeme[2] = {ch, '\0'};
            return token_new(TOKEN_ERROR, lexeme, false, '\0', start_pos);
        }
    }
}

void token_free(Token *token) {
    if (token->lexeme) free(token->lexeme);
    free(token);
}

void lexer_free(Lexer *lexer) {
    free(lexer);
}

TokenBuffer tokenize_input(const char *input) {
    Lexer *lexer = lexer_new(input);
    int capacity = 256;
    TokenBuffer buffer;

    buffer.tokens = xmalloc(sizeof(Token *) * capacity);
    buffer.count = 0;

    while (1) {
        Token *token = lexer_next_token(lexer);
        if (buffer.count >= capacity - 1) {
            capacity *= 2;
            buffer.tokens = xrealloc(buffer.tokens, sizeof(Token *) * capacity);
        }
        buffer.tokens[buffer.count++] = token;
        if (token->type == TOKEN_EOF) {
            break;
        }
    }

    lexer_free(lexer);
    return buffer;
}

void free_token_buffer(TokenBuffer buffer) {
    for (int i = 0; i < buffer.count; i++) {
        token_free(buffer.tokens[i]);
    }
    free(buffer.tokens);
}