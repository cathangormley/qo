#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "evaluator_internal.h"
#include "internal.h"
#include "lexer.h"
#include "parser.h"
#include "ipc.h"
#include "symbol_intern.h"

/* forward declaration: eval_apply_keyword and eval_apply_value are mutually recursive */
static Qo eval_apply_value(Qo head, Qo *args, int arg_count, Environment *env);

/* ── parse helpers ───────────────────────────────────────────────────────── */

static int is_parse_builtin_or_special(const char *name) {
    TokenType op;
    if (operator_name_to_token(name, &op)) return 1;
        return strcmp(name,"sum")==0 || strcmp(name,"count")==0 || strcmp(name,"min")==0 ||
            strcmp(name,"max")==0 || strcmp(name,"parse")==0 || strcmp(name,"lex")==0 ||
           strcmp(name,"type")==0 || strcmp(name,"key")==0  || strcmp(name,"value")==0 ||
           strcmp(name,"print")==0|| strcmp(name,"exit")==0 || strcmp(name,"enlist")==0 ||
           strcmp(name,"eval")==0 || strcmp(name,"first")==0|| strcmp(name,"last")==0  ||
           strcmp(name,"now")==0  || strcmp(name,"read")==0 || strcmp(name,"shell")==0 ||
            strcmp(name,"ser")==0  || strcmp(name,"deser")==0 || strcmp(name,"refcount")==0 || strcmp(name,"til")==0 ||
            strcmp(name,"listen")==0 || strcmp(name,"hclose")==0 || strcmp(name,"hopen")==0 ||
            strcmp(name,"find")==0 ||
             strcmp(name,";")==0;
}

static Qo normalize_parsed_tree(Qo tree) {
    if (tree == NULL) return NULL;
    if (qo_type(tree) != QO_LIST) return value_copy(tree);
    int64_t n = qo_count(tree);
    Qo result = alloc_ptr_vec(QO_LIST, n);
    for (int64_t i = 0; i < n; i++) {
        qo_ptr_data(result)[i] = normalize_parsed_tree(qo_ptr_data(tree)[i]);
    }
    if (n > 0) {
        Qo h = qo_ptr_data(result)[0];
        if (h != NULL && qo_type(h) == QO_KEYWORD &&
            !is_parse_builtin_or_special((char *)qo_char_data(h))) {
            Qo sym = make_symbol_value((char *)qo_char_data(h));
            value_free(qo_ptr_data(result)[0]);
            qo_ptr_data(result)[0] = sym;
        }
    }
    return result;
}

static Qo parse_source_to_value(const char *input) {
    TokenBuffer buffer = tokenize_input(input);
    Parser *parser = parser_new(buffer.tokens, buffer.count, input);
    Qo tree = parser_parse(parser);
    parser_free(parser);
    free_token_buffer(buffer);
    return tree;
}

/* ── CHAR_VEC → C string helper ──────────────────────────────────────────── */

static char *charvec_to_cstr(Qo arg) {
    int64_t len = qo_count(arg);
    char *s = xmalloc((size_t)len + 1);
    memcpy(s, qo_char_data(arg), (size_t)len);
    s[len] = '\0';
    return s;
}

/* ── builtins ────────────────────────────────────────────────────────────── */

static Qo eval_builtin_sum(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) EVAL_ERROR("sum expects a number or numeric vector");
    uint8_t t = qo_type(arg);
    if (t == QO_SHORT || t == QO_INT || t == QO_LONG || t == QO_FLOAT) return value_copy(arg);
    if (t == QO_SHORT_VEC) {
        int64_t total = 0;
        int64_t n = qo_count(arg);
        for (int64_t i = 0; i < n; i++) total += (int64_t)qo_short_data(arg)[i];
        return make_long_value(total);
    }
    if (t == QO_INT_VEC) {
        int64_t total = 0;
        int64_t n = qo_count(arg);
        for (int64_t i = 0; i < n; i++) total += (int64_t)qo_int_data(arg)[i];
        return make_long_value(total);
    }
    if (t == QO_LONG_VEC) {
        int64_t total = 0;
        int64_t n = qo_count(arg);
        for (int64_t i = 0; i < n; i++) total += qo_long_data(arg)[i];
        return make_long_value(total);
    }
    if (t == QO_FLOAT_VEC) {
        double total = 0.0;
        int64_t n = qo_count(arg);
        for (int64_t i = 0; i < n; i++) total += qo_float_data(arg)[i];
        return make_float_value(total);
    }
    if (t == QO_LIST) {
        int use_float = 0;
        double total = 0.0;
        int64_t n = qo_count(arg);
        for (int64_t i = 0; i < n; i++) {
            Qo e = qo_ptr_data(arg)[i];
            if (e == NULL) EVAL_ERROR("sum expects a number or numeric vector");
            uint8_t et = qo_type(e);
            if (et == QO_SHORT)      total += (double)qo_short(e);
            else if (et == QO_INT)   total += (double)qo_int(e);
            else if (et == QO_LONG)  total += (double)qo_long(e);
            else if (et == QO_FLOAT) { total += qo_float(e); use_float = 1; }
            else EVAL_ERROR("sum expects a number or numeric vector");
        }
        return use_float ? make_float_value(total) : make_long_value((long)total);
    }
    EVAL_ERROR("sum expects a number or numeric vector");
}

static Qo eval_builtin_parse(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_CHAR_VEC) EVAL_ERROR("parse expects a CHAR vector argument");
    char *input = charvec_to_cstr(arg);
    Qo result = parse_source_to_value(input);
    free(input);
    if (result != NULL) {
        Qo normalized = normalize_parsed_tree(result);
        value_free(result);
        result = normalized;
    }
    return result;
}

static Qo eval_builtin_lex(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_CHAR_VEC) EVAL_ERROR("lex expects a CHAR vector argument");
    char *input = charvec_to_cstr(arg);
    TokenBuffer buffer = tokenize_input(input);
    free(input);
    
    int token_count = 0;
    for (int i = 0; i < buffer.count; i++) {
        if (buffer.tokens[i]->type != TOKEN_EOF) {
            token_count++;
        }
    }
    
    Qo result = alloc_ptr_vec(QO_LIST, (int64_t)token_count);
    Qo *result_data = (Qo *)qo_ptr_data(result);
    
    int out_idx = 0;
    for (int i = 0; i < buffer.count; i++) {
        Token *tok = buffer.tokens[i];
        if (tok->type == TOKEN_EOF) continue;
        
        const char *text = tok->lexeme ? tok->lexeme : "";
        size_t len = strlen(text);
        int needs_symbol_prefix = tok->type == TOKEN_SYMBOL;
        Qo lexeme = alloc_charlike(QO_CHAR_VEC, (int64_t)(len + (needs_symbol_prefix ? 1 : 0)));
        if (needs_symbol_prefix) {
            qo_char_data(lexeme)[0] = '`';
            memcpy(qo_char_data(lexeme) + 1, text, len);
        } else if (tok->lexeme) {
            memcpy(qo_char_data(lexeme), text, len);
        }
        result_data[out_idx++] = lexeme;
    }
    
    free_token_buffer(buffer);
    return result;
}

