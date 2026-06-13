#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>
#include "evaluator_internal.h"
#include "internal.h"
#include "lexer.h"
#include "parser.h"
#include "ipc.h"
#include "symbol_intern.h"

/* forward declarations */
static Qo eval_apply_value(Qo head, Qo *args, int arg_count, Environment *env);
static Qo make_projection_value(Qo function, Qo *slots, int64_t slot_count);

/* ── builtin name ↔ id mapping ────────────────────────────────────────────── */

int builtin_name_to_id(const char *name) {
    if (strcmp(name, "sum") == 0)      return QO_BUILTIN_SUM;
    if (strcmp(name, "count") == 0)    return QO_BUILTIN_COUNT;
    if (strcmp(name, "min") == 0)      return QO_BUILTIN_MIN;
    if (strcmp(name, "max") == 0)      return QO_BUILTIN_MAX;
    if (strcmp(name, "til") == 0)      return QO_BUILTIN_TIL;
    if (strcmp(name, "parse") == 0)    return QO_BUILTIN_PARSE;
    if (strcmp(name, "lex") == 0)      return QO_BUILTIN_LEX;
    if (strcmp(name, "not") == 0)      return QO_BUILTIN_NOT;
    if (strcmp(name, "type") == 0)     return QO_BUILTIN_TYPE;
    if (strcmp(name, "key") == 0)      return QO_BUILTIN_KEY;
    if (strcmp(name, "value") == 0)    return QO_BUILTIN_VALUE;
    if (strcmp(name, "print") == 0)    return QO_BUILTIN_PRINT;
    if (strcmp(name, "exit") == 0)     return QO_BUILTIN_EXIT;
    if (strcmp(name, "enlist") == 0)   return QO_BUILTIN_ENLIST;
    if (strcmp(name, "eval") == 0)     return QO_BUILTIN_EVAL;
    if (strcmp(name, "first") == 0)    return QO_BUILTIN_FIRST;
    if (strcmp(name, "last") == 0)     return QO_BUILTIN_LAST;
    if (strcmp(name, "now") == 0)      return QO_BUILTIN_NOW;
    if (strcmp(name, "read") == 0)     return QO_BUILTIN_READ;
    if (strcmp(name, "shell") == 0)    return QO_BUILTIN_SHELL;
    if (strcmp(name, "ser") == 0)      return QO_BUILTIN_SER;
    if (strcmp(name, "deser") == 0)    return QO_BUILTIN_DESER;
    if (strcmp(name, "listen") == 0)   return QO_BUILTIN_LISTEN;
    if (strcmp(name, "hclose") == 0)   return QO_BUILTIN_HCLOSE;
    if (strcmp(name, "hopen") == 0)    return QO_BUILTIN_HOPEN;
    if (strcmp(name, "refcount") == 0) return QO_BUILTIN_REFCOUNT;
    if (strcmp(name, "find") == 0)     return QO_BUILTIN_FIND;
    if (strcmp(name, ";") == 0)        return QO_BUILTIN_SEMICOLON;
    if (strcmp(name, "null") == 0)     return QO_BUILTIN_NULL;
    if (strcmp(name, "string") == 0)   return QO_BUILTIN_STRING;
    if (strcmp(name, "flip") == 0)     return QO_BUILTIN_FLIP;
    if (strcmp(name, "where") == 0)    return QO_BUILTIN_WHERE;
    return -1;
}

const char *builtin_id_to_name(uint8_t id) {
    switch (id) {
        case QO_BUILTIN_SUM:      return "sum";
        case QO_BUILTIN_COUNT:    return "count";
        case QO_BUILTIN_MIN:      return "min";
        case QO_BUILTIN_MAX:      return "max";
        case QO_BUILTIN_TIL:      return "til";
        case QO_BUILTIN_PARSE:    return "parse";
        case QO_BUILTIN_LEX:      return "lex";
        case QO_BUILTIN_NOT:      return "not";
        case QO_BUILTIN_TYPE:     return "type";
        case QO_BUILTIN_KEY:      return "key";
        case QO_BUILTIN_VALUE:    return "value";
        case QO_BUILTIN_PRINT:    return "print";
        case QO_BUILTIN_EXIT:     return "exit";
        case QO_BUILTIN_ENLIST:   return "enlist";
        case QO_BUILTIN_EVAL:     return "eval";
        case QO_BUILTIN_FIRST:    return "first";
        case QO_BUILTIN_LAST:     return "last";
        case QO_BUILTIN_NOW:      return "now";
        case QO_BUILTIN_READ:     return "read";
        case QO_BUILTIN_SHELL:    return "shell";
        case QO_BUILTIN_SER:      return "ser";
        case QO_BUILTIN_DESER:    return "deser";
        case QO_BUILTIN_LISTEN:   return "listen";
        case QO_BUILTIN_HCLOSE:   return "hclose";
        case QO_BUILTIN_HOPEN:    return "hopen";
        case QO_BUILTIN_REFCOUNT: return "refcount";
        case QO_BUILTIN_FIND:     return "find";
        case QO_BUILTIN_SEMICOLON:return ";";
        case QO_BUILTIN_NULL:     return "null";
        case QO_BUILTIN_STRING:   return "string";
        case QO_BUILTIN_FLIP:     return "flip";
        case QO_BUILTIN_WHERE:    return "where";
        default:                  return NULL;
    }
}

/* ── parse helpers ───────────────────────────────────────────────────────── */

/* Recursively copy a parsed tree. Operators and builtins keep their type;
   the parser already resolves non-builtin identifiers to symbols. */
