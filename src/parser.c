#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "internal.h"
#include "parser.h"
#include "evaluator_internal.h"

static Token *current_token(Parser *parser) {
    if (parser->pos < parser->count) {
        return parser->tokens[parser->pos];
    }
    return NULL;
}

static void advance(Parser *parser) {
    parser->pos++;
}

const char *token_type_to_operator(TokenType op) {
    switch (op) {
        case TOKEN_PLUS: return "+";
        case TOKEN_MINUS: return "-";
        case TOKEN_STAR: return "*";
        case TOKEN_STAR_STAR: return "**";
        case TOKEN_SLASH: return "/";
        case TOKEN_DIVIDE: return "%";
        case TOKEN_BANG: return "!";
        case TOKEN_COMMA: return ",";
        case TOKEN_HASH: return "#";
        case TOKEN_UNDERSCORE: return "_";
        case TOKEN_EQUAL: return "=";
        case TOKEN_LESS: return "<";
        case TOKEN_GREATER: return ">";
        case TOKEN_LE: return "<=";
        case TOKEN_GE: return ">=";
        case TOKEN_PIPE: return "|";
        case TOKEN_AMP: return "&";
        case TOKEN_COLON: return ":";
        case TOKEN_AT:    return "@";
        case TOKEN_DOT:   return ".";
        case TOKEN_DOT_DOT:    return "..";
        case TOKEN_DOT_DOT_EQ: return "..=";
        case TOKEN_DOLLAR:    return "$";
        case TOKEN_QUESTION:  return "?";
        case TOKEN_TABLE:     return "table";
        default: return NULL;
    }
}