static Qo eval_builtin_not(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) EVAL_ERROR("not expects a numeric argument");
    
    uint8_t t = qo_type(arg);
    if (is_numeric_scalar_type(t)) {
        double val = value_as_double(arg);
        return make_bool_value(val == 0.0);
    }
    
    if (is_numeric_vector_type(t)) {
        int64_t n = qo_count(arg);
        Qo result = alloc_data_vec(QO_BOOL_VEC, n);
        for (int64_t i = 0; i < n; i++) {
            int zero;
            if (t == QO_SHORT_VEC) zero = (qo_short_data(arg)[i] == 0);
            else if (t == QO_INT_VEC) zero = (qo_int_data(arg)[i] == 0);
            else if (t == QO_LONG_VEC) zero = (qo_long_data(arg)[i] == 0);
            else if (t == QO_FLOAT_VEC) zero = (qo_float_data(arg)[i] == 0.0);
            else zero = (qo_bool_data(arg)[i] == 0);
            qo_bool_data(result)[i] = zero ? 1 : 0;
        }
        return result;
    }
    
    if (t == QO_LIST) {
        int64_t n = qo_count(arg);
        Qo result = alloc_data_vec(QO_BOOL_VEC, n);
        for (int64_t i = 0; i < n; i++) {
            Qo elem = qo_ptr_data(arg)[i];
            if (elem == NULL) {
                qo_release(result);
                EVAL_ERROR("not expects numeric arguments");
            }
            double val = value_as_double(elem);
            qo_bool_data(result)[i] = (val == 0.0) ? 1 : 0;
        }
        return result;
    }
    
    EVAL_ERROR("not expects a numeric argument");
}

static Qo eval_builtin_count(Qo arg, Environment *env) {
    (void)env;
    if (arg != NULL && is_vector_type(qo_type(arg))) return make_int_value((int32_t)qo_count(arg));
    EVAL_ERROR("count expects a list or vector argument");
}

static Qo eval_builtin_min(Qo arg, Environment *env) {
    int64_t n;
    (void)env;
    if (arg == NULL) EVAL_ERROR("min expects a number or numeric vector");

    switch (qo_type(arg)) {
        case QO_SHORT:
        case QO_INT:
        case QO_LONG:
        case QO_FLOAT:
            return value_copy(arg);
        case QO_SHORT_VEC: {
            int16_t m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("min on empty vector");
            m = qo_short_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_short_data(arg)[i] < m) m = qo_short_data(arg)[i];
            return make_short_value(m);
        }
        case QO_INT_VEC: {
            int32_t m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("min on empty vector");
            m = qo_int_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_int_data(arg)[i] < m) m = qo_int_data(arg)[i];
            return make_int_value(m);
        }
        case QO_LONG_VEC: {
            int64_t m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("min on empty vector");
            m = qo_long_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_long_data(arg)[i] < m) m = qo_long_data(arg)[i];
            return make_long_value(m);
        }
        case QO_FLOAT_VEC: {
            double m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("min on empty vector");
            m = qo_float_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_float_data(arg)[i] < m) m = qo_float_data(arg)[i];
            return make_float_value(m);
        }
        case QO_LIST: {
            int use_float = 0;
            double m = 0.0;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("min on empty list");
            for (int64_t i = 0; i < n; i++) {
                Qo e = qo_ptr_data(arg)[i];
                double v;
                if (e == NULL) EVAL_ERROR("min expects a number or numeric vector");
                if (qo_type(e) == QO_SHORT) v = (double)qo_short(e);
                else if (qo_type(e) == QO_INT) v = (double)qo_int(e);
                else if (qo_type(e) == QO_LONG) v = (double)qo_long(e);
                else if (qo_type(e) == QO_FLOAT) { v = qo_float(e); use_float = 1; }
                else EVAL_ERROR("min expects a number or numeric vector");
                if (i == 0 || v < m) m = v;
            }
            return use_float ? make_float_value(m) : make_long_value((int64_t)m);
        }
        default:
            EVAL_ERROR("min expects a number or numeric vector");
    }
}

static Qo eval_builtin_max(Qo arg, Environment *env) {
    int64_t n;
    (void)env;
    if (arg == NULL) EVAL_ERROR("max expects a number or numeric vector");

    switch (qo_type(arg)) {
        case QO_SHORT:
        case QO_INT:
        case QO_LONG:
        case QO_FLOAT:
            return value_copy(arg);
        case QO_SHORT_VEC: {
            int16_t m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("max on empty vector");
            m = qo_short_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_short_data(arg)[i] > m) m = qo_short_data(arg)[i];
            return make_short_value(m);
        }
        case QO_INT_VEC: {
            int32_t m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("max on empty vector");
            m = qo_int_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_int_data(arg)[i] > m) m = qo_int_data(arg)[i];
            return make_int_value(m);
        }
        case QO_LONG_VEC: {
            int64_t m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("max on empty vector");
            m = qo_long_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_long_data(arg)[i] > m) m = qo_long_data(arg)[i];
            return make_long_value(m);
        }
        case QO_FLOAT_VEC: {
            double m;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("max on empty vector");
            m = qo_float_data(arg)[0];
            for (int64_t i = 1; i < n; i++) if (qo_float_data(arg)[i] > m) m = qo_float_data(arg)[i];
            return make_float_value(m);
        }
        case QO_LIST: {
            int use_float = 0;
            double m = 0.0;
            n = qo_count(arg);
            if (n == 0) EVAL_ERROR("max on empty list");
            for (int64_t i = 0; i < n; i++) {
                Qo e = qo_ptr_data(arg)[i];
                double v;
                if (e == NULL) EVAL_ERROR("max expects a number or numeric vector");
                if (qo_type(e) == QO_SHORT) v = (double)qo_short(e);
                else if (qo_type(e) == QO_INT) v = (double)qo_int(e);
                else if (qo_type(e) == QO_LONG) v = (double)qo_long(e);
                else if (qo_type(e) == QO_FLOAT) { v = qo_float(e); use_float = 1; }
                else EVAL_ERROR("max expects a number or numeric vector");
                if (i == 0 || v > m) m = v;
            }
            return use_float ? make_float_value(m) : make_long_value((int64_t)m);
        }
        default:
            EVAL_ERROR("max expects a number or numeric vector");
    }
}

static Qo eval_builtin_til(Qo arg, Environment *env) {
    int64_t n;
    Qo out;
    (void)env;

    if (arg == NULL) EVAL_ERROR("til expects a numeric argument");
    if (qo_type(arg) == QO_SHORT) n = (int64_t)qo_short(arg);
    else if (qo_type(arg) == QO_INT) n = (int64_t)qo_int(arg);
    else if (qo_type(arg) == QO_LONG) n = qo_long(arg);
    else EVAL_ERROR("til expects a short, int, or long argument");

    if (n < 0) EVAL_ERROR("til expects a non-negative argument");

    out = alloc_data_vec(QO_LONG_VEC, n);
    for (int64_t i = 0; i < n; i++) qo_long_data(out)[i] = i;
    return out;
}