static Qo normalize_parsed_tree(Qo tree) {
    if (tree == NULL) return NULL;
    if (qo_type(tree) != QO_LIST) return qo_clone(tree);
    int64_t n = qo_count(tree);
    Qo result = alloc_ptr_vec(QO_LIST, n);
    for (int64_t i = 0; i < n; i++) {
        qo_ptr_data(result)[i] = normalize_parsed_tree(qo_ptr_data(tree)[i]);
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
    if (type_has_flag(t, TF_SCALAR)) return qo_clone(arg);
    switch (type_storage(t)) {
        case SC_I16: {
            int64_t total = 0;
            int64_t n = qo_count(arg);
            for (int64_t i = 0; i < n; i++) total += (int64_t)qo_short_data(arg)[i];
            return make_long_value(total);
        }
        case SC_I32: {
            int64_t total = 0;
            int64_t n = qo_count(arg);
            for (int64_t i = 0; i < n; i++) total += (int64_t)qo_int_data(arg)[i];
            return make_long_value(total);
        }
        case SC_I64: {
            int64_t total = 0;
            int64_t n = qo_count(arg);
            for (int64_t i = 0; i < n; i++) total += qo_long_data(arg)[i];
            return make_long_value(total);
        }
        case SC_F64: {
            double total = 0.0;
            int64_t n = qo_count(arg);
            for (int64_t i = 0; i < n; i++) total += qo_float_data(arg)[i];
            return make_float_value(total);
        }
        default: break;
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
    if (is_parse_error(result)) EVAL_ERROR("parse: failed to parse expression");
    if (result != NULL) {
        Qo normalized = normalize_parsed_tree(result);
        qo_release(result);
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
            switch (type_storage(t)) {
                case SC_I16: zero = (qo_short_data(arg)[i] == 0); break;
                case SC_I32: zero = (qo_int_data(arg)[i] == 0); break;
                case SC_I64: zero = (qo_long_data(arg)[i] == 0); break;
                case SC_F64: zero = (qo_float_data(arg)[i] == 0.0); break;
                default:     zero = (qo_bool_data(arg)[i] == 0); break;
            }
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

static Qo eval_builtin_null(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) return make_bool_value(1);

    uint8_t t = qo_type(arg);

    switch (t) {
        case QO_SHORT:
            return make_bool_value(qo_short(arg) == QO_SHORT_NULL);
        case QO_INT:
            return make_bool_value(qo_int(arg) == QO_INT_NULL);
        case QO_LONG:
            return make_bool_value(qo_long(arg) == QO_LONG_NULL);
        case QO_TIMESPAN:
            return make_bool_value(qo_timespan(arg) == QO_LONG_NULL);
        case QO_FLOAT:
            return make_bool_value(isnan(qo_float(arg)));

        case QO_BOOL_VEC:
        case QO_BYTE_VEC:
        case QO_CHAR_VEC:
        case QO_SHORT_VEC:
        case QO_INT_VEC:
        case QO_LONG_VEC:
        case QO_TIMESTAMP_VEC:
        case QO_TIMESPAN_VEC:
        case QO_FLOAT_VEC:
        case QO_SYM_VEC: {
            int64_t n = qo_count(arg);
            Qo result = alloc_data_vec(QO_BOOL_VEC, n);
            for (int64_t i = 0; i < n; i++) {
                int is_null = 0;
                switch (type_storage(t)) {
                    case SC_U8:                                                     /* bool/byte/char: no sentinel null */
                        break;
                    case SC_I16: is_null = (qo_short_data(arg)[i] == QO_SHORT_NULL); break;
                    case SC_I32: is_null = (qo_int_data(arg)[i] == QO_INT_NULL); break;
                    case SC_I64: is_null = (qo_long_data(arg)[i] == QO_LONG_NULL); break;
                    case SC_F64: is_null = isnan(qo_float_data(arg)[i]); break;
                    default: break;
                }
                qo_bool_data(result)[i] = is_null ? 1 : 0;
            }
            return result;
        }

        case QO_LIST: {
            int64_t n = qo_count(arg);
            Qo result = alloc_data_vec(QO_BOOL_VEC, n);
            for (int64_t i = 0; i < n; i++) {
                Qo elem = qo_ptr_data(arg)[i];
                int is_null = 0;
                if (elem == NULL) {
                    is_null = 1;
                } else {
                    uint8_t et = qo_type(elem);
                    if (et == QO_SHORT) is_null = (qo_short(elem) == QO_SHORT_NULL);
                    else if (et == QO_INT) is_null = (qo_int(elem) == QO_INT_NULL);
                    else if (et == QO_LONG) is_null = (qo_long(elem) == QO_LONG_NULL);
                    else if (et == QO_FLOAT) is_null = isnan(qo_float(elem));
                }
                qo_bool_data(result)[i] = is_null ? 1 : 0;
            }
            return result;
        }

        default:
            return make_bool_value(0);
    }
}

static Qo eval_builtin_count(Qo arg, Environment *env) {
    (void)env;
    if (arg != NULL) {
        uint8_t t = qo_type(arg);
        if (is_vector_type(t) || t == QO_DICT || t == QO_TABLE)
            return make_long_value(qo_count(arg));
    }
    EVAL_ERROR("count expects a list, vector, dict, or table argument");
}

static Qo eval_builtin_min(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) EVAL_ERROR("min expects a number or numeric vector");
    uint8_t t = qo_type(arg);
    if (type_has_flag(t, TF_SCALAR)) return qo_clone(arg);
    if (t == QO_LIST) {
        int use_float = 0;
        double m = 0.0;
        int64_t n = qo_count(arg);
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
    if (!is_vector_type(t)) EVAL_ERROR("min expects a number or numeric vector");
    {
        int64_t n = qo_count(arg);
        if (n == 0) EVAL_ERROR("min on empty vector");
        switch (type_storage(t)) {
            case SC_I16: {
                int16_t m = qo_short_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_short_data(arg)[i] < m) m = qo_short_data(arg)[i];
                return make_short_value(m);
            }
            case SC_I32: {
                int32_t m = qo_int_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_int_data(arg)[i] < m) m = qo_int_data(arg)[i];
                return make_int_value(m);
            }
            case SC_I64: {
                int64_t m = qo_long_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_long_data(arg)[i] < m) m = qo_long_data(arg)[i];
                return make_long_value(m);
            }
            case SC_F64: {
                double m = qo_float_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_float_data(arg)[i] < m) m = qo_float_data(arg)[i];
                return make_float_value(m);
            }
            default:
                EVAL_ERROR("min expects a number or numeric vector");
        }
    }
}

static Qo eval_builtin_max(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) EVAL_ERROR("max expects a number or numeric vector");
    uint8_t t = qo_type(arg);
    if (type_has_flag(t, TF_SCALAR)) return qo_clone(arg);
    if (t == QO_LIST) {
        int use_float = 0;
        double m = 0.0;
        int64_t n = qo_count(arg);
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
    if (!is_vector_type(t)) EVAL_ERROR("max expects a number or numeric vector");
    {
        int64_t n = qo_count(arg);
        if (n == 0) EVAL_ERROR("max on empty vector");
        switch (type_storage(t)) {
            case SC_I16: {
                int16_t m = qo_short_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_short_data(arg)[i] > m) m = qo_short_data(arg)[i];
                return make_short_value(m);
            }
            case SC_I32: {
                int32_t m = qo_int_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_int_data(arg)[i] > m) m = qo_int_data(arg)[i];
                return make_int_value(m);
            }
            case SC_I64: {
                int64_t m = qo_long_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_long_data(arg)[i] > m) m = qo_long_data(arg)[i];
                return make_long_value(m);
            }
            case SC_F64: {
                double m = qo_float_data(arg)[0];
                for (int64_t i = 1; i < n; i++) if (qo_float_data(arg)[i] > m) m = qo_float_data(arg)[i];
                return make_float_value(m);
            }
            default:
                EVAL_ERROR("max expects a number or numeric vector");
        }
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

static Qo eval_builtin_where(Qo arg, Environment *env) {
    (void)env;

    if (arg == NULL) EVAL_ERROR("where expects an argument");

    uint8_t t = qo_type(arg);
    int is_vec = (t == QO_SHORT_VEC || t == QO_INT_VEC || t == QO_LONG_VEC || t == QO_BOOL_VEC);
    int64_t n = is_vec ? qo_count(arg) : 1;

    if (!is_vec && t != QO_SHORT && t != QO_INT && t != QO_LONG && t != QO_BOOL)
        EVAL_ERROR("where expects a boolean or integer argument");

    if (n == 0) return alloc_data_vec(QO_LONG_VEC, 0);

    /* First pass: compute total result size */
    int64_t total = 0;
    for (int64_t i = 0; i < n; i++) {
        int64_t v;
        if      (t == QO_SHORT)     v = (int64_t)qo_short(arg);
        else if (t == QO_SHORT_VEC) v = (int64_t)qo_short_data(arg)[i];
        else if (t == QO_INT)       v = (int64_t)qo_int(arg);
        else if (t == QO_INT_VEC)   v = (int64_t)qo_int_data(arg)[i];
        else if (t == QO_LONG)      v = qo_long(arg);
        else if (t == QO_LONG_VEC)  v = qo_long_data(arg)[i];
        else if (t == QO_BOOL)      v = qo_bool(arg) ? 1 : 0;
        else /* QO_BOOL_VEC */      v = qo_bool_data(arg)[i] ? 1 : 0;

        if (v < 0) EVAL_ERROR("where expects non-negative values");
        total += v;
    }

    /* Guard against overflow: alloc_data_vec multiplies count by sizeof(int64_t);
       INT64_MAX * 8 overflows size_t and would crash. */
    {
        size_t needed = (size_t)total * sizeof(int64_t);
        if (total > 0 && needed / sizeof(int64_t) != (size_t)total)
            EVAL_ERROR("where result too large");
    }

    /* Second pass: fill the result with replicated indices */
    Qo out = alloc_data_vec(QO_LONG_VEC, total);
    int64_t pos = 0;
    for (int64_t i = 0; i < n; i++) {
        int64_t v;
        if      (t == QO_SHORT)     v = (int64_t)qo_short(arg);
        else if (t == QO_SHORT_VEC) v = (int64_t)qo_short_data(arg)[i];
        else if (t == QO_INT)       v = (int64_t)qo_int(arg);
        else if (t == QO_INT_VEC)   v = (int64_t)qo_int_data(arg)[i];
        else if (t == QO_LONG)      v = qo_long(arg);
        else if (t == QO_LONG_VEC)  v = qo_long_data(arg)[i];
        else if (t == QO_BOOL)      v = qo_bool(arg) ? 1 : 0;
        else /* QO_BOOL_VEC */      v = qo_bool_data(arg)[i] ? 1 : 0;

        for (int64_t j = 0; j < v; j++)
            qo_long_data(out)[pos++] = i;
    }

    return out;
}

static Qo eval_builtin_range_impl(Qo a, Qo b, Environment *env, int inclusive) {
    (void)env;
    const char *op = inclusive ? "..=" : "..";
    if (a == NULL || b == NULL) EVAL_ERROR_FMT("%s expects two arguments", op);
    int64_t start = (int64_t)value_as_double(a);
    int64_t end = (int64_t)value_as_double(b);
    if (inclusive ? (end < start) : (end <= start)) return alloc_data_vec(QO_LONG_VEC, 0);
    int64_t n = inclusive ? (end - start + 1) : (end - start);
    Qo out = alloc_data_vec(QO_LONG_VEC, n);
    for (int64_t i = 0; i < n; i++) qo_long_data(out)[i] = start + i;
    return out;
}

