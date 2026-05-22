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
        case TOKEN_PERCENT: return "%";
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
        default: return NULL;
    }
}

static int is_parse_error(Qo v) {
    return v == NULL;
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
        return NULL;
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
    return NULL;
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
            return NULL;
        }
    }
    
    /* Determine type from the last token's suffix */
    char final_suffix = tokens[count - 1]->type_suffix;
    int has_float = (final_suffix == 'f') ? 1 : 0;
    
    for (int i = 0; i < count; i++) {
        Token *t = tokens[i];
        if (is_special_int_literal(t)) {
            values[i] = make_special_int_value(t, final_suffix);
            if (values[i] == NULL) {
                for (int j = 0; j < i; j++) value_free(values[j]);
                free(values);
                free(tokens);
                return NULL;
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
                value_free(values[i]);
            }
        } else if (vec_type == QO_INT_VEC) {
            for (int i = 0; i < count; i++) {
                qo_int_data(result)[i] = qo_int(values[i]);
                value_free(values[i]);
            }
        } else if (vec_type == QO_LONG_VEC) {
            for (int i = 0; i < count; i++) {
                qo_long_data(result)[i] = qo_long(values[i]);
                value_free(values[i]);
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
                value_free(values[i]);
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

static int is_expression_operator(TokenType type) {
    return type == TOKEN_PLUS || type == TOKEN_MINUS || type == TOKEN_STAR ||
           type == TOKEN_STAR_STAR || type == TOKEN_SLASH || type == TOKEN_PERCENT || type == TOKEN_BANG ||
           type == TOKEN_COMMA || type == TOKEN_HASH || type == TOKEN_UNDERSCORE ||
           type == TOKEN_EQUAL || type == TOKEN_LESS || type == TOKEN_GREATER ||
           type == TOKEN_LE || type == TOKEN_GE ||
           type == TOKEN_PIPE || type == TOKEN_AMP ||
           type == TOKEN_COLON || type == TOKEN_AT || type == TOKEN_DOT ||
           type == TOKEN_DOT_DOT || type == TOKEN_DOT_DOT_EQ;
}

static int is_callable_operator(TokenType type) {
    return is_expression_operator(type);
}

static int starts_factor(TokenType type) {
    return type == TOKEN_NUMBER ||
           type == TOKEN_BOOLEAN ||
           type == TOKEN_HEX ||
           type == TOKEN_IDENTIFIER ||
           type == TOKEN_SYMBOL ||
           type == TOKEN_STRING ||
           type == TOKEN_LPAREN ||
           type == TOKEN_LBRACE ||
           type == TOKEN_EACH;
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
        int capacity = 4;
        int count = 0;
        Qo *args;

        advance(parser);
        args = xmalloc(sizeof(Qo) * capacity);
        token = current_token(parser);
        if (!token) {
            free(args);
            value_free(base);
            fprintf(stderr, "Error: unexpected end of input in function call\n");
            return NULL;
        }

        if (token->type == TOKEN_RBRACKET) {
            Qo *call_elements = xmalloc(sizeof(Qo) * 2);
            advance(parser);
            call_elements[0] = base;
            call_elements[1] = make_null_value();
            free(args);
            base = qo_make_list_take(call_elements, 2);
            token = current_token(parser);
            continue;
        }

        {
            int expecting_slot = 1;
            while (1) {
                token = current_token(parser);
                if (!token) {
                    for (int i = 0; i < count; i++) {
                        value_free(args[i]);
                    }
                    free(args);
                    value_free(base);
                    fprintf(stderr, "Error: unexpected end of input in function call\n");
                    return NULL;
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
                        Token *check = current_token(parser);
                        if (!check || (check->type != TOKEN_RBRACKET && check->type != TOKEN_SEMICOLON)) {
                            for (int i = 0; i < count; i++) {
                                value_free(args[i]);
                            }
                            free(args);
                            value_free(base);
                            return NULL;
                        }
                    }

                    if (count >= capacity) {
                        capacity *= 2;
                        args = xrealloc(args, sizeof(Qo) * capacity);
                    }
                    args[count++] = arg;
                    expecting_slot = 0;

                    token = current_token(parser);
                    if (!token) {
                        for (int i = 0; i < count; i++) {
                            value_free(args[i]);
                        }
                        free(args);
                        value_free(base);
                        fprintf(stderr, "Error: unexpected end of input in function call\n");
                        return NULL;
                    }

                    if (token->type == TOKEN_SEMICOLON) {
                        advance(parser);
                        expecting_slot = 1;
                        continue;
                    }

                    if (token->type == TOKEN_RBRACKET) {
                        continue;
                    }

                    for (int i = 0; i < count; i++) {
                        value_free(args[i]);
                    }
                    free(args);
                    value_free(base);
                    fprintf(stderr, "Error: expected ';' or ']' in function call\n");
                    return NULL;
                }
            }
        }

        token = current_token(parser);
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
        return NULL;
    }

    if (token->type == TOKEN_RPAREN) {
        Qo empty = alloc_ptr_vec(QO_LIST, 0);
        advance(parser);
        return parse_postfix_calls(parser, empty);
    }

    first = parse_expression(parser);
    if (is_parse_error(first)) {
        Token *check = current_token(parser);
        if (!check) {
            fprintf(stderr, "Error: unexpected end of input after '('\n");
            return NULL;
        }
    }

    token = current_token(parser);
    if (!token) {
        value_free(first);
        fprintf(stderr, "Error: unexpected end of input after '('\n");
        return NULL;
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
                Token *check = current_token(parser);
                if (!check || (check->type != TOKEN_SEMICOLON && check->type != TOKEN_RPAREN)) {
                    for (int i = 0; i < count; i++) {
                        value_free(elements[i]);
                    }
                    free(elements);
                    return NULL;
                }
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
                value_free(elements[i]);
            }
            free(elements);
            fprintf(stderr, "Error: expected ')' to close list literal\n");
            return NULL;
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

    value_free(first);
    fprintf(stderr, "Error: expected ')' or ';' after expression in parentheses\n");
    return NULL;
}

static Qo parse_function_literal(Parser *parser) {
    Token *token;
    int capacity = 8;
    int count = 0;
    Qo *statements;
    Token *opening_brace;
    char **params = NULL;
    int param_count = 0;

    opening_brace = current_token(parser);
    advance(parser);
    token = current_token(parser);
    if (!token) {
        fprintf(stderr, "Error: unexpected end of input in function literal\n");
        return NULL;
    }

    if (token->type == TOKEN_LBRACKET) {
        int param_capacity = 8;
        advance(parser);
        params = xmalloc(sizeof(char *) * param_capacity);

        token = current_token(parser);
        if (!token) {
            fprintf(stderr, "Error: unexpected end of input in parameter list\n");
            free(params);
            return NULL;
        }

        if (token->type != TOKEN_RBRACKET) {
            while (1) {
                token = current_token(parser);
                if (!token) {
                    fprintf(stderr, "Error: unexpected end of input in parameter list\n");
                    free_param_list(params, param_count);
                    return NULL;
                }

                if (token->type != TOKEN_IDENTIFIER) {
                    fprintf(stderr, "Error: expected identifier in parameter list\n");
                    free_param_list(params, param_count);
                    return NULL;
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
                    return NULL;
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
                return NULL;
            }
        } else {
            advance(parser);
        }
    }

    token = current_token(parser);
    if (!token) {
        fprintf(stderr, "Error: unexpected end of input in function literal\n");
        free_param_list(params, param_count);
        return NULL;
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
            Token *check = current_token(parser);
            if (!check || (check->type != TOKEN_SEMICOLON && check->type != TOKEN_RBRACE)) {
                for (int i = 0; i < count; i++) {
                    value_free(statements[i]);
                }
                free(statements);
                free_param_list(params, param_count);
                return NULL;
            }
        }

        if (count >= capacity) {
            capacity *= 2;
            statements = xrealloc(statements, sizeof(Qo) * capacity);
        }
        statements[count++] = statement;

        token = current_token(parser);
        if (!token) {
            for (int i = 0; i < count; i++) {
                value_free(statements[i]);
            }
            free(statements);
            free_param_list(params, param_count);
            fprintf(stderr, "Error: expected '}' to close function literal\n");
            return NULL;
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
            for (int i = 0; i < count; i++) {
                value_free(statements[i]);
            }
            free(statements);
            free_param_list(params, param_count);
            fprintf(stderr, "Error: expected ';' or '}' in function literal\n");
            return NULL;
        }

        advance(parser);
        token = current_token(parser);
        if (!token) {
            for (int i = 0; i < count; i++) {
                value_free(statements[i]);
            }
            free(statements);
            free_param_list(params, param_count);
            fprintf(stderr, "Error: expected expression or '}' after ';' in function literal\n");
            return NULL;
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
}

static Qo parse_factor(Parser *parser) {
    Token *token = current_token(parser);
    Qo node;

    if (!token) {
        fprintf(stderr, "Error: unexpected end of input\n");
        return NULL;
    }

    if (token->type == TOKEN_NUMBER) {
        Qo node = parse_number_sequence(parser, token);
        if (is_parse_error(node)) return NULL;
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
            return NULL;
        }

        {
            int64_t n = (int64_t)((digits + 1) / 2);
            Qo result = alloc_data_vec(QO_BYTE_VEC, n);

            size_t src = 2;
            int64_t dst = 0;

            if ((digits % 2) != 0) {
                int lo = hex_digit_value(lexeme[src]);
                if (lo < 0) {
                    value_free(result);
                    fprintf(stderr, "Error: invalid hex literal\n");
                    return NULL;
                }
                qo_byte_data(result)[dst++] = (uint8_t)lo;
                src++;
            }

            while (dst < n) {
                int hi = hex_digit_value(lexeme[src]);
                int lo = hex_digit_value(lexeme[src + 1]);
                if (hi < 0 || lo < 0) {
                    value_free(result);
                    fprintf(stderr, "Error: invalid hex literal\n");
                    return NULL;
                }
                qo_byte_data(result)[dst++] = (uint8_t)((hi << 4) | lo);
                src += 2;
            }

            if (n == 1) {
                Qo scalar = make_byte_value(qo_byte_data(result)[0]);
                value_free(result);
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

    if (is_callable_operator(token->type)) {
        TokenType op_type = token->type;
        advance(parser);
        node = make_operator_value(op_type);
        return parse_postfix_calls(parser, node);
    }

    if (token->type == TOKEN_SYMBOL) {
        Qo *literal_elements = xmalloc(sizeof(Qo));
        advance(parser);
        literal_elements[0] = make_symbol_value(token->lexeme);
        return parse_postfix_calls(parser, qo_make_list_take(literal_elements, 1));
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
    return NULL;
}

static Qo parse_expression(Parser *parser) {
    Qo left = parse_factor(parser);
    Token *op_token;

    if (is_parse_error(left)) {
        Token *check = current_token(parser);
        if (!check) {
            return NULL;
        }
    }

    op_token = current_token(parser);

    if (op_token && is_expression_operator(op_token->type)) {
        TokenType op_type = op_token->type;
        advance(parser);
        {
            Qo right = parse_expression(parser);
            if (is_parse_error(right)) {
                Token *check = current_token(parser);
                if (!check) {
                    value_free(left);
                    fprintf(stderr, "Error: expected expression after operator\n");
                    return NULL;
                }
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

    if (op_token && starts_factor(op_token->type)) {
        Qo right = parse_expression(parser);
        if (is_parse_error(right)) {
            Token *check = current_token(parser);
            if (!check) {
                value_free(left);
                fprintf(stderr, "Error: expected expression after function application\n");
                return NULL;
            }
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

        statement = parse_expression(parser);
        if (is_parse_error(statement)) {
            Token *check = current_token(parser);
            if (!check || check->type == TOKEN_EOF) {
                if (count == 0) {
                    free(statements);
                    return NULL;
                }
                break;
            }

            for (int i = 0; i < count; i++) {
                value_free(statements[i]);
            }
            free(statements);
            return NULL;
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
                value_free(statements[i]);
            }
            free(statements);
            fprintf(stderr, "Error: expected ';' between statements\n");
            return NULL;
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
        return make_null_value();
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