static Qo eval_builtin_refcount(Qo arg, Environment *env) {
    uint32_t rc;
    (void)env;
    if (arg == NULL) return make_long_value(0);
    rc = qo_refcount(arg);
    /* Expose stable ownership count, excluding the transient call argument ref. */
    if (rc > 0) rc -= 1;
    return make_long_value((int64_t)rc);
}

static Qo eval_builtin_type(Qo arg, Environment *env) {
    (void)env;
    const char *tag = "unknown";
    if (arg == NULL) { tag = "null"; }
    else switch (qo_type(arg)) {
        case QO_SHORT:      tag = "short";     break;
        case QO_INT:        tag = "int";       break;
        case QO_LONG:       tag = "long";      break;
        case QO_SHORT_VEC:  tag = "SHORT";     break;
        case QO_INT_VEC:    tag = "INT";       break;
        case QO_LONG_VEC:   tag = "LONG";      break;
        case QO_FLOAT:      tag = "float";     break;
        case QO_FLOAT_VEC:  tag = "FLOAT";     break;
        case QO_CHAR:       tag = "char";      break;
        case QO_CHAR_VEC:   tag = "CHAR";      break;
        case QO_BOOL:       tag = "bool";      break;
        case QO_BOOL_VEC:   tag = "BOOL";      break;
        case QO_BYTE:       tag = "byte";      break;
        case QO_BYTE_VEC:   tag = "BYTE";      break;
        case QO_PROJECTOR:  tag = "projector"; break;
        case QO_PROJECTION: tag = "projection";break;
        case QO_SYMBOL:     tag = "symbol";    break;
        case QO_SYM_VEC:    tag = "SYMBOL";    break;
        case QO_DICT:       tag = "dict";      break;
        case QO_KEYWORD:    tag = "keyword";   break;
        case QO_LIST:       tag = "list";      break;
        case QO_FUNCTION:   tag = "function";  break;
        case QO_EACHED:     tag = "each";      break;
        default: break;
    }
    return make_symbol_value(tag);
}

static Qo eval_builtin_key(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_DICT) EVAL_ERROR("key expects a dictionary");
    return extract_dict_side(arg, 1);
}

static Qo eval_builtin_value_fn(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_DICT) EVAL_ERROR("value expects a dictionary");
    return extract_dict_side(arg, 0);
}

static Qo eval_builtin_exit(Qo arg, Environment *env) {
    (void)env;
    Qo a = arg;
    if (a == NULL) EVAL_ERROR("exit expects a numeric argument");
    if (qo_type(a) == QO_SHORT) { evaluator_request_exit(qo_short(a));       return value_copy(a); }
    if (qo_type(a) == QO_INT)   { evaluator_request_exit(qo_int(a));         return value_copy(a); }
    if (qo_type(a) == QO_LONG)  { evaluator_request_exit((long)qo_long(a));  return value_copy(a); }
    if (qo_type(a) == QO_FLOAT) { evaluator_request_exit((long)qo_float(a)); return value_copy(a); }
    EVAL_ERROR("exit expects a numeric argument");
}

static int enlist_scalar_vec_type(uint8_t scalar_type, uint8_t *out_vec_type) {
    if (scalar_type == QO_SHORT) {
        *out_vec_type = QO_SHORT_VEC;
        return 1;
    }
    if (scalar_type == QO_INT) {
        *out_vec_type = QO_INT_VEC;
        return 1;
    }
    if (scalar_type == QO_LONG) {
        *out_vec_type = QO_LONG_VEC;
        return 1;
    }
    if (scalar_type == QO_FLOAT) {
        *out_vec_type = QO_FLOAT_VEC;
        return 1;
    }
    if (scalar_type == QO_CHAR) {
        *out_vec_type = QO_CHAR_VEC;
        return 1;
    }
    if (scalar_type == QO_BOOL) {
        *out_vec_type = QO_BOOL_VEC;
        return 1;
    }
    if (scalar_type == QO_BYTE) {
        *out_vec_type = QO_BYTE_VEC;
        return 1;
    }
    if (scalar_type == QO_SYMBOL) {
        *out_vec_type = QO_SYM_VEC;
        return 1;
    }
    return 0;
}

static Qo eval_builtin_enlist(Qo *args, int arg_count, Environment *env) {
    (void)env;
    if (arg_count > 0 && args[0] != NULL) {
        uint8_t scalar_type = qo_type(args[0]);
        uint8_t vec_type;
        int all_same = 1;

        for (int i = 1; i < arg_count; i++) {
            if (args[i] == NULL || qo_type(args[i]) != scalar_type) {
                all_same = 0;
                break;
            }
        }

        if (all_same && enlist_scalar_vec_type(scalar_type, &vec_type)) {
            if (vec_type == QO_CHAR_VEC) {
                Qo result = alloc_charlike(QO_CHAR_VEC, (int64_t)arg_count);
                for (int i = 0; i < arg_count; i++) qo_char_data(result)[i] = qo_char(args[i]);
                return result;
            }

            if (vec_type == QO_SYM_VEC) {
                Qo result = alloc_ptr_vec(QO_SYM_VEC, (int64_t)arg_count);
                for (int i = 0; i < arg_count; i++) QO_LIST_DATA(result)[i] = value_copy(args[i]);
                return result;
            }

            {
                Qo result = alloc_data_vec(vec_type, (int64_t)arg_count);
                for (int i = 0; i < arg_count; i++) {
                    if (vec_type == QO_SHORT_VEC) qo_short_data(result)[i] = qo_short(args[i]);
                    else if (vec_type == QO_INT_VEC) qo_int_data(result)[i] = qo_int(args[i]);
                    else if (vec_type == QO_LONG_VEC) qo_long_data(result)[i] = qo_long(args[i]);
                    else if (vec_type == QO_FLOAT_VEC) qo_float_data(result)[i] = qo_float(args[i]);
                    else if (vec_type == QO_BOOL_VEC) qo_bool_data(result)[i] = qo_bool(args[i]);
                    else qo_byte_data(result)[i] = qo_byte(args[i]);
                }
                return result;
            }
        }
    }

    {
        Qo result = alloc_ptr_vec(QO_LIST, (int64_t)arg_count);
        for (int i = 0; i < arg_count; i++) QO_LIST_DATA(result)[i] = value_copy(args[i]);
        return result;
    }
}

static Qo eval_builtin_print(Qo arg, Environment *env) {
    (void)env;
    Qo a = arg;
    if (a != NULL && qo_type(a) == QO_CHAR_VEC) {
        int64_t n = qo_count(a);
        for (int64_t i = 0; i < n; i++) printf("%c", qo_char_data(a)[i]);
        printf("\n");
    } else if (a != NULL && qo_type(a) == QO_CHAR) {
        printf("%c\n", qo_char(a));
    } else {
        EVAL_ERROR("print expects a string or char argument");
    }
    return make_null_value();
}

Qo eval_value(Qo tree, Environment *env);

static Qo eval_builtin_eval(Qo arg, Environment *env) {
    return eval_value(arg, env);
}