static Qo eval_builtin_range(Qo a, Qo b, Environment *env) { return eval_builtin_range_impl(a, b, env, 0); }
static Qo eval_builtin_range_inclusive(Qo a, Qo b, Environment *env) { return eval_builtin_range_impl(a, b, env, 1); }

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
        case QO_TIMESTAMP:  tag = "timestamp"; break;
        case QO_TIMESPAN:   tag = "timespan";  break;
        case QO_SHORT_VEC:  tag = "SHORT";     break;
        case QO_INT_VEC:    tag = "INT";       break;
        case QO_LONG_VEC:   tag = "LONG";      break;
        case QO_TIMESTAMP_VEC: tag = "TIMESTAMP"; break;
        case QO_TIMESPAN_VEC:  tag = "TIMESPAN";  break;
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
        case QO_OPERATOR:   tag = "operator";  break;
        case QO_BUILTIN:    tag = "builtin";   break;
        case QO_LIST:       tag = "list";      break;
        case QO_FUNCTION:   tag = "function";  break;
        case QO_EACHED:     tag = "each";      break;
        case QO_ADVERB:     tag = "adverb";    break;
        case QO_TABLE:      tag = "table";     break;
        default: break;
    }
    return make_symbol_value(tag);
}

static Qo eval_builtin_key(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_DICT) EVAL_ERROR("key expects a dictionary");
    return extract_dict_side(arg, 1);
}

static Qo eval_builtin_value(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL || qo_type(arg) != QO_DICT) EVAL_ERROR("value expects a dictionary");
    return extract_dict_side(arg, 0);
}

static Qo eval_builtin_exit(Qo arg, Environment *env) {
    (void)env;
    Qo a = arg;
    if (a == NULL) EVAL_ERROR("exit expects a numeric argument");
    if (qo_type(a) == QO_SHORT) { evaluator_request_exit(qo_short(a));       return qo_clone(a); }
    if (qo_type(a) == QO_INT)   { evaluator_request_exit(qo_int(a));         return qo_clone(a); }
    if (qo_type(a) == QO_LONG)  { evaluator_request_exit((long)qo_long(a));  return qo_clone(a); }
    if (qo_type(a) == QO_FLOAT) { evaluator_request_exit((long)qo_float(a)); return qo_clone(a); }
    EVAL_ERROR("exit expects a numeric argument");
}

static int enlist_scalar_vec_type(uint8_t scalar_type, uint8_t *out_vec_type) {
    if (!type_has_flag(scalar_type, TF_SCALAR)) return 0;
    *out_vec_type = type_base_type(scalar_type);
    return *out_vec_type != 0;
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
                for (int i = 0; i < arg_count; i++) QO_LIST_DATA(result)[i] = qo_clone(args[i]);
                return result;
            }

            {
                Qo result = alloc_data_vec(vec_type, (int64_t)arg_count);
                for (int i = 0; i < arg_count; i++) {
                    set_vec_elem_from_scalar(result, i, args[i]);
                }
                return result;
            }
        }

        /* All args are dicts with the same keys → promote to table */
        if (all_same && scalar_type == QO_DICT) {
            Qo first = args[0];
            int64_t nkeys = QO_DICT_COUNT(first);

            int same_keys = 1;
            for (int i = 1; i < arg_count && same_keys; i++) {
                if (QO_DICT_COUNT(args[i]) != nkeys) { same_keys = 0; break; }
                for (int64_t j = 0; j < nkeys; j++) {
                    if (!value_equals(QO_DICT_KEYS(args[i])[j], QO_DICT_KEYS(first)[j])) {
                        same_keys = 0; break;
                    }
                }
            }

            if (same_keys) {
                Qo dict = alloc_dict_block(nkeys);
                QO_DICT_KTYPE(dict) = QO_SYMBOL;
                QO_DICT_VTYPE(dict) = 0;

                for (int64_t j = 0; j < nkeys; j++) {
                    QO_DICT_KEYS(dict)[j] = qo_retain(QO_DICT_KEYS(first)[j]);

                    Qo *col_vals = xmalloc(sizeof(Qo) * (size_t)arg_count);
                    for (int i = 0; i < arg_count; i++)
                        col_vals[i] = QO_DICT_VALS(args[i])[j];
                    QO_DICT_VALS(dict)[j] = eval_builtin_enlist(col_vals, arg_count, env);
                    free(col_vals);
                }

                Qo table = alloc_table_block();
                QO_SET_COUNT(table, arg_count);
                QO_TABLE_DICT(table) = dict;
                return table;
            }
        }
    }

    {
        Qo result = alloc_ptr_vec(QO_LIST, (int64_t)arg_count);
        for (int i = 0; i < arg_count; i++) QO_LIST_DATA(result)[i] = qo_clone(args[i]);
        return result;
    }
}

static Qo make_charvec_from_buf(const char *buf, size_t n) {
    Qo r = alloc_charlike(QO_CHAR_VEC, (int64_t)n);
    if (n > 0) memcpy(qo_char_data(r), buf, n);
    return r;
}

/* Format a scalar Qo as a q-style string with no type suffix. */
static Qo format_scalar_as_string(Qo q) {
    char buf[64];
    int n = 0;
    if (q == NULL) return alloc_charlike(QO_CHAR_VEC, 0);

    uint8_t t = qo_type(q);
    switch (t) {
        case QO_SHORT: {
            int16_t v = qo_short(q);
            if (v == QO_SHORT_NULL) { memcpy(buf, "0N", 2); n = 2; }
            else n = snprintf(buf, sizeof buf, "%d", (int)v);
            break;
        }
        case QO_INT: {
            int32_t v = qo_int(q);
            if (v == QO_INT_NULL)        { memcpy(buf, "0N", 2);  n = 2; }
            else if (v == QO_INT_INF)    { memcpy(buf, "0W", 2);  n = 2; }
            else if (v == QO_INT_NEGINF) { memcpy(buf, "-0W", 3); n = 3; }
            else n = snprintf(buf, sizeof buf, "%d", (int)v);
            break;
        }
        case QO_LONG: {
            int64_t v = qo_long(q);
            if (v == QO_LONG_NULL)        { memcpy(buf, "0N", 2);  n = 2; }
            else if (v == QO_LONG_INF)    { memcpy(buf, "0W", 2);  n = 2; }
            else if (v == QO_LONG_NEGINF) { memcpy(buf, "-0W", 3); n = 3; }
            else n = snprintf(buf, sizeof buf, "%ld", (long)v);
            break;
        }
        case QO_TIMESTAMP: {
            int64_t v = qo_timestamp(q);
            if (v == QO_LONG_NULL) { memcpy(buf, "0N", 2); n = 2; }
            else {
                int64_t secs = v / 1000000000LL;
                int64_t nanos = v % 1000000000LL;
                if (nanos < 0) { nanos += 1000000000LL; secs--; }
                time_t tt = (time_t)secs;
                struct tm tm;
                gmtime_r(&tt, &tm);
                n = snprintf(buf, sizeof buf, "%04d.%02d.%02dT%02d:%02d:%02d.%09ld",
                             tm.tm_year + 1900, tm.tm_mon + 1, tm.tm_mday,
                             tm.tm_hour, tm.tm_min, tm.tm_sec, (long)nanos);
            }
            break;
        }
        case QO_TIMESPAN: {
            int64_t v = qo_timespan(q);
            if (v == QO_LONG_NULL) { memcpy(buf, "0N", 2); n = 2; }
            else {
                int neg = (v < 0);
                if (neg) v = -v;
                int64_t days = v / 86400000000000LL;
                int64_t rem = v % 86400000000000LL;
                int64_t hours = rem / 3600000000000LL;
                rem %= 3600000000000LL;
                int64_t minutes = rem / 60000000000LL;
                rem %= 60000000000LL;
                int64_t secs = rem / 1000000000LL;
                int64_t nanos = rem % 1000000000LL;
                if (neg)
                    n = snprintf(buf, sizeof buf, "-%ldT%02ld:%02ld:%02ld.%09ld", days, hours, minutes, secs, nanos);
                else
                    n = snprintf(buf, sizeof buf, "%ldT%02ld:%02ld:%02ld.%09ld", days, hours, minutes, secs, nanos);
            }
            break;
        }
        case QO_FLOAT: {
            double v = qo_float(q);
            if (isnan(v))      { memcpy(buf, "0N", 2);  n = 2; }
            else if (isinf(v)) {
                if (v > 0) { memcpy(buf, "0W", 2);  n = 2; }
                else       { memcpy(buf, "-0W", 3); n = 3; }
            } else n = snprintf(buf, sizeof buf, "%g", v);
            break;
        }
        case QO_CHAR:
            buf[0] = qo_char(q);
            n = 1;
            break;
        case QO_BOOL:
            buf[0] = qo_bool(q) ? '1' : '0';
            n = 1;
            break;
        case QO_BYTE:
            n = snprintf(buf, sizeof buf, "%02x", (unsigned int)qo_byte(q));
            break;
        case QO_SYMBOL: {
            const char *s = qo_symbol_name(q);
            return make_charvec_from_buf(s, strlen(s));
        }
        default:
            return alloc_charlike(QO_CHAR_VEC, 0);
    }
    if (n < 0) n = 0;
    if ((size_t)n > sizeof buf) n = (int)sizeof buf;
    return make_charvec_from_buf(buf, (size_t)n);
}