static int hex_digit_value(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static int is_special_int_literal(const Token *token) {
    if (!token || token->type != TOKEN_NUMBER || token->lexeme == NULL) return 0;
    return strcmp(token->lexeme, "0N") == 0 ||
           strcmp(token->lexeme, "0W") == 0 ||
           strcmp(token->lexeme, "-0W") == 0;
}

static Qo make_special_int_value(const Token *token, char final_suffix) {
    int use_int = (final_suffix == 'i');
    int use_float = (final_suffix == 'f');
    if (!use_int && !use_float && final_suffix != '\0' && final_suffix != 'j') {
        fprintf(stderr, "Error: special literal requires i, j, or f suffix\n");
        return PARSE_ERROR;
    }

    if (use_float) {
        if (strcmp(token->lexeme, "0N") == 0) return make_float_value(QO_FLOAT_NULL);
        if (strcmp(token->lexeme, "0W") == 0) return make_float_value(INFINITY);
        if (strcmp(token->lexeme, "-0W") == 0) return make_float_value(-INFINITY);
    } else {
        if (strcmp(token->lexeme, "0N") == 0) {
            return use_int ? make_int_value(QO_INT_NULL) : make_long_value(QO_LONG_NULL);
        }
        if (strcmp(token->lexeme, "0W") == 0) {
            return use_int ? make_int_value(QO_INT_INF) : make_long_value(QO_LONG_INF);
        }
        if (strcmp(token->lexeme, "-0W") == 0) {
            return use_int ? make_int_value(QO_INT_NEGINF) : make_long_value(QO_LONG_NEGINF);
        }
    }

    fprintf(stderr, "Error: invalid special literal\n");
    return PARSE_ERROR;
}

/* Helper: Parse a sequence of numbers into a vector or single scalar */
static Qo parse_number_sequence(Parser *parser, Token *first_token) {
    int capacity = 64;
    Qo *values = xmalloc(sizeof(Qo) * (size_t)capacity);
    Token **tokens = xmalloc(sizeof(Token*) * (size_t)capacity);
    int count = 0;
    
    /* Collect all consecutive numbers */
    tokens[count] = first_token;
    count++;
    advance(parser);
    
    while (1) {
        if (count >= capacity) {
            capacity *= 2;
            values = xrealloc(values, sizeof(Qo) * (size_t)capacity);
            tokens = xrealloc(tokens, sizeof(Token*) * (size_t)capacity);
        }
        Token *next = current_token(parser);
        if (!next || next->type != TOKEN_NUMBER) break;
        tokens[count] = next;
        count++;
        advance(parser);
    }
    
    /* Validate: only the last token can have a type suffix */
    for (int i = 0; i < count - 1; i++) {
        if (tokens[i]->type_suffix != '\0') {
            fprintf(stderr, "Error: type suffix only allowed on last number in sequence (got '%c' on element %d)\n", 
                    tokens[i]->type_suffix, i + 1);
            free(values);
            free(tokens);
            return PARSE_ERROR;
        }
    }
    
    /* Determine type from the last token's suffix */
    char final_suffix = tokens[count - 1]->type_suffix;
    int has_float = (final_suffix == 'f') ? 1 : 0;
    
    for (int i = 0; i < count; i++) {
        Token *t = tokens[i];
        if (is_special_int_literal(t)) {
            values[i] = make_special_int_value(t, final_suffix);
            if (values[i] == PARSE_ERROR) {
                for (int j = 0; j < i; j++) qo_release(values[j]);
                free(values);
                free(tokens);
                return PARSE_ERROR;
            }
            continue;
        }
        double val = strtod(t->lexeme, NULL);
        
        if (t->is_float) {
            has_float = 1;
            values[i] = make_float_value(val);
        } else {
            int64_t ival = (int64_t)val;
            if (final_suffix == 'h') {
                values[i] = make_short_value((int16_t)ival);
            } else if (final_suffix == 'i') {
                values[i] = make_int_value((int32_t)ival);
            } else if (final_suffix == 'j') {
                values[i] = make_long_value(ival);
            } else if (final_suffix == 'f') {
                values[i] = make_float_value(val);
            } else {
                /* Default integer literals use long when no suffix is present. */
                values[i] = make_long_value(ival);
            }
        }
    }
    
    /* Return scalar or vector */
    if (count == 1) {
        Qo result = values[0];
        free(values);
        free(tokens);
        return result;
    } else {
        /* Create vector */
        uint8_t vec_type;
        if (has_float || final_suffix == 'f') {
            vec_type = QO_FLOAT_VEC;
        } else if (final_suffix == 'h') {
            vec_type = QO_SHORT_VEC;
        } else if (final_suffix == 'i') {
            vec_type = QO_INT_VEC;
        } else if (final_suffix == 'j') {
            vec_type = QO_LONG_VEC;
        } else {
            vec_type = QO_LONG_VEC;
        }
        
        Qo result = alloc_data_vec(vec_type, count);
        
        if (vec_type == QO_SHORT_VEC) {
            for (int i = 0; i < count; i++) {
                qo_short_data(result)[i] = qo_short(values[i]);
                qo_release(values[i]);
            }
        } else if (vec_type == QO_INT_VEC) {
            for (int i = 0; i < count; i++) {
                qo_int_data(result)[i] = qo_int(values[i]);
                qo_release(values[i]);
            }
        } else if (vec_type == QO_LONG_VEC) {
            for (int i = 0; i < count; i++) {
                qo_long_data(result)[i] = qo_long(values[i]);
                qo_release(values[i]);
            }
        } else {  /* Float vector */
            for (int i = 0; i < count; i++) {
                if (qo_type(values[i]) == QO_FLOAT) {
                    qo_float_data(result)[i] = qo_float(values[i]);
                } else if (qo_type(values[i]) == QO_SHORT) {
                    qo_float_data(result)[i] = (double)qo_short(values[i]);
                } else if (qo_type(values[i]) == QO_INT) {
                    qo_float_data(result)[i] = (double)qo_int(values[i]);
                } else {
                    qo_float_data(result)[i] = (double)qo_long(values[i]);
                }
                qo_release(values[i]);
            }
        }
        free(values);
        free(tokens);
        return result;
    }
}

static Qo parse_expression(Parser *parser);
static Qo parse_factor(Parser *parser);
static Qo parse_postfix_calls(Parser *parser, Qo base);
static Qo parse_table_literal(Parser *parser);

/* Parse HH, :MM, :SS, and .<1..9 frac digits> starting at s[pos].
   Fields not present default to zero. */
static void parse_hms_nanos(const char *s, size_t pos, size_t len,
                            int *hour, int *minute, int *second, int64_t *nanos) {
    if (len > pos) {
        *hour = (s[pos] - '0') * 10 + (s[pos + 1] - '0');
        if (len >= pos + 4 && s[pos + 2] == ':') {
            *minute = (s[pos + 3] - '0') * 10 + (s[pos + 4] - '0');
            if (len >= pos + 7 && s[pos + 5] == ':') {
                *second = (s[pos + 6] - '0') * 10 + (s[pos + 7] - '0');
                if (len >= pos + 9 && s[pos + 8] == '.') {
                    int digits = (int)(len - (pos + 9));
                    if (digits > 9) digits = 9;
                    for (int i = 0; i < digits; i++) *nanos = *nanos * 10 + (s[pos + 9 + i] - '0');
                    for (int i = digits; i < 9; i++) *nanos *= 10;
                }
            }
        }
    }
}

/* Convert a timestamp lexeme into nanoseconds since unix epoch.
   The required prefix is "YYYY.MM.DDT"; HH, :MM, :SS, and .<1..9 frac digits>
   are optional and default to zero. */
static int64_t parse_timestamp_lexeme(const char *s) {
    int year   = (s[0]-'0')*1000 + (s[1]-'0')*100 + (s[2]-'0')*10 + (s[3]-'0');
    int month  = (s[5]-'0')*10 + (s[6]-'0');
    int day    = (s[8]-'0')*10 + (s[9]-'0');
    int hour = 0, minute = 0, second = 0;
    int64_t nanos = 0;

    parse_hms_nanos(s, 11, strlen(s), &hour, &minute, &second, &nanos);

    /* Days from civil (Howard Hinnant). Treats year/month/day as UTC. */
    int y = year - (month <= 2);
    int era = (y >= 0 ? y : y - 399) / 400;
    unsigned yoe = (unsigned)(y - era * 400);
    unsigned doy = (153u * (unsigned)(month + (month > 2 ? -3 : 9)) + 2) / 5 + (unsigned)day - 1;
    unsigned doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
    int64_t days = (int64_t)era * 146097 + (int64_t)doe - 719468;

    int64_t secs = days * 86400 + hour * 3600 + minute * 60 + second;
    return secs * 1000000000LL + nanos;
}

/* Convert a timespan lexeme "<days>T[HH[:MM[:SS[.FFF]]]]" into nanoseconds. */
static int64_t parse_timespan_lexeme(const char *s) {
    const char *t = strchr(s, 'T');
    if (!t) return 0;
    size_t day_len = (size_t)(t - s);
    int days = 0;
    for (size_t i = 0; i < day_len; i++) days = days * 10 + (s[i] - '0');

    int hour = 0, minute = 0, second = 0;
    int64_t nanos = 0;
    parse_hms_nanos(s, (size_t)(t - s + 1), strlen(s), &hour, &minute, &second, &nanos);

    int64_t secs = (int64_t)days * 86400 + hour * 3600 + minute * 60 + second;
    return secs * 1000000000LL + nanos;
}

static Qo parse_timespan_sequence(Parser *parser, Token *first_token) {
    int capacity = 16;
    int count = 0;
    int64_t *values = xmalloc(sizeof(int64_t) * (size_t)capacity);

    values[count++] = parse_timespan_lexeme(first_token->lexeme);
    advance(parser);

    while (1) {
        Token *next = current_token(parser);
        if (!next || next->type != TOKEN_TIMESPAN) break;
        if (count >= capacity) {
            capacity *= 2;
            values = xrealloc(values, sizeof(int64_t) * (size_t)capacity);
        }
        values[count++] = parse_timespan_lexeme(next->lexeme);
        advance(parser);
    }

    if (count == 1) {
        Qo r = make_timespan_value(values[0]);
        free(values);
        return r;
    }
    Qo result = alloc_data_vec(QO_TIMESPAN_VEC, count);
    memcpy(qo_long_data(result), values, (size_t)count * sizeof(int64_t));
    free(values);
    return result;
}

static Qo parse_timestamp_sequence(Parser *parser, Token *first_token) {
    int capacity = 16;
    int count = 0;
    int64_t *values = xmalloc(sizeof(int64_t) * (size_t)capacity);

    values[count++] = parse_timestamp_lexeme(first_token->lexeme);
    advance(parser);

    while (1) {
        Token *next = current_token(parser);
        if (!next || next->type != TOKEN_TIMESTAMP) break;
        if (count >= capacity) {
            capacity *= 2;
            values = xrealloc(values, sizeof(int64_t) * (size_t)capacity);
        }
        values[count++] = parse_timestamp_lexeme(next->lexeme);
        advance(parser);
    }

    if (count == 1) {
        Qo r = make_timestamp_value(values[0]);
        free(values);
        return r;
    }
    Qo result = alloc_data_vec(QO_TIMESTAMP_VEC, count);
    memcpy(qo_long_data(result), values, (size_t)count * sizeof(int64_t));
    free(values);
    return result;
}

static Qo parse_symbol_sequence(Parser *parser, Token *first_token) {
    int capacity = 64;
    Qo *values = xmalloc(sizeof(Qo) * (size_t)capacity);
    int count = 0;

    values[count++] = make_symbol_value(first_token->lexeme);
    advance(parser);

    while (1) {
        if (count >= capacity) {
            capacity *= 2;
            values = xrealloc(values, sizeof(Qo) * (size_t)capacity);
        }
        Token *next = current_token(parser);
        if (!next || next->type != TOKEN_SYMBOL) break;
        values[count++] = make_symbol_value(next->lexeme);
        advance(parser);
    }

    if (count == 1) {
        Qo result = qo_make_list_take(values, 1);
        return parse_postfix_calls(parser, result);
    }

    {
        Qo result = alloc_ptr_vec(QO_SYM_VEC, count);
        for (int i = 0; i < count; i++) {
            QO_LIST_DATA(result)[i] = values[i];
        }
        free(values);
        return parse_postfix_calls(parser, result);
    }
}

static int is_expression_operator(TokenType type) {
    return type == TOKEN_PLUS || type == TOKEN_MINUS || type == TOKEN_STAR ||
           type == TOKEN_STAR_STAR || type == TOKEN_SLASH || type == TOKEN_DIVIDE || type == TOKEN_BANG ||
           type == TOKEN_COMMA || type == TOKEN_HASH || type == TOKEN_UNDERSCORE ||
           type == TOKEN_EQUAL || type == TOKEN_LESS || type == TOKEN_GREATER ||
           type == TOKEN_LE || type == TOKEN_GE ||
           type == TOKEN_PIPE || type == TOKEN_AMP ||
           type == TOKEN_COLON || type == TOKEN_AT || type == TOKEN_DOT ||
           type == TOKEN_DOT_DOT || type == TOKEN_DOT_DOT_EQ ||
           type == TOKEN_DOLLAR ||
           type == TOKEN_QUESTION;
}

static int starts_factor(TokenType type) {
    return type == TOKEN_NUMBER ||
           type == TOKEN_BOOLEAN ||
           type == TOKEN_HEX ||
           type == TOKEN_TIMESTAMP ||
           type == TOKEN_TIMESPAN ||
           type == TOKEN_IDENTIFIER ||
           type == TOKEN_SYMBOL ||
           type == TOKEN_STRING ||
           type == TOKEN_LPAREN ||
           type == TOKEN_LBRACE ||
           type == TOKEN_EACH ||
           is_expression_operator(type);
}



static void free_param_list(char **params, int param_count) {
    if (!params) return;
    for (int i = 0; i < param_count; i++) {
        free(params[i]);
    }
    free(params);
}

static Qo finish_function_literal(Parser *parser,
                                  Token *opening_brace,
                                  Token *closing_brace,
                                  char **params,
                                  int param_count,
                                  Qo *statements,
                                  int statement_count,
                                  const char *fallback_source,
                                  int fallback_source_len) {
    int start = opening_brace ? opening_brace->start_pos : 0;
    int end = closing_brace ? (closing_brace->start_pos + 1) : start;
    int len = end - start;
    const char *src = fallback_source;
    int src_len = fallback_source_len;

    if (parser->input && len > 0) {
        src = &parser->input[start];
        src_len = len;
    }

    {
        Qo fn = make_function_value(params, param_count, statements, statement_count, src, src_len);
        free_param_list(params, param_count);
        /* release each body element (make_function_value retained them) */
        for (int i = 0; i < statement_count; i++) qo_release(statements[i]);
        if (statements) free(statements);
        return parse_postfix_calls(parser, fn);
    }
}

static Qo make_call_value(Qo base, Qo arg) {
    Qo *call_elements = xmalloc(sizeof(Qo) * 2);
    call_elements[0] = base;
    call_elements[1] = arg;
    return qo_make_list_take(call_elements, 2);
}

static Qo parse_postfix_calls(Parser *parser, Qo base) {
    Token *token = current_token(parser);
    int capacity = 4;
    int count = 0;
    Qo *args = NULL;

    while (token && (token->type == TOKEN_LBRACKET || token->type == TOKEN_EACH)) {
        if (token->type == TOKEN_EACH) {
            advance(parser);
            Qo *elements = xmalloc(sizeof(Qo) * 2);
            elements[0] = make_adverb_value(QO_ADVERB_EACH);
            elements[1] = base;
            base = qo_make_list_take(elements, 2);
            token = current_token(parser);
            continue;
        }

        capacity = 4;
        count = 0;
        args = NULL;

        advance(parser);
        args = xmalloc(sizeof(Qo) * capacity);
        token = current_token(parser);
        if (!token) {
            fprintf(stderr, "Error: unexpected end of input in function call\n");
            goto bracket_cleanup;
        }

        if (token->type == TOKEN_RBRACKET) {
            Qo *call_elements = xmalloc(sizeof(Qo) * 2);
            advance(parser);
            call_elements[0] = base;
            call_elements[1] = make_null_value();
            free(args);
            args = NULL;
            base = qo_make_list_take(call_elements, 2);
            token = current_token(parser);
            continue;
        }

        {
            int expecting_slot = 1;
            while (1) {
                token = current_token(parser);
                if (!token) {
                    fprintf(stderr, "Error: unexpected end of input in function call\n");
                    goto bracket_cleanup;
                }

                if (token->type == TOKEN_RBRACKET) {
                    if (expecting_slot && count > 0) {
                        if (count >= capacity) {
                            capacity *= 2;
                            args = xrealloc(args, sizeof(Qo) * capacity);
                        }
                        args[count++] = make_projector_value();
                    }

                    advance(parser);
                    {
                        Qo *call_elements = xmalloc(sizeof(Qo) * (count + 1));
                        call_elements[0] = base;
                        for (int i = 0; i < count; i++) {
                            call_elements[i + 1] = args[i];
                        }
                        free(args);
                        args = NULL;
                        base = qo_make_list_take(call_elements, count + 1);
                        break;
                    }
                }

                if (token->type == TOKEN_SEMICOLON) {
                    if (count >= capacity) {
                        capacity *= 2;
                        args = xrealloc(args, sizeof(Qo) * capacity);
                    }
                    args[count++] = make_projector_value();
                    advance(parser);
                    expecting_slot = 1;
                    continue;
                }

                {
                    Qo arg = parse_expression(parser);
                    if (is_parse_error(arg)) {
                        goto bracket_cleanup;
                    }

                    if (count >= capacity) {
                        capacity *= 2;
                        args = xrealloc(args, sizeof(Qo) * capacity);
                    }
                    args[count++] = arg;
                    expecting_slot = 0;

                    token = current_token(parser);
                    if (!token) {
                        fprintf(stderr, "Error: unexpected end of input in function call\n");
                        goto bracket_cleanup;
                    }

                    if (token->type == TOKEN_SEMICOLON) {
                        advance(parser);
                        expecting_slot = 1;
                        continue;
                    }

                    if (token->type == TOKEN_RBRACKET) {
                        continue;
                    }

                    fprintf(stderr, "Error: expected ';' or ']' in function call\n");
                    goto bracket_cleanup;
                }
            }
        }

        token = current_token(parser);
        continue;

bracket_cleanup:
        for (int i = 0; i < count; i++) qo_release(args[i]);
        free(args);
        qo_release(base);
        return PARSE_ERROR;
    }

    return base;
}

static Qo parse_parenthesized(Parser *parser) {
    Token *token;
    Qo first;

    advance(parser);
    token = current_token(parser);
    if (!token) {
        fprintf(stderr, "Error: unexpected end of input after '('\n");
        return PARSE_ERROR;
    }

    if (token->type == TOKEN_RPAREN) {
        Qo empty = alloc_ptr_vec(QO_LIST, 0);
        advance(parser);
        return parse_postfix_calls(parser, empty);
    }

    first = parse_expression(parser);
    if (is_parse_error(first)) {
        return PARSE_ERROR;
    }

    token = current_token(parser);
    if (!token) {
        qo_release(first);
        fprintf(stderr, "Error: unexpected end of input after '('\n");
        return PARSE_ERROR;
    }

    if (token->type == TOKEN_RPAREN) {
        advance(parser);
        return parse_postfix_calls(parser, first);
    }

    if (token->type == TOKEN_SEMICOLON) {
        int capacity = 8;
        int count = 0;
        Qo *elements = xmalloc(sizeof(Qo) * capacity);
        elements[count++] = first;

        while (token && token->type == TOKEN_SEMICOLON) {
            Qo elem;
            advance(parser);
            elem = parse_expression(parser);
            if (is_parse_error(elem)) {
                for (int i = 0; i < count; i++) {
                    qo_release(elements[i]);
                }
                free(elements);
                return PARSE_ERROR;
            }

            if (count >= capacity) {
                capacity *= 2;
                elements = xrealloc(elements, sizeof(Qo) * capacity);
            }
            elements[count++] = elem;
            token = current_token(parser);
        }

        if (!token || token->type != TOKEN_RPAREN) {
            for (int i = 0; i < count; i++) {
                qo_release(elements[i]);
            }
            free(elements);
            fprintf(stderr, "Error: expected ')' to close list literal\n");
            return PARSE_ERROR;
        }

        advance(parser);
        {
            Qo *enlist_elements = xmalloc(sizeof(Qo) * (count + 1));
            enlist_elements[0] = make_builtin_value(QO_BUILTIN_ENLIST);
            for (int i = 0; i < count; i++) {
                enlist_elements[i + 1] = elements[i];
            }
            free(elements);
            return parse_postfix_calls(parser, qo_make_list_take(enlist_elements, count + 1));
        }
    }

    qo_release(first);
    fprintf(stderr, "Error: expected ')' or ';' after expression in parentheses\n");
    return PARSE_ERROR;
}

static Qo parse_function_literal(Parser *parser) {
    Token *token;
    int capacity = 8;
    int count = 0;
    Qo *statements = NULL;
    Token *opening_brace;
    char **params = NULL;
    int param_count = 0;

    opening_brace = current_token(parser);
    advance(parser);
    token = current_token(parser);
    if (!token) {
        fprintf(stderr, "Error: unexpected end of input in function literal\n");
        return PARSE_ERROR;
    }

    if (token->type == TOKEN_LBRACKET) {
        int param_capacity = 8;
        advance(parser);
        params = xmalloc(sizeof(char *) * param_capacity);

        token = current_token(parser);
        if (!token) {
            fprintf(stderr, "Error: unexpected end of input in parameter list\n");
            free(params);
            return PARSE_ERROR;
        }

        if (token->type != TOKEN_RBRACKET) {
            while (1) {
                token = current_token(parser);
                if (!token) {
                    fprintf(stderr, "Error: unexpected end of input in parameter list\n");
                    free_param_list(params, param_count);
                    return PARSE_ERROR;
                }

                if (token->type != TOKEN_IDENTIFIER) {
                    fprintf(stderr, "Error: expected identifier in parameter list\n");
                    free_param_list(params, param_count);
                    return PARSE_ERROR;
                }

                if (param_count >= param_capacity) {
                    param_capacity *= 2;
                    params = xrealloc(params, sizeof(char *) * param_capacity);
                }
                params[param_count++] = xstrdup(token->lexeme);
                advance(parser);

                token = current_token(parser);
                if (!token) {
                    fprintf(stderr, "Error: unexpected end of input in parameter list\n");
                    free_param_list(params, param_count);
                    return PARSE_ERROR;
                }

                if (token->type == TOKEN_RBRACKET) {
                    advance(parser);
                    break;
                }
                if (token->type == TOKEN_SEMICOLON) {
                    advance(parser);
                    continue;
                }

                fprintf(stderr, "Error: expected ';' or ']' in parameter list\n");
                free_param_list(params, param_count);
                return PARSE_ERROR;
            }
        } else {
            advance(parser);
        }
    }

    token = current_token(parser);
    if (!token) {
        fprintf(stderr, "Error: unexpected end of input in function literal\n");
        free_param_list(params, param_count);
        return PARSE_ERROR;
    }

    if (token->type == TOKEN_RBRACE) {
        Token *closing_brace = token;
        advance(parser);
        return finish_function_literal(parser,
                                       opening_brace,
                                       closing_brace,
                                       params,
                                       param_count,
                                       NULL,
                                       0,
                                       "{}",
                                       2);
    }

    statements = xmalloc(sizeof(Qo) * capacity);
    while (1) {
        Qo statement = parse_expression(parser);
        if (is_parse_error(statement)) {
            goto func_cleanup;
        }

        if (count >= capacity) {
            capacity *= 2;
            statements = xrealloc(statements, sizeof(Qo) * capacity);
        }
        statements[count++] = statement;

        token = current_token(parser);
        if (!token) {
            fprintf(stderr, "Error: expected '}' to close function literal\n");
            goto func_cleanup;
        }

        if (token->type == TOKEN_RBRACE) {
            Token *closing_brace = token;
            advance(parser);
            return finish_function_literal(parser,
                                           opening_brace,
                                           closing_brace,
                                           params,
                                           param_count,
                                           statements,
                                           count,
                                           "{...}",
                                           5);
        }

        if (token->type != TOKEN_SEMICOLON) {
            fprintf(stderr, "Error: expected ';' or '}' in function literal\n");
            goto func_cleanup;
        }

        advance(parser);
        token = current_token(parser);
        if (!token) {
            fprintf(stderr, "Error: expected expression or '}' after ';' in function literal\n");
            goto func_cleanup;
        }

        if (token->type == TOKEN_RBRACE) {
            Token *closing_brace = token;
            advance(parser);
            return finish_function_literal(parser,
                                           opening_brace,
                                           closing_brace,
                                           params,
                                           param_count,
                                           statements,
                                           count,
                                           "{...}",
                                           5);
        }
    }

func_cleanup:
    for (int i = 0; i < count; i++) qo_release(statements[i]);
    free(statements);
    free_param_list(params, param_count);
    return PARSE_ERROR;
}

/* Parse a table literal: ([keyCol:expr;...]valCol:expr;...)
   Called after '(' has been consumed and '[' is the current token. */
static Qo parse_table_literal(Parser *parser) {
    Token *token;

    /* Consume '[' */
    advance(parser);
    token = current_token(parser);

    /* Dynamic arrays for key column names and expressions */
    int capacity = 16;
    int nKey = 0;
    Qo *keyNames = xmalloc(sizeof(Qo) * (size_t)capacity);
    Qo *keyExprs = xmalloc(sizeof(Qo) * (size_t)capacity);

    /* Parse key columns until ']' */
    while (token && token->type != TOKEN_RBRACKET) {
        if (token->type != TOKEN_IDENTIFIER) {
            fprintf(stderr, "Error: expected column name in table literal\n");
            goto table_cleanup_keys;
        }
        if (nKey >= capacity) {
            capacity *= 2;
            keyNames = xrealloc(keyNames, sizeof(Qo) * (size_t)capacity);
            keyExprs = xrealloc(keyExprs, sizeof(Qo) * (size_t)capacity);
        }
        keyNames[nKey] = make_symbol_value(token->lexeme);
        advance(parser);

        token = current_token(parser);
        if (!token || token->type != TOKEN_COLON) {
            fprintf(stderr, "Error: expected ':' after column name in table literal\n");
            qo_release(keyNames[nKey]);
            goto table_cleanup_keys;
        }
        advance(parser);

        {
            Qo expr = parse_expression(parser);
            if (is_parse_error(expr)) {
                qo_release(keyNames[nKey]);
                goto table_cleanup_keys;
            }
            keyExprs[nKey] = expr;
        }
        nKey++;

        token = current_token(parser);
        if (token && token->type == TOKEN_SEMICOLON) {
            advance(parser);
            token = current_token(parser);
        } else if (token && token->type == TOKEN_RBRACKET) {
            /* end of key columns */
        } else {
            fprintf(stderr, "Error: expected ';' or ']' in table literal\n");
            goto table_cleanup_keys;
        }
    }

    if (!token) {
        fprintf(stderr, "Error: expected ']' in table literal\n");
        goto table_cleanup_keys;
    }
    advance(parser); /* consume ']' */
    token = current_token(parser);

    /* Dynamic arrays for value column names and expressions */
    capacity = 16;
    int nVal = 0;
    Qo *valNames = xmalloc(sizeof(Qo) * (size_t)capacity);
    Qo *valExprs = xmalloc(sizeof(Qo) * (size_t)capacity);

    /* Parse value columns until ')' */
    while (token && token->type != TOKEN_RPAREN) {
        if (token->type != TOKEN_IDENTIFIER) {
            fprintf(stderr, "Error: expected column name in table literal\n");
            goto table_cleanup_vals;
        }
        if (nVal >= capacity) {
            capacity *= 2;
            valNames = xrealloc(valNames, sizeof(Qo) * (size_t)capacity);
            valExprs = xrealloc(valExprs, sizeof(Qo) * (size_t)capacity);
        }
        valNames[nVal] = make_symbol_value(token->lexeme);
        advance(parser);

        token = current_token(parser);
        if (!token || token->type != TOKEN_COLON) {
            fprintf(stderr, "Error: expected ':' after column name in table literal\n");
            qo_release(valNames[nVal]);
            goto table_cleanup_vals;
        }
        advance(parser);

        {
            Qo expr = parse_expression(parser);
            if (is_parse_error(expr)) {
                qo_release(valNames[nVal]);
                goto table_cleanup_vals;
            }
            valExprs[nVal] = expr;
        }
        nVal++;

        token = current_token(parser);
        if (token && token->type == TOKEN_SEMICOLON) {
            advance(parser);
            token = current_token(parser);
        } else if (token && token->type == TOKEN_RPAREN) {
            /* end of value columns */
        } else {
            fprintf(stderr, "Error: expected ';' or ')' in table literal\n");
            goto table_cleanup_vals;
        }
    }

    if (!token) {
        fprintf(stderr, "Error: expected ')' in table literal\n");
        goto table_cleanup_vals;
    }
    advance(parser); /* consume ')' */

    /* Build AST: (TOKEN_TABLE; namesVec; expr1; expr2; ...) */
    {
        int64_t totalCols = nKey + nVal;

        /* List of column name symbols (Qo* pointers) */
        Qo namesVec = alloc_ptr_vec(QO_SYM_VEC, totalCols);
        for (int i = 0; i < nKey; i++)
            QO_LIST_DATA(namesVec)[i] = keyNames[i];
        for (int i = 0; i < nVal; i++)
            QO_LIST_DATA(namesVec)[nKey + i] = valNames[i];

        /* Outer (TOKEN_TABLE; namesVec; keyExpr1; ...; valExpr1; ...) */
        int64_t outerLen = 2 + totalCols;
        Qo outerList = alloc_ptr_vec(QO_LIST, outerLen);
        QO_LIST_DATA(outerList)[0] = make_operator_value(TOKEN_TABLE);
        QO_LIST_DATA(outerList)[1] = namesVec;
        for (int i = 0; i < nKey; i++)
            QO_LIST_DATA(outerList)[2 + i] = keyExprs[i];
        for (int i = 0; i < nVal; i++)
            QO_LIST_DATA(outerList)[2 + nKey + i] = valExprs[i];

        free(keyNames);
        free(keyExprs);
        free(valNames);
        free(valExprs);

        return outerList;
    }

table_cleanup_vals:
    for (int i = 0; i < nVal; i++) {
        qo_release(valNames[i]);
        qo_release(valExprs[i]);
    }
    free(valNames);
    free(valExprs);

table_cleanup_keys:
    for (int i = 0; i < nKey; i++) {
        qo_release(keyNames[i]);
        qo_release(keyExprs[i]);
    }
    free(keyNames);
    free(keyExprs);
    return PARSE_ERROR;
}

static Qo parse_factor(Parser *parser) {
    Token *token = current_token(parser);
    Qo node;

    if (!token) {
        fprintf(stderr, "Error: unexpected end of input\n");
        return PARSE_ERROR;
    }

    if (token->type == TOKEN_NUMBER) {
        Qo node = parse_number_sequence(parser, token);
        if (is_parse_error(node)) return PARSE_ERROR;
        return parse_postfix_calls(parser, node);
    }

    if (token->type == TOKEN_TIMESTAMP) {
        Qo node = parse_timestamp_sequence(parser, token);
        if (is_parse_error(node)) return PARSE_ERROR;
        return parse_postfix_calls(parser, node);
    }

    if (token->type == TOKEN_TIMESPAN) {
        Qo node = parse_timespan_sequence(parser, token);
        if (is_parse_error(node)) return PARSE_ERROR;
        return parse_postfix_calls(parser, node);
    }

    if (token->type == TOKEN_BOOLEAN) {
        const char *lexeme = token->lexeme;
        int len = (int)strlen(lexeme);
        advance(parser);

        if (len == 2) {
            Qo result = make_bool_value((lexeme[0] == '1') ? 1 : 0);
            return parse_postfix_calls(parser, result);
        } else {
            int bit_count = len - 1;
            Qo result = alloc_data_vec(QO_BOOL_VEC, bit_count);
            for (int i = 0; i < bit_count; i++) {
                qo_bool_data(result)[i] = (lexeme[i] == '1') ? 1 : 0;
            }
            return parse_postfix_calls(parser, result);
        }
    }

    if (token->type == TOKEN_HEX) {
        const char *lexeme = token->lexeme;
        size_t digits = strlen(lexeme) - 2; /* strip 0x */
        advance(parser);

        if (digits == 0) {
            fprintf(stderr, "Error: hex literal must contain at least one hex digit\n");
            return PARSE_ERROR;
        }

        {
            int64_t n = (int64_t)((digits + 1) / 2);
            Qo result = alloc_data_vec(QO_BYTE_VEC, n);

            size_t src = 2;
            int64_t dst = 0;

            if ((digits % 2) != 0) {
                int lo = hex_digit_value(lexeme[src]);
                if (lo < 0) {
                    qo_release(result);
                    fprintf(stderr, "Error: invalid hex literal\n");
                    return PARSE_ERROR;
                }
                qo_byte_data(result)[dst++] = (uint8_t)lo;
                src++;
            }

            while (dst < n) {
                int hi = hex_digit_value(lexeme[src]);
                int lo = hex_digit_value(lexeme[src + 1]);
                if (hi < 0 || lo < 0) {
                    qo_release(result);
                    fprintf(stderr, "Error: invalid hex literal\n");
                    return PARSE_ERROR;
                }
                qo_byte_data(result)[dst++] = (uint8_t)((hi << 4) | lo);
                src += 2;
            }

            if (n == 1) {
                Qo scalar = make_byte_value(qo_byte_data(result)[0]);
                qo_release(result);
                return parse_postfix_calls(parser, scalar);
            }

            return parse_postfix_calls(parser, result);
        }
    }

    if (token->type == TOKEN_IDENTIFIER) {
        const char *name = token->lexeme;
        int builtin_id = builtin_name_to_id(name);
        advance(parser);
        node = builtin_id >= 0 ? make_builtin_value((uint8_t)builtin_id) : make_symbol_value(name);
        return parse_postfix_calls(parser, node);
    }

    if (is_expression_operator(token->type)) {
        TokenType op_type = token->type;
        advance(parser);
        node = make_operator_value(op_type);
        return parse_postfix_calls(parser, node);
    }

    if (token->type == TOKEN_SYMBOL) {
        Qo node = parse_symbol_sequence(parser, token);
        if (is_parse_error(node)) return PARSE_ERROR;
        return node;
    }

    if (token->type == TOKEN_STRING) {
        const char *lexeme = token->lexeme;
        int len = (int)strlen(lexeme);
        Qo result;
        advance(parser);
        result = alloc_charlike(QO_CHAR_VEC, len);
        memcpy(qo_char_data(result), lexeme, (size_t)len);
        return parse_postfix_calls(parser, result);
    }

    if (token->type == TOKEN_LPAREN) {
        /* Save position to check for table literal ([...]) */
        int saved_pos = parser->pos;
        advance(parser);
        token = current_token(parser);
        if (token && token->type == TOKEN_LBRACKET) {
            Qo table = parse_table_literal(parser);
            if (is_parse_error(table)) return PARSE_ERROR;
            return parse_postfix_calls(parser, table);
        }
        /* Not a table literal — restore position for normal parenthesized parsing */
        parser->pos = saved_pos;
        return parse_parenthesized(parser);
    }

    if (token->type == TOKEN_LBRACE) {
        return parse_function_literal(parser);
    }

    if (token->type == TOKEN_EACH) {
        advance(parser);
        return parse_postfix_calls(parser, make_adverb_value(QO_ADVERB_EACH));
    }

    fprintf(stderr, "Error: unexpected token '%s'\n", token->lexeme ? token->lexeme : "EOF");
    return PARSE_ERROR;
}

static Qo parse_expression(Parser *parser) {
    Qo left = parse_factor(parser);
    Token *op_token;

    if (is_parse_error(left)) {
        return PARSE_ERROR;
    }

    op_token = current_token(parser);

    if (op_token && is_expression_operator(op_token->type)) {
        /*
         * @, ., and ? have bracket forms (@[x], .[x], ?[x]) that can also
         * appear as a factor after an expression (juxtaposition).  When
         * these operators are immediately followed by [, treat them as a
         * factor start, not a binary operator.
         */
        int is_bracket_factor = (
            parser->pos + 1 < parser->count
            && parser->tokens[parser->pos + 1]->type == TOKEN_LBRACKET
        );

        if (!is_bracket_factor) {
            TokenType op_type = op_token->type;
            advance(parser);
            {
                Qo right = parse_expression(parser);
                if (is_parse_error(right)) {
                    qo_release(left);
                    fprintf(stderr, "Error: expected expression after operator\n");
                    return PARSE_ERROR;
                }
                {
                    Qo *binop_elements = xmalloc(sizeof(Qo) * 3);
                    binop_elements[0] = make_operator_value(op_type);
                    binop_elements[1] = left;
                    binop_elements[2] = right;
                    return qo_make_list_take(binop_elements, 3);
                }
            }
        }
    }

    if (op_token && starts_factor(op_token->type)) {
        Qo right = parse_expression(parser);
        if (is_parse_error(right)) {
            qo_release(left);
            fprintf(stderr, "Error: expected expression after function application\n");
            return PARSE_ERROR;
        }
        return make_call_value(left, right);
    }

    return left;
}

Parser *parser_new(Token **tokens, int count, const char *input) {
    Parser *parser = xmalloc(sizeof(Parser));
    parser->tokens = tokens;
    parser->count = count;
    parser->pos = 0;
    parser->input = input;
    return parser;
}

void parser_free(Parser *parser) {
    free(parser);
}

Qo parser_parse(Parser *parser) {
    int capacity = 64;
    Qo *statements = xmalloc(sizeof(Qo) * (size_t)capacity);
    int count = 0;

    while (1) {
        Token *token = current_token(parser);
        Qo statement;

        if (!token || token->type == TOKEN_EOF) {
            break;
        }

        /* Allow empty statements (bare `;` or consecutive `;;`).
           Don't advance — the separator check below will consume
           the `;`, handling all cases uniformly. */
        if (token->type == TOKEN_SEMICOLON) {
            statement = make_null_value();
        } else {
            statement = parse_expression(parser);
            if (is_parse_error(statement)) {
                Token *check = current_token(parser);
                if (!check || check->type == TOKEN_EOF) {
                    if (count == 0) {
                        free(statements);
                        return PARSE_ERROR;
                    }
                    break;
                }

                for (int i = 0; i < count; i++) {
                    qo_release(statements[i]);
                }
                free(statements);
                return PARSE_ERROR;
            }
        }

        if (count >= capacity) {
            capacity *= 2;
            statements = xrealloc(statements, sizeof(Qo) * (size_t)capacity);
        }
        statements[count++] = statement;

        token = current_token(parser);
        if (!token || token->type == TOKEN_EOF) {
            break;
        }

        if (token->type != TOKEN_SEMICOLON) {
            for (int i = 0; i < count; i++) {
                qo_release(statements[i]);
            }
            free(statements);
            fprintf(stderr, "Error: expected ';' between statements\n");
            return PARSE_ERROR;
        }

        advance(parser);
        token = current_token(parser);
        if (token && token->type == TOKEN_EOF) {
            if (count >= capacity) {
                capacity *= 2;
                statements = xrealloc(statements, sizeof(Qo) * (size_t)capacity);
            }
            statements[count++] = make_null_value();
            break;
        }
    }

    if (count == 0) {
        free(statements);
        return alloc_ptr_vec(QO_LIST, 0);
    }

    if (count == 1) {
        Qo single = statements[0];
        free(statements);
        return single;
    }

    {
        Qo *sequence_elements = xmalloc(sizeof(Qo) * ((size_t)count + 1));
        sequence_elements[0] = make_builtin_value(QO_BUILTIN_SEMICOLON);
        for (int i = 0; i < count; i++) {
            sequence_elements[i + 1] = statements[i];
        }
        free(statements);
        return qo_make_list_take(sequence_elements, count + 1);
    }
}