static Qo eval_builtin_first(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) EVAL_ERROR("first on null");
    if (is_vector_type(qo_type(arg))) {
        if (qo_count(arg) == 0) EVAL_ERROR("first on empty vector");
        return dict_elem_copy(arg, 0);
    }
    return value_copy(arg);
}

static Qo eval_builtin_last(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) EVAL_ERROR("last on null");
    if (is_vector_type(qo_type(arg))) {
        int64_t n = qo_count(arg);
        if (n == 0) EVAL_ERROR("last on empty vector");
        return dict_elem_copy(arg, n - 1);
    }
    return value_copy(arg);
}

static Qo eval_builtin_now(Qo arg, Environment *env) {
    struct timespec ts;
    int64_t now_ns;
    (void)arg; (void)env;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) EVAL_ERROR("now: failed to read system clock");
    now_ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    return make_long_value(now_ns);
}

static Qo eval_builtin_read(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_CHAR_VEC) EVAL_ERROR("read expects a string argument");
    char *path = charvec_to_cstr(arg);
    char *contents = read_file_contents(path);
    free(path);
    if (!contents) { evaluator_request_error(); return NULL; }
    int64_t len = (int64_t)strlen(contents);
    Qo result = alloc_charlike(QO_CHAR_VEC, len);
    memcpy(qo_char_data(result), contents, (size_t)len);
    free(contents);
    return result;
}

static Qo eval_builtin_shell(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_CHAR_VEC) EVAL_ERROR("shell expects a string argument");
    char *command = charvec_to_cstr(arg);
    FILE *fp = popen(command, "r");
    free(command);
    if (!fp) EVAL_ERROR("shell: failed to execute command");
    char *output = xmalloc(4096);
    int output_len = 0, output_cap = 4096, ch;
    while ((ch = fgetc(fp)) != EOF) {
        if (output_len >= output_cap - 1) {
            output_cap *= 2;
            output = xrealloc(output, output_cap);
        }
        output[output_len++] = (char)ch;
    }
    if (ferror(fp)) { pclose(fp); free(output); EVAL_ERROR("shell: error reading command output"); }
    pclose(fp);
    if (output_len > 0 && output[output_len-1] == '\n') output_len--;
    Qo result = alloc_charlike(QO_CHAR_VEC, (int64_t)output_len);
    memcpy(qo_char_data(result), output, (size_t)output_len);
    free(output);
    return result;
}

static Qo eval_builtin_ser(Qo arg, Environment *env) {
    (void)env;
    return ipc_serialize(arg);
}

static Qo eval_builtin_deser(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_BYTE_VEC) EVAL_ERROR("deser expects a byte vector");
    Qo result = ipc_deserialize(qo_byte_data(arg), (size_t)qo_count(arg));
    if (result == NULL) EVAL_ERROR("failed to deserialize value");
    return result;
}

static Qo eval_builtin_listen(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || (!is_numeric_scalar_type(qo_type(arg)))) EVAL_ERROR("listen expects a numeric port");
    int port = (int)value_as_double(arg);
    if (ipc_listen(port) < 0) EVAL_ERROR("failed to listen on port");
    return make_null_value();
}

static Qo eval_builtin_hclose(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_INT) EVAL_ERROR("hclose expects an int handle");
    ipc_close(qo_int(arg));
    return make_null_value();
}

static Qo eval_builtin_find(Qo haystack, Qo needles, Environment *env) {
    (void)env;
    if (haystack == NULL) EVAL_ERROR("find: haystack cannot be null");
    uint8_t ht = qo_type(haystack);
    if (!is_vector_type(ht)) EVAL_ERROR("find: haystack must be a vector");

    int64_t n = qo_count(haystack);

    /* Determine needle count and whether result should be scalar.
       Iterate if it's a list or numeric vector (SHORT/INT/LONG/FLOAT).
       Treat CHAR/BYTE/BOOL vectors and scalar atoms as a single needle. */
    uint8_t nt = needles ? qo_type(needles) : 0;
    int needle_count;
    int result_is_scalar;
    int iter_needles = (nt == QO_LIST || nt == QO_SHORT_VEC || nt == QO_INT_VEC ||
                        nt == QO_LONG_VEC || nt == QO_FLOAT_VEC || nt == QO_SYM_VEC);
    if (needles == NULL || !iter_needles) {
        needle_count = 1;
        result_is_scalar = 1;
    } else {
        needle_count = (int)qo_count(needles);
        result_is_scalar = 0;
    }

    Qo result = alloc_data_vec(QO_LONG_VEC, needle_count);

    for (int i = 0; i < needle_count; i++) {
        Qo needle;
        int own_needle = 0;
        if (needles == NULL) {
            needle = NULL;
        } else if (iter_needles) {
            needle = dict_elem_copy(needles, i);
            own_needle = 1;
        } else {
            needle = needles;
        }

        int64_t idx = n;
        for (int64_t j = 0; j < n; j++) {
            Qo elem = dict_elem_copy(haystack, j);
            if (value_equals(elem, needle)) {
                idx = j;
                qo_release(elem);
                break;
            }
            qo_release(elem);
        }

        qo_long_data(result)[i] = idx;
        if (own_needle) qo_release(needle);
    }

    if (result_is_scalar) {
        int64_t val = qo_long_data(result)[0];
        value_free(result);
        return make_long_value(val);
    }
    return result;
}

static Qo null_for_vector_type(uint8_t vec_type) {
    switch (vec_type) {
        case QO_LONG_VEC:  return make_long_value(QO_LONG_NULL);
        case QO_INT_VEC:   return make_int_value(QO_INT_NULL);
        case QO_SHORT_VEC: return make_short_value(QO_SHORT_NULL);
        case QO_FLOAT_VEC: return make_float_value(QO_FLOAT_NULL);
        case QO_CHAR_VEC:  return make_char_value(0);
        case QO_BOOL_VEC:  return make_bool_value(0);
        case QO_BYTE_VEC:  return make_byte_value(0);
        default:           return NULL;
    }
}