static Qo eval_builtin_string(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) return alloc_charlike(QO_CHAR_VEC, 0);

    uint8_t t = qo_type(arg);

    /* Scalar: produce a single string */
    if (type_has_flag(t, TF_SCALAR)) {
        return format_scalar_as_string(arg);
    }

    /* Mixed list: recurse atomically */
    if (t == QO_LIST) {
        int64_t n = qo_count(arg);
        Qo result = alloc_ptr_vec(QO_LIST, n);
        for (int64_t i = 0; i < n; i++) {
            qo_ptr_data(result)[i] = eval_builtin_string(qo_ptr_data(arg)[i], env);
        }
        return result;
    }

    /* Typed vector: build a scalar per element, format, collect into a list */
    if (type_has_flag(t, TF_VECTOR)) {
        int64_t n = qo_count(arg);
        Qo result = alloc_ptr_vec(QO_LIST, n);
        for (int64_t i = 0; i < n; i++) {
            Qo scalar = NULL;
            switch (t) {
                case QO_SHORT_VEC: scalar = make_short_value(qo_short_data(arg)[i]); break;
                case QO_INT_VEC:   scalar = make_int_value(qo_int_data(arg)[i]);     break;
                case QO_LONG_VEC:  scalar = make_long_value(qo_long_data(arg)[i]);   break;
                case QO_TIMESTAMP_VEC: scalar = make_timestamp_value(qo_timestamp_data(arg)[i]); break;
                case QO_TIMESPAN_VEC: scalar = make_timespan_value(qo_timespan_data(arg)[i]); break;
                case QO_FLOAT_VEC: scalar = make_float_value(qo_float_data(arg)[i]); break;
                case QO_BOOL_VEC:  scalar = make_bool_value(qo_bool_data(arg)[i]);   break;
                case QO_BYTE_VEC:  scalar = make_byte_value(qo_byte_data(arg)[i]);   break;
                case QO_CHAR_VEC:  scalar = make_char_value(qo_char_data(arg)[i]);   break;
                case QO_SYM_VEC:   scalar = qo_clone(QO_LIST_DATA(arg)[i]);          break;
                default: break;
            }
            qo_ptr_data(result)[i] = format_scalar_as_string(scalar);
            qo_release(scalar);
        }
        return result;
    }

    return alloc_charlike(QO_CHAR_VEC, 0);
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

static Qo eval_builtin_flip(Qo arg, Environment *env) {
    (void)env;
    uint8_t t = qo_type(arg);
    if (t == QO_DICT) {
        /* Flip dict → table */
        int64_t ncols = QO_DICT_COUNT(arg);
        int64_t nrows = 0;
        if (ncols > 0) {
            Qo first_val = QO_DICT_VALS(arg)[0];
            if (is_vector_type(qo_type(first_val)))
                nrows = QO_COUNT(first_val);
            else
                nrows = 1;
        }
        Qo table = alloc_table_block();
        QO_SET_COUNT(table, nrows);
        QO_TABLE_DICT(table) = qo_clone(arg);
        return table;
    }
    if (t == QO_TABLE) {
        /* Flip table → dict */
        Qo dict = QO_TABLE_DICT(arg);
        if (!dict) return alloc_dict_block(0);
        return qo_clone(dict);
    }
    EVAL_ERROR("flip expects a dict or table");
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
    return qo_clone(arg);
}

static Qo eval_builtin_last(Qo arg, Environment *env) {
    (void)env;
    if (arg == NULL) EVAL_ERROR("last on null");
    if (is_vector_type(qo_type(arg))) {
        int64_t n = qo_count(arg);
        if (n == 0) EVAL_ERROR("last on empty vector");
        return dict_elem_copy(arg, n - 1);
    }
    return qo_clone(arg);
}

static Qo eval_builtin_now(Qo arg, Environment *env) {
    struct timespec ts;
    int64_t now_ns;
    (void)arg; (void)env;
    if (timespec_get(&ts, TIME_UTC) != TIME_UTC) EVAL_ERROR("now: failed to read system clock");
    now_ns = (int64_t)ts.tv_sec * 1000000000LL + (int64_t)ts.tv_nsec;
    return make_timestamp_value(now_ns);
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
    int64_t needle_count;
    int result_is_scalar;
    int iter_needles = (nt == QO_LIST || nt == QO_SHORT_VEC || nt == QO_INT_VEC ||
                        nt == QO_LONG_VEC || nt == QO_FLOAT_VEC || nt == QO_SYM_VEC);
    if (needles == NULL || !iter_needles) {
        needle_count = 1;
        result_is_scalar = 1;
    } else {
        needle_count = qo_count(needles);
        result_is_scalar = 0;
    }

    Qo result = alloc_data_vec(QO_LONG_VEC, needle_count);

    for (int64_t i = 0; i < needle_count; i++) {
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
        qo_release(result);
        return make_long_value(val);
    }
    return result;
}

static Qo null_for_vector_type(uint8_t vec_type) {
    switch (type_storage(vec_type)) {
        case SC_I64: return make_long_value(QO_LONG_NULL);
        case SC_I32: return make_int_value(QO_INT_NULL);
        case SC_I16: return make_short_value(QO_SHORT_NULL);
        case SC_F64: return make_float_value(QO_FLOAT_NULL);
        case SC_U8:
            if (vec_type == QO_SYM_VEC) return make_symbol_value("");
            return make_bool_value(0);
        default:     return NULL;
    }
}

/* ── $ cast operator ──────────────────────────────────────────────────────── */

static const struct { const char *name; uint8_t type; } cast_type_names[] = {
    {"short",    QO_SHORT},
    {"int",      QO_INT},
    {"long",     QO_LONG},
    {"float",    QO_FLOAT},
    {"char",     QO_CHAR},
    {"bool",     QO_BOOL},
    {"byte",     QO_BYTE},
    {"symbol",   QO_SYMBOL},
    {"timespan", QO_TIMESPAN},
};

static int64_t scalar_as_int64(Qo v) {
    switch (type_storage(qo_type(v))) {
        case SC_I64: return qo_long(v);
        case SC_I32: return qo_int(v);
        case SC_I16: return qo_short(v);
        case SC_F64: return (int64_t)qo_float(v);
        case SC_U8:
            if (qo_type(v) == QO_CHAR) return (unsigned char)qo_char(v);
            return qo_bool(v);
        default: return 0;
    }
}

static int64_t vec_elem_as_int64(Qo v, int64_t i) {
    switch (type_storage(qo_type(v))) {
        case SC_I64: return qo_long_data(v)[i];
        case SC_I32: return qo_int_data(v)[i];
        case SC_I16: return qo_short_data(v)[i];
        case SC_F64: return (int64_t)qo_float_data(v)[i];
        case SC_U8:
            if (qo_type(v) == QO_CHAR_VEC) return (unsigned char)qo_char_data(v)[i];
            return qo_bool_data(v)[i];
        default:
            break;
    }
    Qo e = qo_ptr_data(v)[i];
    return e ? scalar_as_int64(e) : 0;
}

static double vec_elem_as_double(Qo v, int64_t i) {
    if (type_storage(qo_type(v)) == SC_F64) return qo_float_data(v)[i];
    return (double)vec_elem_as_int64(v, i);
}

static uint8_t vec_type_for_scalar(uint8_t st) {
    return type_base_type(st);
}

static Qo make_scalar_from_int64(uint8_t tt, int64_t i) {
    if (tt == QO_TIMESPAN) return make_timespan_value(i);
    switch (type_storage(tt)) {
        case SC_I16: return make_short_value((int16_t)i);
        case SC_I32: return make_int_value((int32_t)i);
        case SC_I64: return make_long_value(i);
        case SC_F64: return make_float_value((double)i);
        case SC_U8:
            if (tt == QO_CHAR) return make_char_value((char)i);
            if (tt == QO_BOOL) return make_bool_value(i != 0);
            return make_byte_value((uint8_t)i);
        default:     return NULL;
    }
}

#define VEC_CAST(vectype, allocfn, accessor, elem_type, cast_expr) \
    if (vt == (vectype)) { \
        Qo r = allocfn(vectype, n); \
        elem_type *dst = accessor(r); \
        for (int64_t i = 0; i < n; i++) dst[i] = (cast_expr); \
        return r; \
    }

static uint8_t char_to_cast_type(char c) {
    switch (c) {
        case 'c': return QO_CHAR;
        case 'x': return QO_BYTE;
        case 'h': return QO_SHORT;
        case 'i': return QO_INT;
        case 'j': return QO_LONG;
        case 'f': return QO_FLOAT;
        case 'b': return QO_BOOL;
        default:  return 0;
    }
}

static Qo eval_cast(Qo type_sym, Qo value, Environment *env) {
    (void)env;
    uint8_t tt = 0;
    if (type_sym != NULL && qo_type(type_sym) == QO_SYMBOL) {
        const char *name = qo_symbol_name(type_sym);
        for (size_t i = 0; i < sizeof(cast_type_names)/sizeof(cast_type_names[0]); i++) {
            if (strcmp(name, cast_type_names[i].name) == 0) { tt = cast_type_names[i].type; break; }
        }
    } else if (type_sym != NULL && qo_type(type_sym) == QO_CHAR) {
        tt = char_to_cast_type(qo_char(type_sym));
    } else if (type_sym != NULL && qo_type(type_sym) == QO_CHAR_VEC && qo_count(type_sym) == 1) {
        tt = char_to_cast_type(qo_char_data(type_sym)[0]);
    }
    if (tt == 0) EVAL_ERROR("cast: left argument must be a type symbol or type character");

    if (value == NULL) EVAL_ERROR("cast: cannot cast null");

    uint8_t st = qo_type(value);

    /* symbol <-> string */
    if (tt == QO_SYMBOL) {
        if (st == QO_CHAR_VEC) return make_symbol_value(qo_char_data(value));
        EVAL_ERROR("cast: can only cast string to symbol");
    }
    if (st == QO_SYMBOL) {
        if (tt == QO_CHAR) {
            const char *s = qo_symbol_name(value);
            int64_t len = (int64_t)strlen(s);
            Qo r = alloc_charlike(QO_CHAR_VEC, len);
            memcpy(qo_char_data(r), s, (size_t)len);
            return r;
        }
        EVAL_ERROR("cast: can only cast symbol to string");
    }

    int src_scalar = type_has_flag(st, TF_SCALAR) && !type_has_flag(st, TF_COMPLEX);
    if (!src_scalar && !is_vector_type(st))
        EVAL_ERROR("cast: unsupported source type");

    /* ── Scalar source ── */
    if (src_scalar) {
        if (st == tt) return qo_clone(value);
        Qo r = make_scalar_from_int64(tt, st == QO_FLOAT ? (int64_t)qo_float(value) : scalar_as_int64(value));
        if (r != NULL) return r;
        EVAL_ERROR("cast: unsupported target type");
    }

    /* ── Vector source (element-wise) ── */
    {
        int64_t n = qo_count(value);
        uint8_t vt = vec_type_for_scalar(tt);

        if (vt == st) return qo_clone(value);

        VEC_CAST(QO_LONG_VEC,  alloc_data_vec, qo_long_data,  int64_t, vec_elem_as_int64(value, i))
        VEC_CAST(QO_TIMESPAN_VEC, alloc_data_vec, qo_long_data, int64_t, vec_elem_as_int64(value, i))
        VEC_CAST(QO_INT_VEC,   alloc_data_vec, qo_int_data,   int32_t, (int32_t)vec_elem_as_int64(value, i))
        VEC_CAST(QO_SHORT_VEC, alloc_data_vec, qo_short_data, int16_t, (int16_t)vec_elem_as_int64(value, i))
        VEC_CAST(QO_FLOAT_VEC, alloc_data_vec, qo_float_data, double,  vec_elem_as_double(value, i))
        VEC_CAST(QO_BOOL_VEC,  alloc_data_vec, qo_bool_data,  uint8_t, (vec_elem_as_int64(value, i) != 0))
        VEC_CAST(QO_BYTE_VEC,  alloc_data_vec, qo_byte_data,  uint8_t, (uint8_t)vec_elem_as_int64(value, i))
        VEC_CAST(QO_CHAR_VEC,  alloc_charlike, qo_char_data,  char,    (char)vec_elem_as_int64(value, i))
        EVAL_ERROR("cast: unsupported target type");
    }
}

#undef VEC_CAST