static Qo eval_apply_keyword(Qo head, Qo *arg_values, int arg_count, Environment *env) {
    const char *verb = QO_STR(head);
    Qo result;
    TokenType op_token;

    if (operator_name_to_token(verb, &op_token)) {
        if (arg_count != 2) {
            EVAL_ERROR_FMT("%s expects exactly 2 arguments, got %d", verb, arg_count);
        }
        if      (op_token == TOKEN_BANG)       return eval_dict_creation(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_COMMA)      return eval_comma_binop(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_HASH)       return eval_take(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_UNDERSCORE) return eval_drop(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_EQUAL)      return eval_equals(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_LESS)       return eval_less(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_GREATER)    return eval_greater(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_LE)         return eval_lte(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_GE)         return eval_gte(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_PIPE)       return eval_max_of(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_AMP)        return eval_min_of(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_PLUS)       return eval_add(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_MINUS)      return eval_subtract(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_STAR)       return eval_multiply(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_PERCENT)    return eval_divide(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_STAR_STAR)  return eval_power(arg_values[0], arg_values[1]);
        else if (op_token == TOKEN_AT) {
            /* f@x  =  f[x]: apply left to right as a single argument */
            return eval_apply_value(arg_values[0], &arg_values[1], 1, env);
        }
        else if (op_token == TOKEN_DOT) {
            /* f . v  =  f[...v]: apply left, spreading rhs vector/list elements as args */
            Qo func = arg_values[0];
            Qo rhs  = arg_values[1];
            if (rhs != NULL && is_vector_type(qo_type(rhs))) {
                int64_t n = qo_count(rhs);
                Qo *spread = xmalloc(sizeof(Qo) * (n > 0 ? (size_t)n : 1));
                int owns_spread_values = (qo_type(rhs) != QO_LIST);
                for (int64_t i = 0; i < n; i++) {
                    spread[i] = owns_spread_values ? dict_elem_copy(rhs, i) : qo_ptr_data(rhs)[i];
                }
                result = eval_apply_value(func, spread, (int)n, env);
                if (owns_spread_values) {
                    for (int64_t i = 0; i < n; i++) value_free(spread[i]);
                }
                free(spread);
                return result;
            }
            return eval_apply_value(func, &rhs, 1, env);
        }
        else EVAL_ERROR("operator '/' is undefined");
    }

    /* hopen: accepts 1 arg (port) or 2 args (host, port) */
    if (strcmp(verb, "hopen") == 0) {
        int fd;
        if (arg_count == 1) {
            Qo port_val = arg_values[0];
            if (port_val == NULL || !is_numeric_scalar_type(qo_type(port_val)))
                EVAL_ERROR("hopen expects a numeric port");
            int port = (int)value_as_double(port_val);
            fd = ipc_connect("127.0.0.1", port);
        } else if (arg_count == 2) {
            Qo host_val = arg_values[0];
            Qo port_val = arg_values[1];
            if (host_val == NULL || qo_type(host_val) != QO_CHAR_VEC)
                EVAL_ERROR("hopen expects a string host");
            if (port_val == NULL || !is_numeric_scalar_type(qo_type(port_val)))
                EVAL_ERROR("hopen expects a numeric port");
            char *host = charvec_to_cstr(host_val);
            int port = (int)value_as_double(port_val);
            fd = ipc_connect(host, port);
            free(host);
        } else {
            EVAL_ERROR("hopen expects 1 or 2 arguments");
        }
        if (fd < 0) EVAL_ERROR("failed to connect");
        // Accept the server-side connection if this process has a server socket
        if (ipc_server_fd() >= 0) {
            ipc_accept_connection();
        }
        return make_int_value(fd);
    }

    /* find: 2 args (haystack, needles) */
    if (strcmp(verb, "find") == 0) {
        if (arg_count != 2) EVAL_ERROR("find expects exactly 2 arguments");
        return eval_builtin_find(arg_values[0], arg_values[1], env);
    }

    static const BuiltinSpec builtins[] = {
        {"sum",   eval_builtin_sum},
        {"count", eval_builtin_count},
        {"min",   eval_builtin_min},
        {"max",   eval_builtin_max},
        {"til",   eval_builtin_til},
        {"refcount", eval_builtin_refcount},
        {"parse", eval_builtin_parse},
        {"lex",   eval_builtin_lex},
        {"not",   eval_builtin_not},
        {"type",  eval_builtin_type},
        {"key",   eval_builtin_key},
        {"value", eval_builtin_value_fn},
        {"print", eval_builtin_print},
        {"exit",  eval_builtin_exit},
        {"eval",  eval_builtin_eval},
        {"first", eval_builtin_first},
        {"last",  eval_builtin_last},
        {"now",   eval_builtin_now},
        {"read",  eval_builtin_read},
        {"shell", eval_builtin_shell},
        {"ser",   eval_builtin_ser},
        {"deser", eval_builtin_deser},
        {"listen", eval_builtin_listen},
        {"hclose", eval_builtin_hclose},
    };

    for (size_t i = 0; i < sizeof(builtins) / sizeof(builtins[0]); i++) {
        if (strcmp(verb, builtins[i].name) == 0) {
            if (arg_count != 1)
                EVAL_ERROR_FMT("%s expects exactly 1 argument, got %d",
                               builtins[i].name, arg_count);
            result = builtins[i].fn(arg_values[0], env);
            return result;
        }
    }

    EVAL_ERROR_FMT("unknown keyword '%s'", verb);
}

static Qo eval_builtin_assign(Qo *args, int arg_count, Environment *env) {
    if (arg_count != 2) EVAL_ERROR("assignment requires exactly 2 arguments");
    Qo target = args[0];
    if (target == NULL || qo_type(target) != QO_SYMBOL) EVAL_ERROR("assignment target must be a symbol");
    Qo result = eval_value(args[1], env);
    if (evaluator_error_requested() || evaluator_exit_requested()) return result;
        env_set(env, qo_symbol_id(target), result);
    return result;
}

/* ── projection helpers ──────────────────────────────────────────────────── */

static Qo make_projection_value(Qo function, Qo *slots, int64_t slot_count) {
    size_t sz = 16 + (size_t)slot_count * sizeof(Qo);
    Qo p = qo_alloc_raw(sz);
    p->type_tag = QO_PROJECTION;
    p->attribute = 0;
    QO_PROJ_AC(p) = slot_count;
    QO_PROJ_FUNC(p) = value_copy(function);
    for (int64_t i = 0; i < slot_count; i++) QO_PROJ_ARGS(p)[i] = slots[i];
    free(slots);  /* "take" ownership of slots array */
    return p;
}

/* ── call function ───────────────────────────────────────────────────────── */

static Qo eval_call_function(Qo function, Qo *arg_vals, int arg_count, Environment *env) {
    int64_t pc = QO_FN_PC(function);
    int64_t bc = QO_FN_BC(function);
    if ((int64_t)arg_count != pc) {
        EVAL_ERROR_FMT("function expects %ld args, got %d", (long)pc, arg_count);
    }
    Environment *global_env = env_root(env);
    Environment *call_env   = env_new_with_parent(global_env);
    for (int i = 0; i < arg_count; i++) {
            env_set(call_env, qo_symbol_id(QO_FN_PARAMS(function)[i]), arg_vals[i]);
    }
    Qo last = NULL;
    for (int64_t i = 0; i < bc; i++) {
        Qo cur = eval_value(QO_FN_BODY(function)[i], call_env);
        if (i > 0) value_free(last);
        last = cur;
        if (evaluator_exit_requested() || evaluator_error_requested()) break;
    }
    env_free(call_env);
    return last;
}

static void free_evaluated_args(Qo *args, int count) {
    for (int i = 0; i < count; i++) value_free(args[i]);
    free(args);
}

static int eval_args_or_stop(Qo tree, int start_index, int arg_count, Environment *env, Qo *args) {
    for (int i = 0; i < arg_count; i++) {
        args[i] = eval_value(QO_LIST_DATA(tree)[start_index + i], env);
        if (evaluator_error_requested() || evaluator_exit_requested()) {
            for (int j = 0; j <= i; j++) value_free(args[j]);
            return 0;
        }
    }
    return 1;
}