static Qo eval_apply_keyword(Qo head, Qo *arg_values, int arg_count, Environment *env) {
    Qo result;

    for (int i = 0; i < arg_count; i++) {
        if (arg_values[i] != NULL && qo_type(arg_values[i]) == QO_PROJECTOR) {
            Qo *slots = xmalloc(sizeof(Qo) * (arg_count > 0 ? arg_count : 1));
            for (int j = 0; j < arg_count; j++) slots[j] = qo_clone(arg_values[j]);
            return make_projection_value(head, slots, arg_count);
        }
    }

    if (qo_type(head) == QO_OPERATOR) {
        TokenType op_token = (TokenType)QO_OPERATOR_OP(head);

        /* TOKEN_TABLE can have 0 column expressions (empty table), so it's exempt
           from the general arg_count < 2 projection-creation logic. */
        if (op_token == TOKEN_TABLE) {
            Qo names = arg_values[0];
            int64_t ncols = arg_count - 1;

            Qo dict = alloc_dict_block(ncols);
            QO_DICT_KTYPE(dict) = QO_SYMBOL;
            QO_DICT_VTYPE(dict) = 0;

            int64_t nrows = 0;
            for (int64_t i = 0; i < ncols; i++) {
                QO_DICT_KEYS(dict)[i] = qo_retain(QO_LIST_DATA(names)[i]);
                Qo col = arg_values[1 + i];
                QO_DICT_VALS(dict)[i] = qo_retain(col);
                if (is_vector_type(qo_type(col))) {
                    int64_t cnt = QO_COUNT(col);
                    if (nrows == 0) nrows = cnt;
                }
            }

            /* If all columns are scalars, treat as single-row table */
            if (nrows == 0 && ncols > 0) nrows = 1;

            Qo table = alloc_table_block();
            QO_SET_COUNT(table, nrows);
            QO_TABLE_DICT(table) = dict;

            return table;
        }

        if (arg_count < 2) {
            Qo *slots = xmalloc(sizeof(Qo) * 2);
            slots[0] = make_projector_value();
            slots[1] = make_projector_value();
            for (int i = 0; i < arg_count; i++) { qo_release(slots[i]); slots[i] = qo_clone(arg_values[i]); }
            return make_projection_value(head, slots, 2);
        }
        switch (op_token) {
            case TOKEN_BANG:       return eval_dict_creation(arg_values[0], arg_values[1]);
            case TOKEN_COMMA:      return eval_comma_binop(arg_values[0], arg_values[1]);
            case TOKEN_HASH:       return eval_take(arg_values[0], arg_values[1]);
            case TOKEN_UNDERSCORE: return eval_drop(arg_values[0], arg_values[1]);
            case TOKEN_EQUAL:      return eval_equals(arg_values[0], arg_values[1]);
            case TOKEN_LESS:       return eval_less(arg_values[0], arg_values[1]);
            case TOKEN_GREATER:    return eval_greater(arg_values[0], arg_values[1]);
            case TOKEN_LE:         return eval_lte(arg_values[0], arg_values[1]);
            case TOKEN_GE:         return eval_gte(arg_values[0], arg_values[1]);
            case TOKEN_PIPE:       return eval_max_of(arg_values[0], arg_values[1]);
            case TOKEN_AMP:        return eval_min_of(arg_values[0], arg_values[1]);
            case TOKEN_PLUS:       return eval_add(arg_values[0], arg_values[1]);
            case TOKEN_MINUS:      return eval_subtract(arg_values[0], arg_values[1]);
            case TOKEN_STAR:       return eval_multiply(arg_values[0], arg_values[1]);
            case TOKEN_DIVIDE:    return eval_divide(arg_values[0], arg_values[1]);
            case TOKEN_STAR_STAR:  return eval_power(arg_values[0], arg_values[1]);
            case TOKEN_QUESTION:  return eval_builtin_find(arg_values[0], arg_values[1], env);
            case TOKEN_AT:
                return eval_apply_value(arg_values[0], &arg_values[1], 1, env);
            case TOKEN_DOT: {
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
                        for (int64_t i = 0; i < n; i++) qo_release(spread[i]);
                    }
                    free(spread);
                    return result;
                }
                return eval_apply_value(func, &rhs, 1, env);
            }
            case TOKEN_DOT_DOT:
                return eval_builtin_range(arg_values[0], arg_values[1], env);
            case TOKEN_DOT_DOT_EQ:
                return eval_builtin_range_inclusive(arg_values[0], arg_values[1], env);
            case TOKEN_DOLLAR:
                return eval_cast(arg_values[0], arg_values[1], env);
            case TOKEN_SLASH:
                EVAL_ERROR("operator '/' is undefined");
            default:
                EVAL_ERROR("unknown operator");
        }
    }

    if (qo_type(head) == QO_BUILTIN) {
        uint8_t id = QO_BUILTIN_ID(head);
        switch (id) {
            case QO_BUILTIN_HOPEN: {
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
                if (ipc_server_fd() >= 0) {
                    ipc_accept_connection();
                }
                return make_int_value(fd);
            }
            case QO_BUILTIN_FIND:
                if (arg_count != 2) EVAL_ERROR("find expects exactly 2 arguments");
                return eval_builtin_find(arg_values[0], arg_values[1], env);
            case QO_BUILTIN_ENLIST:
                return eval_builtin_enlist(arg_values, arg_count, env);
            case QO_BUILTIN_SEMICOLON:
                EVAL_ERROR("unexpected semicolon in apply");
            default:
                if (arg_count != 1)
                    EVAL_ERROR_FMT("builtin expects exactly 1 argument, got %d", arg_count);
                switch (id) {
                    case QO_BUILTIN_SUM:      return eval_builtin_sum(arg_values[0], env);
                    case QO_BUILTIN_COUNT:    return eval_builtin_count(arg_values[0], env);
                    case QO_BUILTIN_MIN:      return eval_builtin_min(arg_values[0], env);
                    case QO_BUILTIN_MAX:      return eval_builtin_max(arg_values[0], env);
                    case QO_BUILTIN_TIL:      return eval_builtin_til(arg_values[0], env);
                    case QO_BUILTIN_PARSE:    return eval_builtin_parse(arg_values[0], env);
                    case QO_BUILTIN_LEX:      return eval_builtin_lex(arg_values[0], env);
                    case QO_BUILTIN_NOT:      return eval_builtin_not(arg_values[0], env);
                    case QO_BUILTIN_TYPE:     return eval_builtin_type(arg_values[0], env);
                    case QO_BUILTIN_KEY:      return eval_builtin_key(arg_values[0], env);
                    case QO_BUILTIN_VALUE:    return eval_builtin_value(arg_values[0], env);
                    case QO_BUILTIN_PRINT:    return eval_builtin_print(arg_values[0], env);
                    case QO_BUILTIN_EXIT:     return eval_builtin_exit(arg_values[0], env);
                    case QO_BUILTIN_EVAL:     return eval_builtin_eval(arg_values[0], env);
                    case QO_BUILTIN_FIRST:    return eval_builtin_first(arg_values[0], env);
                    case QO_BUILTIN_LAST:     return eval_builtin_last(arg_values[0], env);
                    case QO_BUILTIN_NOW:      return eval_builtin_now(arg_values[0], env);
                    case QO_BUILTIN_READ:     return eval_builtin_read(arg_values[0], env);
                    case QO_BUILTIN_SHELL:    return eval_builtin_shell(arg_values[0], env);
                    case QO_BUILTIN_SER:      return eval_builtin_ser(arg_values[0], env);
                    case QO_BUILTIN_DESER:    return eval_builtin_deser(arg_values[0], env);
                    case QO_BUILTIN_LISTEN:   return eval_builtin_listen(arg_values[0], env);
                    case QO_BUILTIN_HCLOSE:   return eval_builtin_hclose(arg_values[0], env);
                    case QO_BUILTIN_REFCOUNT: return eval_builtin_refcount(arg_values[0], env);
                    case QO_BUILTIN_NULL:     return eval_builtin_null(arg_values[0], env);
                    case QO_BUILTIN_STRING:   return eval_builtin_string(arg_values[0], env);
                    case QO_BUILTIN_FLIP:     return eval_builtin_flip(arg_values[0], env);
                    case QO_BUILTIN_WHERE:    return eval_builtin_where(arg_values[0], env);
                    default:
                        EVAL_ERROR_FMT("unknown builtin id %d", id);
                }
        }
    }

    EVAL_ERROR("unknown keyword type");
}

static Qo eval_builtin_assign(Qo *args, int arg_count, Environment *env) {
    if (arg_count != 2) EVAL_ERROR("assignment requires exactly 2 arguments");
    Qo target = args[0];

    if (target == NULL || qo_type(target) != QO_SYMBOL) EVAL_ERROR("assignment target must be a symbol");

    /* Namespace assignment: a.b:5 — walk the dotted chain, creating nested dicts */
    const char *name = qo_symbol_name(target);
    const char *dot = strchr(name, '.');
    if (dot) {
        int comp_count = 1;
        for (const char *p = name; *p; p++) if (*p == '.') comp_count++;

        Qo *comps = xmalloc(sizeof(Qo) * (size_t)comp_count);
        const char *rest = name;
        for (int i = 0; i < comp_count; i++) {
            const char *d = strchr(rest, '.');
            int len = d ? (int)(d - rest) : (int)strlen(rest);
            char *buf = strndup(rest, (size_t)len);
            comps[i] = qo_symbol_intern(buf);
            free(buf);
            rest = d ? d + 1 : rest + len;
        }

        Qo *dict_stack = xmalloc(sizeof(Qo) * (size_t)comp_count);
        int64_t *idx_stack = xmalloc(sizeof(int64_t) * (size_t)comp_count);
        int depth = 0;

        /* First component: get or create root dict */
        int found;
        Qo current = env_get(env, qo_symbol_id(comps[0]), &found);
        if (!found || current == NULL) {
            current = alloc_dict_block(0);
            QO_DICT_KTYPE(current) = QO_SYMBOL;
            QO_DICT_VTYPE(current) = QO_DICT;
            env_set(env_root(env), qo_symbol_id(comps[0]), current);
            qo_release(current);
            current = env_get(env, qo_symbol_id(comps[0]), &found);
        } else if (qo_type(current) != QO_DICT) {
            qo_release(current);
            free(comps); free(dict_stack); free(idx_stack);
            EVAL_ERROR("namespace must be a dictionary");
        }
        dict_stack[0] = qo_retain(current);
        idx_stack[0] = -1;
        depth = 1;

        /* Walk the chain */
        for (int ci = 1; ci < comp_count; ci++) {
            int64_t n = QO_DICT_COUNT(current);
            int64_t kidx = -1;
            for (int64_t j = 0; j < n; j++) {
                if (value_equals(QO_DICT_KEYS(current)[j], comps[ci])) {
                    kidx = j;
                    break;
                }
            }

            if (kidx < 0) {
                /* Missing key: build the remaining chain and insert */
                Qo val = eval_value(args[1], env);
                if (evaluator_error_requested() || evaluator_exit_requested()) {
                    for (int k = 0; k < depth; k++) qo_release(dict_stack[k]);
                    free(comps); free(dict_stack); free(idx_stack);
                    return val;
                }

                /* Build the value to insert at key comps[ci].
                   If ci is the last component, the value is val itself.
                   Otherwise, build a nested dict chain from comps[ci+1] down. */
                Qo value_to_insert;
                if (ci == comp_count - 1) {
                    value_to_insert = qo_retain(val);
                } else {
                    /* Innermost leaf: {comps[-1]: val} */
                    Qo leaf = alloc_dict_block(1);
                    QO_DICT_KTYPE(leaf) = QO_SYMBOL;
                    QO_DICT_VTYPE(leaf) = qo_type(val);
                    QO_DICT_KEYS(leaf)[0] = qo_retain(comps[comp_count - 1]);
                    QO_DICT_VALS(leaf)[0] = qo_retain(val);

                    /* Wrap from right to left, stopping just before ci */
                    Qo chain = leaf;
                    for (int i = comp_count - 2; i > ci; i--) {
                        Qo outer = alloc_dict_block(1);
                        QO_DICT_KTYPE(outer) = QO_SYMBOL;
                        QO_DICT_VTYPE(outer) = QO_DICT;
                        QO_DICT_KEYS(outer)[0] = qo_retain(comps[i]);
                        QO_DICT_VALS(outer)[0] = chain;
                        chain = outer;
                    }
                    value_to_insert = chain;
                }

                /* Insert value_to_insert at key comps[ci] into current */
                Qo new_current = alloc_dict_block(n + 1);
                QO_DICT_KTYPE(new_current) = QO_DICT_KTYPE(current);
                /* When parent is empty, derive VTYPE from the inserted value;
                   otherwise copy the existing VTYPE (all values share one type). */
                QO_DICT_VTYPE(new_current) = (n > 0) ? QO_DICT_VTYPE(current) : qo_type(value_to_insert);
                for (int64_t j = 0; j < n; j++) {
                    QO_DICT_KEYS(new_current)[j] = qo_retain(QO_DICT_KEYS(current)[j]);
                    QO_DICT_VALS(new_current)[j] = qo_retain(QO_DICT_VALS(current)[j]);
                }
                QO_DICT_KEYS(new_current)[n] = qo_retain(comps[ci]);
                QO_DICT_VALS(new_current)[n] = value_to_insert;

                /* Replace current in parent (or env if root) */
                if (depth == 1) {
                    env_set(env_root(env), qo_symbol_id(comps[0]), new_current);
                } else {
                    Qo parent = dict_stack[depth - 2];
                    int64_t pidx = idx_stack[depth - 1];
                    qo_release(QO_DICT_VALS(parent)[pidx]);
                    QO_DICT_VALS(parent)[pidx] = qo_retain(new_current);
                }

                for (int k = 0; k < depth; k++) qo_release(dict_stack[k]);
                free(comps); free(dict_stack); free(idx_stack);
                return val;
            }

            /* Key exists */
            Qo child = QO_DICT_VALS(current)[kidx];

            if (ci == comp_count - 1) {
                /* Last component: update existing value in place */
                Qo val = eval_value(args[1], env);
                if (evaluator_error_requested() || evaluator_exit_requested()) {
                    for (int k = 0; k < depth; k++) qo_release(dict_stack[k]);
                    free(comps); free(dict_stack); free(idx_stack);
                    return val;
                }
                qo_release(QO_DICT_VALS(current)[kidx]);
                QO_DICT_VALS(current)[kidx] = qo_retain(val);
                for (int k = 0; k < depth; k++) qo_release(dict_stack[k]);
                free(comps); free(dict_stack); free(idx_stack);
                return val;
            }

            /* Middle: descend into child dict */
            if (qo_type(child) != QO_DICT) {
                for (int k = 0; k < depth; k++) qo_release(dict_stack[k]);
                free(comps); free(dict_stack); free(idx_stack);
                EVAL_ERROR("namespace must be a dictionary");
            }

            dict_stack[depth] = qo_retain(current);
            idx_stack[depth] = kidx;
            depth++;
            current = child;
        }

        /* Should not reach here (handled above) */
        for (int k = 0; k < depth; k++) qo_release(dict_stack[k]);
        free(comps); free(dict_stack); free(idx_stack);
        return NULL;
    }

    Qo result = eval_value(args[1], env);
    if (evaluator_error_requested() || evaluator_exit_requested()) return result;
    env_set(env, qo_symbol_id(target), result);
    return result;
}

/* Nested namespace read: a.b.c — walk dict chain */
static Qo eval_nested_read(Qo full_sym, Environment *env) {
    const char *name = qo_symbol_name(full_sym);
    const char *dot = strchr(name, '.');
    if (!dot) {
        int found;
        Qo v = env_get(env, qo_symbol_id(full_sym), &found);
        if (found) return v;
        EVAL_ERROR_FMT("undefined variable '%s'", name);
    }

    /* First component: look up in env */
    int ns_len = (int)(dot - name);
    char *ns_name = strndup(name, (size_t)ns_len);
    Qo ns_sym = qo_symbol_intern(ns_name);

    int found;
    Qo current = env_get(env, qo_symbol_id(ns_sym), &found);
    if (!found) {
        const char *saved_name = qo_symbol_name(ns_sym);
        free(ns_name);
        EVAL_ERROR_FMT("undefined variable '%s'", saved_name);
    }
    free(ns_name);

    const char *rest = dot + 1;

    /* Walk remaining dots */
    while (1) {
        const char *next_dot = strchr(rest, '.');
        int key_len = next_dot ? (int)(next_dot - rest) : (int)strlen(rest);
        char *key_name = strndup(rest, (size_t)key_len);
        Qo key_sym = qo_symbol_intern(key_name);
        free(key_name);

        if (qo_type(current) != QO_DICT) {
            qo_release(current);
            EVAL_ERROR("namespace must be a dictionary");
        }

        int64_t n = QO_DICT_COUNT(current);
        Qo found_val = NULL;
        int val_found = 0;
        for (int64_t i = 0; i < n; i++) {
            if (value_equals(QO_DICT_KEYS(current)[i], key_sym)) {
                found_val = qo_clone(QO_DICT_VALS(current)[i]);
                val_found = 1;
                break;
            }
        }

        if (!next_dot) {
            qo_release(current);
            if (!val_found) EVAL_ERROR_FMT("key '%s' not found in namespace", qo_symbol_name(key_sym));
            return found_val;
        }

        qo_release(current);
        if (!val_found) EVAL_ERROR_FMT("key '%s' not found in namespace", qo_symbol_name(key_sym));

        current = found_val;
        rest = next_dot + 1;
    }
}

/* ── projection helpers ──────────────────────────────────────────────────── */

static Qo make_projection_value(Qo function, Qo *slots, int64_t slot_count) {
    size_t sz = 16 + (size_t)slot_count * sizeof(Qo);
    Qo p = qo_alloc_raw(sz);
    p->type_tag = QO_PROJECTION;
    p->attribute = 0;
    QO_PROJ_AC(p) = slot_count;
    QO_PROJ_FUNC(p) = qo_clone(function);
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
        if (i > 0) qo_release(last);
        last = cur;
        if (evaluator_exit_requested() || evaluator_error_requested()) break;
    }
    env_free(call_env);
    return last;
}

static void free_evaluated_args(Qo *args, int count) {
    for (int i = 0; i < count; i++) qo_release(args[i]);
    free(args);
}