/* ── eval_apply_value ────────────────────────────────────────────────────── */

/* Try to pack an array of scalar Qo values into a typed vector.
   Returns a typed vector if all elements share a single scalar type,
   otherwise returns a list. Takes ownership of the values array. */
static Qo pack_results(Qo *values, int count) {
    if (count == 0) {
        free(values);
        return alloc_ptr_vec(QO_LIST, 0);
    }
    uint8_t first_type = qo_type(values[0]);
    int all_same = 1;
    for (int i = 1; i < count; i++) {
        if (values[i] == NULL || qo_type(values[i]) != first_type) {
            all_same = 0;
            break;
        }
    }
    if (all_same) {
        uint8_t vec_type;
        if (first_type == QO_SHORT) vec_type = QO_SHORT_VEC;
        else if (first_type == QO_INT) vec_type = QO_INT_VEC;
        else if (first_type == QO_LONG) vec_type = QO_LONG_VEC;
        else if (first_type == QO_FLOAT) vec_type = QO_FLOAT_VEC;
        else if (first_type == QO_CHAR) vec_type = QO_CHAR_VEC;
        else if (first_type == QO_BOOL) vec_type = QO_BOOL_VEC;
        else if (first_type == QO_BYTE) vec_type = QO_BYTE_VEC;
        else if (first_type == QO_SYMBOL) vec_type = QO_SYM_VEC;
        else all_same = 0;

        if (all_same) {
            if (vec_type == QO_CHAR_VEC) {
                Qo result = alloc_charlike(QO_CHAR_VEC, count);
                for (int i = 0; i < count; i++) qo_char_data(result)[i] = qo_char(values[i]);
                for (int i = 0; i < count; i++) qo_release(values[i]);
                free(values);
                return result;
            }
            if (vec_type == QO_SYM_VEC) {
                Qo result = alloc_ptr_vec(QO_SYM_VEC, count);
                for (int i = 0; i < count; i++) qo_ptr_data(result)[i] = values[i];
                free(values);
                return result;
            }
            {
                Qo result = alloc_data_vec(vec_type, count);
                for (int i = 0; i < count; i++) {
                    if (vec_type == QO_SHORT_VEC) qo_short_data(result)[i] = qo_short(values[i]);
                    else if (vec_type == QO_INT_VEC) qo_int_data(result)[i] = qo_int(values[i]);
                    else if (vec_type == QO_LONG_VEC) qo_long_data(result)[i] = qo_long(values[i]);
                    else if (vec_type == QO_FLOAT_VEC) qo_float_data(result)[i] = qo_float(values[i]);
                    else if (vec_type == QO_BOOL_VEC) qo_bool_data(result)[i] = qo_bool(values[i]);
                    else qo_byte_data(result)[i] = qo_byte(values[i]);
                    qo_release(values[i]);
                }
                free(values);
                return result;
            }
        }
    }
    /* Heterogeneous: return as list */
    Qo result = alloc_ptr_vec(QO_LIST, count);
    for (int i = 0; i < count; i++) qo_ptr_data(result)[i] = values[i];
    free(values);
    return result;
}

static Qo eval_apply_value(Qo head, Qo *args, int arg_count, Environment *env) {
    if (head == NULL) EVAL_ERROR("cannot apply null value");

    if (qo_type(head) == QO_KEYWORD) {
        return eval_apply_keyword(head, args, arg_count, env);
    }

    if (qo_type(head) == QO_EACHED) {
        if (arg_count != 1) EVAL_ERROR("each expects 1 argument");
        Qo func = QO_EACHED_FUNC(head);
        Qo arg = args[0];
        if (arg == NULL) return eval_apply_value(func, args, arg_count, env);
        uint8_t at = qo_type(arg);
        if (is_vector_type(at)) {
            int64_t n = qo_count(arg);
            Qo *results = xmalloc(sizeof(Qo) * (n > 0 ? (size_t)n : 1));
            for (int64_t i = 0; i < n; i++) {
                Qo elem = dict_elem_copy(arg, i);
                results[i] = eval_apply_value(func, &elem, 1, env);
                qo_release(elem);
                if (evaluator_error_requested() || evaluator_exit_requested()) {
                    for (int64_t j = 0; j <= i; j++) qo_release(results[j]);
                    free(results);
                    return NULL;
                }
            }
            return pack_results(results, (int)n);
        }
        if (at == QO_DICT) {
            int64_t n = QO_DICT_COUNT(arg);
            Qo *results = xmalloc(sizeof(Qo) * (n > 0 ? (size_t)n : 1));
            for (int64_t i = 0; i < n; i++) {
                Qo val = qo_clone(QO_DICT_VALS(arg)[i]);
                results[i] = eval_apply_value(func, &val, 1, env);
                qo_release(val);
                if (evaluator_error_requested() || evaluator_exit_requested()) {
                    for (int64_t j = 0; j <= i; j++) qo_release(results[j]);
                    free(results);
                    return NULL;
                }
            }
            /* build dictionary with same keys, mapped values */
            Qo dict = alloc_dict_block(n);
            QO_DICT_KTYPE(dict) = QO_DICT_KTYPE(arg);
            QO_DICT_VTYPE(dict) = QO_DICT_VTYPE(arg);
            for (int64_t i = 0; i < n; i++) {
                QO_DICT_KEYS(dict)[i] = qo_clone(QO_DICT_KEYS(arg)[i]);
                QO_DICT_VALS(dict)[i] = results[i];
            }
            free(results);
            return dict;
        }
        return eval_apply_value(func, args, arg_count, env);
    }

    if (qo_type(head) == QO_PROJECTION) {
        int64_t slot_count = QO_PROJ_AC(head);
        int64_t holes = 0;
        for (int64_t i = 0; i < slot_count; i++) {
            if (QO_PROJ_ARGS(head)[i] != NULL && qo_type(QO_PROJ_ARGS(head)[i]) == QO_PROJECTOR) holes++;
        }
        if ((int64_t)arg_count > holes)
            EVAL_ERROR_FMT("projection expects at most %ld args, got %d", (long)holes, arg_count);
        Qo *merged = xmalloc(sizeof(Qo) * (slot_count > 0 ? slot_count : 1));
        int fill_idx = 0;
        for (int64_t i = 0; i < slot_count; i++) {
            Qo slot = QO_PROJ_ARGS(head)[i];
            if (slot != NULL && qo_type(slot) == QO_PROJECTOR && fill_idx < arg_count) {
                merged[i] = value_copy(args[fill_idx++]);
            } else {
                merged[i] = value_copy(slot);
            }
        }
        for (int64_t i = 0; i < slot_count; i++) {
            if (merged[i] != NULL && qo_type(merged[i]) == QO_PROJECTOR) {
                return make_projection_value(QO_PROJ_FUNC(head), merged, slot_count);
            }
        }
        Qo result = eval_call_function(QO_PROJ_FUNC(head), merged, (int)slot_count, env);
        for (int64_t i = 0; i < slot_count; i++) value_free(merged[i]);
        free(merged);
        return result;
    }

    if (qo_type(head) == QO_FUNCTION) {
        int64_t pc = QO_FN_PC(head);
        if ((int64_t)arg_count > pc)
            EVAL_ERROR_FMT("function expects %ld args, got %d", (long)pc, arg_count);
        int has_projector = 0;
        for (int i = 0; i < arg_count; i++) {
            if (args[i] != NULL && qo_type(args[i]) == QO_PROJECTOR) { has_projector = 1; break; }
        }
        if ((int64_t)arg_count < pc || has_projector) {
            Qo *slots = xmalloc(sizeof(Qo) * (pc > 0 ? pc : 1));
            for (int64_t i = 0; i < pc; i++) slots[i] = make_projector_value();
            for (int i = 0; i < arg_count; i++) { value_free(slots[i]); slots[i] = value_copy(args[i]); }
            return make_projection_value(head, slots, pc);
        }
        return eval_call_function(head, args, arg_count, env);
    }

    if (arg_count != 1) EVAL_ERROR("application expects exactly one argument");

    /* Handle apply: QO_INT as IPC file descriptor */
    if (head != NULL && qo_type(head) == QO_INT && arg_count == 1) {
        return ipc_handle_apply(qo_int(head), args[0]);
    }

    /* Multi-index: numeric vector, bool vector, or list of indices */
    if (args[0] != NULL) {
        uint8_t it = qo_type(args[0]);
        if (is_numeric_vector_type(it) || it == QO_LIST) {
            if (head == NULL) EVAL_ERROR("cannot index null");
            uint8_t ht = qo_type(head);
            if (!is_vector_type(ht) && ht != QO_DICT) EVAL_ERROR("value is not indexable");

            int64_t n = qo_count(args[0]);
            int64_t head_n = (ht == QO_DICT) ? QO_DICT_COUNT(head) : qo_count(head);
            Qo result;
            int is_ptr = (ht == QO_LIST || ht == QO_SYM_VEC || ht == QO_DICT);

            if (is_ptr) {
                result = alloc_ptr_vec(ht == QO_DICT ? QO_LIST : ht, n);
                memset(qo_ptr_data(result), 0, (size_t)n * sizeof(Qo));
            } else {
                result = alloc_data_vec(ht, n);
            }

            if (it == QO_FLOAT_VEC && ht != QO_DICT) { qo_release(result); EVAL_ERROR("index must be numeric"); }
            for (int64_t i = 0; i < n; i++) {
                if (ht == QO_DICT) {
                    Qo key;
                    if (it == QO_SHORT_VEC) key = make_short_value(qo_short_data(args[0])[i]);
                    else if (it == QO_INT_VEC) key = make_int_value(qo_int_data(args[0])[i]);
                    else if (it == QO_LONG_VEC) key = make_long_value(qo_long_data(args[0])[i]);
                    else if (it == QO_FLOAT_VEC) key = make_float_value(qo_float_data(args[0])[i]);
                    else if (it == QO_BOOL_VEC) key = make_bool_value(qo_bool_data(args[0])[i]);
                    else key = value_copy(qo_ptr_data(args[0])[i]);

                    int found = 0;
                    for (int64_t j = 0; j < head_n; j++) {
                        if (value_equals(QO_DICT_KEYS(head)[j], key)) {
                            qo_ptr_data(result)[i] = value_copy(QO_DICT_VALS(head)[j]);
                            found = 1;
                            break;
                        }
                    }
                    qo_release(key);
                    if (!found) {
                        qo_release(result);
                        EVAL_ERROR("dictionary key not found");
                    }
                } else {
                    int64_t idx;
                    if (it == QO_SHORT_VEC) idx = qo_short_data(args[0])[i];
                    else if (it == QO_INT_VEC) idx = qo_int_data(args[0])[i];
                    else if (it == QO_LONG_VEC) idx = qo_long_data(args[0])[i];
                    else if (it == QO_BOOL_VEC) idx = qo_bool_data(args[0])[i];
                    else {
                        Qo e = qo_ptr_data(args[0])[i];
                        uint8_t et;
                        if (e == NULL || (et = qo_type(e)) == QO_FLOAT || !is_numeric_scalar_type(et)) {
                            qo_release(result);
                            EVAL_ERROR("index must be numeric");
                        }
                        if (et == QO_SHORT) idx = qo_short(e);
                        else if (et == QO_INT) idx = qo_int(e);
                        else if (et == QO_LONG) idx = qo_long(e);
                        else idx = qo_bool(e);
                    }

                    if (idx < 0 || idx >= head_n) {
                        if (ht == QO_DICT) {
                            qo_release(result);
                            EVAL_ERROR("dictionary key not found");
                        }
                        if (is_ptr) qo_ptr_data(result)[i] = NULL;
                        else if (ht == QO_LONG_VEC) qo_long_data(result)[i] = QO_LONG_NULL;
                        else if (ht == QO_INT_VEC) qo_int_data(result)[i] = QO_INT_NULL;
                        else if (ht == QO_SHORT_VEC) qo_short_data(result)[i] = QO_SHORT_NULL;
                        else if (ht == QO_FLOAT_VEC) qo_float_data(result)[i] = QO_FLOAT_NULL;
                        else if (ht == QO_CHAR_VEC) qo_char_data(result)[i] = 0;
                        else if (ht == QO_BOOL_VEC) qo_bool_data(result)[i] = 0;
                        else if (ht == QO_BYTE_VEC) qo_byte_data(result)[i] = 0;
                        continue;
                    }

                    if (is_ptr) {
                        qo_ptr_data(result)[i] = dict_elem_copy(head, idx);
                    } else {
                        Qo elem = dict_elem_copy(head, idx);
                        uint8_t et = qo_type(elem);
                        if (et == QO_SHORT) qo_short_data(result)[i] = qo_short(elem);
                        else if (et == QO_INT) qo_int_data(result)[i] = qo_int(elem);
                        else if (et == QO_LONG) qo_long_data(result)[i] = qo_long(elem);
                        else if (et == QO_FLOAT) qo_float_data(result)[i] = qo_float(elem);
                        else if (et == QO_BOOL) qo_bool_data(result)[i] = qo_bool(elem);
                        else if (et == QO_CHAR) qo_char_data(result)[i] = qo_char(elem);
                        else if (et == QO_BYTE) qo_byte_data(result)[i] = qo_byte(elem);
                        qo_release(elem);
                    }
                }
            }
            return result;
        }
    }

    /* Dictionary key lookup (non-integer index) */
    if (args[0] != NULL &&
        qo_type(args[0]) != QO_SHORT &&
        qo_type(args[0]) != QO_INT &&
        qo_type(args[0]) != QO_LONG) {
        if (head != NULL && qo_type(head) == QO_DICT) {
            int64_t n = QO_DICT_COUNT(head);
            for (int64_t i = 0; i < n; i++) {
                if (value_equals(QO_DICT_KEYS(head)[i], args[0]))
                    return value_copy(QO_DICT_VALS(head)[i]);
            }
            EVAL_ERROR("dictionary key not found");
        }
        EVAL_ERROR("index must be an int");
    }

    if (args[0] == NULL) EVAL_ERROR("index must be an int");
    long index;
    if (qo_type(args[0]) == QO_SHORT) index = qo_short(args[0]);
    else if (qo_type(args[0]) == QO_INT) index = qo_int(args[0]);
    else index = (long)qo_long(args[0]);
    if (index < 0) { return make_long_value(QO_LONG_NULL); }

    if (head == NULL) EVAL_ERROR("cannot index null");
    uint8_t ht = qo_type(head);

    if (is_vector_type(ht)) {
        int64_t n = qo_count(head);
        if ((int64_t)index >= n) return null_for_vector_type(ht);
        return dict_elem_copy(head, (int64_t)index);
    }

    if (ht == QO_DICT) {
        int64_t n = QO_DICT_COUNT(head);
        for (int64_t i = 0; i < n; i++) {
            if (value_equals(QO_DICT_KEYS(head)[i], args[0]))
                return value_copy(QO_DICT_VALS(head)[i]);
        }
        EVAL_ERROR("dictionary key not found");
    }

    EVAL_ERROR("value is not callable/indexable");
}