static int eval_args_or_stop(Qo tree, int start_index, int arg_count, Environment *env, Qo *args) {
    for (int i = 0; i < arg_count; i++) {
        args[i] = eval_value(QO_LIST_DATA(tree)[start_index + i], env);
        if (evaluator_error_requested() || evaluator_exit_requested()) {
            for (int j = 0; j <= i; j++) qo_release(args[j]);
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
        uint8_t vec_type = type_has_flag(first_type, TF_SCALAR) ? type_base_type(first_type) : 0;
        if (vec_type == 0) all_same = 0;

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
                    set_vec_elem_from_scalar(result, i, values[i]);
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

static Qo eval_apply_multi_index(Qo head, Qo indices, Environment *env) {
    (void)env;
    if (head == NULL) EVAL_ERROR("cannot index null");
    uint8_t ht = qo_type(head);
    if (!is_vector_type(ht) && ht != QO_DICT) EVAL_ERROR("value is not indexable");

    uint8_t it = qo_type(indices);
    int64_t n = qo_count(indices);
    int64_t head_n = (ht == QO_DICT) ? QO_DICT_COUNT(head) : qo_count(head);
    int is_ptr = (ht == QO_LIST || ht == QO_SYM_VEC || ht == QO_DICT);

    Qo result;
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
            if (it == QO_SHORT_VEC) key = make_short_value(qo_short_data(indices)[i]);
            else if (it == QO_INT_VEC) key = make_int_value(qo_int_data(indices)[i]);
            else if (it == QO_LONG_VEC) key = make_long_value(qo_long_data(indices)[i]);
            else if (it == QO_FLOAT_VEC) key = make_float_value(qo_float_data(indices)[i]);
            else if (it == QO_BOOL_VEC) key = make_bool_value(qo_bool_data(indices)[i]);
            else key = qo_clone(qo_ptr_data(indices)[i]);

            int found = 0;
            for (int64_t j = 0; j < head_n; j++) {
                if (value_equals(QO_DICT_KEYS(head)[j], key)) {
                    qo_ptr_data(result)[i] = qo_clone(QO_DICT_VALS(head)[j]);
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
            if (it == QO_SHORT_VEC) idx = qo_short_data(indices)[i];
            else if (it == QO_INT_VEC) idx = qo_int_data(indices)[i];
            else if (it == QO_LONG_VEC) idx = qo_long_data(indices)[i];
            else if (it == QO_BOOL_VEC) idx = qo_bool_data(indices)[i];
            else {
                Qo e = qo_ptr_data(indices)[i];
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
                else {
                    switch (type_storage(ht)) {
                        case SC_I64: qo_long_data(result)[i] = QO_LONG_NULL; break;
                        case SC_I32: qo_int_data(result)[i] = QO_INT_NULL; break;
                        case SC_I16: qo_short_data(result)[i] = QO_SHORT_NULL; break;
                        case SC_F64: qo_float_data(result)[i] = QO_FLOAT_NULL; break;
                        default:     qo_bool_data(result)[i] = 0; break;
                    }
                }
                continue;
            }

            if (is_ptr) {
                qo_ptr_data(result)[i] = dict_elem_copy(head, idx);
            } else {
                Qo elem = dict_elem_copy(head, idx);
                set_vec_elem_from_scalar(result, i, elem);
                qo_release(elem);
            }
        }
    }
    return result;
}

static Qo eval_apply_single_index(Qo head, Qo index, Environment *env) {
    (void)env;
    if (index != NULL &&
        qo_type(index) != QO_SHORT &&
        qo_type(index) != QO_INT &&
        qo_type(index) != QO_LONG) {
        if (head != NULL && qo_type(head) == QO_DICT) {
            int64_t n = QO_DICT_COUNT(head);
            for (int64_t i = 0; i < n; i++) {
                if (value_equals(QO_DICT_KEYS(head)[i], index))
                    return qo_clone(QO_DICT_VALS(head)[i]);
            }
            EVAL_ERROR("dictionary key not found");
        }
        EVAL_ERROR("index must be an int");
    }

    if (index == NULL) EVAL_ERROR("index must be an int");
    long idx;
    if (qo_type(index) == QO_SHORT) idx = qo_short(index);
    else if (qo_type(index) == QO_INT) idx = qo_int(index);
    else idx = (long)qo_long(index);
    if (idx < 0) return make_long_value(QO_LONG_NULL);

    if (head == NULL) EVAL_ERROR("cannot index null");
    uint8_t ht = qo_type(head);

    if (is_vector_type(ht)) {
        int64_t n = qo_count(head);
        if ((int64_t)idx >= n) return null_for_vector_type(ht);
        return dict_elem_copy(head, (int64_t)idx);
    }

    if (ht == QO_DICT) {
        int64_t n = QO_DICT_COUNT(head);
        for (int64_t i = 0; i < n; i++) {
            if (value_equals(QO_DICT_KEYS(head)[i], index))
                return qo_clone(QO_DICT_VALS(head)[i]);
        }
        EVAL_ERROR("dictionary key not found");
    }

    EVAL_ERROR("value is not callable/indexable");
}

static Qo eval_apply_value(Qo head, Qo *args, int arg_count, Environment *env) {
    if (head == NULL) EVAL_ERROR("cannot apply null value");

    if (qo_type(head) == QO_OPERATOR || qo_type(head) == QO_BUILTIN) {
        return eval_apply_keyword(head, args, arg_count, env);
    }

    if (qo_type(head) == QO_ADVERB) {
        if (arg_count != 1) EVAL_ERROR("adverb expects 1 argument");
        uint8_t kind = QO_ADVERB_KIND(head);
        if (kind == QO_ADVERB_EACH) return make_eached_value(args[0]);
        EVAL_ERROR("unknown adverb");
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
                merged[i] = qo_clone(args[fill_idx++]);
            } else {
                merged[i] = qo_clone(slot);
            }
        }
        for (int64_t i = 0; i < slot_count; i++) {
            if (merged[i] != NULL && qo_type(merged[i]) == QO_PROJECTOR) {
                return make_projection_value(QO_PROJ_FUNC(head), merged, slot_count);
            }
        }
        Qo func = QO_PROJ_FUNC(head);
        uint8_t ft = qo_type(func);
        Qo result;
        if (ft == QO_OPERATOR || ft == QO_BUILTIN) {
            result = eval_apply_keyword(func, merged, (int)slot_count, env);
        } else {
            result = eval_call_function(func, merged, (int)slot_count, env);
        }
        for (int64_t i = 0; i < slot_count; i++) qo_release(merged[i]);
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
            for (int i = 0; i < arg_count; i++) { qo_release(slots[i]); slots[i] = qo_clone(args[i]); }
            return make_projection_value(head, slots, pc);
        }
        return eval_call_function(head, args, arg_count, env);
    }

    if (arg_count != 1) EVAL_ERROR("application expects exactly one argument");

    /* Handle apply: QO_INT as IPC file descriptor */
    if (head != NULL && qo_type(head) == QO_INT) {
        return ipc_handle_apply(qo_int(head), args[0]);
    }

    /* Multi-index: numeric vector, bool vector, or list of indices */
    if (args[0] != NULL) {
        uint8_t it = qo_type(args[0]);
        if (is_numeric_vector_type(it) || it == QO_LIST) {
            return eval_apply_multi_index(head, args[0], env);
        }
    }

    /* Single index (scalar int or dict key lookup) */
    return eval_apply_single_index(head, args[0], env);
}

/* ── eval_value ──────────────────────────────────────────────────────────── */
Qo eval_value(Qo tree, Environment *env) {
    if (evaluator_error_requested() || evaluator_exit_requested()) return NULL;

    if (tree == NULL) return NULL;

    /* Data vectors return as-is; lists are executable AST nodes and must continue. */
    if (is_vector_type(qo_type(tree)) && qo_type(tree) != QO_LIST) {
        return qo_clone(tree);
    }

    /* Symbol: variable lookup (dotted names for namespace field access) */
    if (qo_type(tree) == QO_SYMBOL) {
        const char *name = qo_symbol_name(tree);
        if (strchr(name, '.'))
            return eval_nested_read(tree, env);
        int found = 0;
        Qo v = env_get(env, qo_symbol_id(tree), &found);
        if (found) return v;
        EVAL_ERROR_FMT("undefined variable '%s'", qo_symbol_name(tree));
    }

    /* Non-list: return as-is */
    if (qo_type(tree) != QO_LIST) return qo_clone(tree);

    int64_t n = qo_count(tree);
    if (n == 0) return qo_clone(tree);

    /* Single-element list: return its contents */
    if (n == 1) return qo_clone(QO_LIST_DATA(tree)[0]);

    Qo verb_val = QO_LIST_DATA(tree)[0];
    int64_t arg_count_64 = n - 1;
    if (arg_count_64 > INT32_MAX) EVAL_ERROR("too many arguments");
    int arg_count = (int)arg_count_64;
    Qo result;

    uint8_t vt = verb_val ? qo_type(verb_val) : 0;
    if (verb_val == NULL || (vt != QO_OPERATOR && vt != QO_BUILTIN)) {
        /* Non-keyword head: evaluate and apply */
        Qo head = eval_value(verb_val, env);
        Qo *args = xmalloc(sizeof(Qo) * arg_count);
        if (evaluator_error_requested() || evaluator_exit_requested()) {
            qo_release(head); free(args); return NULL;
        }
        if (!eval_args_or_stop(tree, 1, arg_count, env, args)) {
            qo_release(head); free(args); return NULL;
        }
        result = eval_apply_value(head, args, arg_count, env);
        qo_release(head);
        free_evaluated_args(args, arg_count);
        return result;
    }

    if (vt == QO_OPERATOR) {
        TokenType op = (TokenType)QO_OPERATOR_OP(verb_val);
        if (op == TOKEN_COLON)
            return eval_builtin_assign(&QO_LIST_DATA(tree)[1], arg_count, env);

        Qo *arg_values = xmalloc(sizeof(Qo) * (arg_count > 0 ? arg_count : 1));
        if (!eval_args_or_stop(tree, 1, arg_count, env, arg_values)) { free(arg_values); return NULL; }
        result = eval_apply_keyword(verb_val, arg_values, arg_count, env);
        free_evaluated_args(arg_values, arg_count);
        return result;
    }

    if (vt == QO_BUILTIN) {
        uint8_t id = QO_BUILTIN_ID(verb_val);
        if (id == QO_BUILTIN_SEMICOLON) {
            Qo last = NULL;
            for (int i = 0; i < arg_count; i++) {
                Qo cur = eval_value(QO_LIST_DATA(tree)[i + 1], env);
                if (i > 0) qo_release(last);
                last = cur;
                if (evaluator_exit_requested() || evaluator_error_requested()) break;
            }
            return last;
        }
        Qo *arg_values = xmalloc(sizeof(Qo) * (arg_count > 0 ? arg_count : 1));
        if (!eval_args_or_stop(tree, 1, arg_count, env, arg_values)) { free(arg_values); return NULL; }
        result = eval_apply_keyword(verb_val, arg_values, arg_count, env);
        free_evaluated_args(arg_values, arg_count);
        return result;
    }

    return qo_clone(verb_val);
}