/* ── eval_value ──────────────────────────────────────────────────────────── */
Qo eval_value(Qo tree, Environment *env) {
    if (evaluator_error_requested() || evaluator_exit_requested()) return NULL;

    if (tree == NULL) return NULL;

    /* Data vectors return as-is; lists are executable AST nodes and must continue. */
    if (is_vector_type(qo_type(tree)) && qo_type(tree) != QO_LIST) {
        return value_copy(tree);
    }

    /* Symbol: variable lookup */
    if (qo_type(tree) == QO_SYMBOL) {
        int found = 0;
        Qo v = env_get(env, qo_symbol_id(tree), &found);
        if (found) return v;
        EVAL_ERROR_FMT("undefined variable '%s'", qo_symbol_name(tree));
    }

    /* Non-list: return as-is */
    if (qo_type(tree) != QO_LIST) return value_copy(tree);

    int64_t n = qo_count(tree);
    if (n == 0) return value_copy(tree);

    /* Single-element list: return its contents */
    if (n == 1) return value_copy(QO_LIST_DATA(tree)[0]);

    Qo verb_val = QO_LIST_DATA(tree)[0];
    int arg_count = (int)(n - 1);
    Qo result;

    if (verb_val == NULL || qo_type(verb_val) != QO_KEYWORD) {
        /* Non-keyword head: evaluate and apply */
        Qo head = eval_value(verb_val, env);
        Qo *args = xmalloc(sizeof(Qo) * arg_count);
        if (evaluator_error_requested() || evaluator_exit_requested()) {
            value_free(head); free(args); return NULL;
        }
        if (!eval_args_or_stop(tree, 1, arg_count, env, args)) {
            value_free(head); free(args); return NULL;
        }
        result = eval_apply_value(head, args, arg_count, env);
        value_free(head);
        free_evaluated_args(args, arg_count);
        return result;
    }

    const char *verb = QO_STR(verb_val);

    /* Assignment: raw LHS symbol, evaluated RHS */
    if (strcmp(verb, ":") == 0)
        return eval_builtin_assign(&QO_LIST_DATA(tree)[1], arg_count, env);

    /* Enlist: evaluate args normally */
    if (strcmp(verb, "enlist") == 0) {
        Qo *av = xmalloc(sizeof(Qo) * (arg_count > 0 ? arg_count : 1));
        if (!eval_args_or_stop(tree, 1, arg_count, env, av)) { free(av); return NULL; }
        result = eval_builtin_enlist(av, arg_count, env);
        free_evaluated_args(av, arg_count);
        return result;
    }

    /* Sequence: one statement at a time */
    if (strcmp(verb, ";") == 0) {
        Qo last = NULL;
        for (int i = 0; i < arg_count; i++) {
            Qo cur = eval_value(QO_LIST_DATA(tree)[i + 1], env);
            if (i > 0) value_free(last);
            last = cur;
            if (evaluator_exit_requested() || evaluator_error_requested()) break;
        }
        return last;
    }

    /* Each: wrap the evaluated func in eached (from ' postfix) */
    if (strcmp(verb, "each") == 0) {
        if (arg_count != 1) EVAL_ERROR("each expects exactly 1 argument");
        Qo arg = eval_value(QO_LIST_DATA(tree)[1], env);
        if (evaluator_error_requested() || evaluator_exit_requested()) { qo_release(arg); return NULL; }
        Qo r = make_eached_value(arg);
        qo_release(arg);
        return r;
    }

    /* All other keywords: evaluate args first */
    Qo *arg_values = xmalloc(sizeof(Qo) * (arg_count > 0 ? arg_count : 1));
    if (!eval_args_or_stop(tree, 1, arg_count, env, arg_values)) { free(arg_values); return NULL; }

    result = eval_apply_keyword(verb_val, arg_values, arg_count, env);
    free_evaluated_args(arg_values, arg_count);
    return result;
}

/* ── operator_name_to_token ──────────────────────────────────────────────── */

int operator_name_to_token(const char *name, TokenType *op) {
    if (strcmp(name, "+") == 0)  { *op = TOKEN_PLUS;       return 1; }
    if (strcmp(name, "-") == 0)  { *op = TOKEN_MINUS;      return 1; }
    if (strcmp(name, "*") == 0)  { *op = TOKEN_STAR;       return 1; }
    if (strcmp(name, "**") == 0) { *op = TOKEN_STAR_STAR;  return 1; }
    if (strcmp(name, "/") == 0)  { *op = TOKEN_SLASH;      return 1; }
    if (strcmp(name, "%") == 0)  { *op = TOKEN_PERCENT;    return 1; }
    if (strcmp(name, "!") == 0)  { *op = TOKEN_BANG;       return 1; }
    if (strcmp(name, ",") == 0)  { *op = TOKEN_COMMA;      return 1; }
    if (strcmp(name, "#") == 0)  { *op = TOKEN_HASH;       return 1; }
    if (strcmp(name, "_") == 0)  { *op = TOKEN_UNDERSCORE; return 1; }
    if (strcmp(name, "=") == 0)  { *op = TOKEN_EQUAL;      return 1; }
    if (strcmp(name, "<") == 0)  { *op = TOKEN_LESS;       return 1; }
    if (strcmp(name, ">") == 0)  { *op = TOKEN_GREATER;    return 1; }
    if (strcmp(name, "<=") == 0) { *op = TOKEN_LE;         return 1; }
    if (strcmp(name, ">=") == 0) { *op = TOKEN_GE;         return 1; }
    if (strcmp(name, "|") == 0)  { *op = TOKEN_PIPE;       return 1; }
    if (strcmp(name, "&") == 0)  { *op = TOKEN_AMP;        return 1; }
    if (strcmp(name, "@") == 0)  { *op = TOKEN_AT;         return 1; }
    if (strcmp(name, ".") == 0)  { *op = TOKEN_DOT;        return 1; }
    return 0;
}
