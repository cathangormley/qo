#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evaluator_internal.h"
#include "internal.h"
#include "symbol_intern.h"

static void qo_destroy(Qo q);

/* ── Type information table ──────────────────────────────────────────────── */
const TypeInfo type_table[256] = {
    /* scalars */
    [QO_SHORT]       = {2, SC_I16, TF_SCALAR | TF_NUMERIC | TF_INTEGER, QO_SHORT_VEC},
    [QO_INT]         = {4, SC_I32, TF_SCALAR | TF_NUMERIC | TF_INTEGER, QO_INT_VEC},
    [QO_LONG]        = {8, SC_I64, TF_SCALAR | TF_NUMERIC | TF_INTEGER, QO_LONG_VEC},
    [QO_TIMESTAMP]   = {8, SC_I64, TF_SCALAR | TF_NUMERIC | TF_INTEGER, QO_TIMESTAMP_VEC},
    [QO_TIMESPAN]    = {8, SC_I64, TF_SCALAR | TF_NUMERIC | TF_INTEGER, QO_TIMESPAN_VEC},
    [QO_FLOAT]       = {8, SC_F64, TF_SCALAR | TF_NUMERIC,              QO_FLOAT_VEC},
    [QO_CHAR]        = {1, SC_U8,  TF_SCALAR,                           QO_CHAR_VEC},
    [QO_BOOL]        = {1, SC_U8,  TF_SCALAR | TF_NUMERIC,              QO_BOOL_VEC},
    [QO_BYTE]        = {1, SC_U8,  TF_SCALAR | TF_NUMERIC | TF_INTEGER, QO_BYTE_VEC},
    [QO_SYMBOL]      = {8, SC_PTR, TF_SCALAR,                           QO_SYM_VEC},
    [QO_OPERATOR]    = {1, SC_U8,  TF_SCALAR,                           0},
    [QO_PROJECTOR]   = {1, SC_U8,  TF_SCALAR,                           0},
    [QO_BUILTIN]     = {1, SC_U8,  TF_SCALAR,                           0},
    [QO_ADVERB]      = {1, SC_U8,  TF_SCALAR,                           0},
    /* data vectors */
    [QO_SHORT_VEC]   = {2, SC_I16, TF_VECTOR | TF_NUMERIC | TF_INTEGER, QO_SHORT},
    [QO_INT_VEC]     = {4, SC_I32, TF_VECTOR | TF_NUMERIC | TF_INTEGER, QO_INT},
    [QO_LONG_VEC]    = {8, SC_I64, TF_VECTOR | TF_NUMERIC | TF_INTEGER, QO_LONG},
    [QO_TIMESTAMP_VEC] = {8, SC_I64, TF_VECTOR | TF_NUMERIC | TF_INTEGER, QO_TIMESTAMP},
    [QO_TIMESPAN_VEC]  = {8, SC_I64, TF_VECTOR | TF_NUMERIC | TF_INTEGER, QO_TIMESPAN},
    [QO_FLOAT_VEC]   = {8, SC_F64, TF_VECTOR | TF_NUMERIC,              QO_FLOAT},
    [QO_CHAR_VEC]    = {1, SC_U8,  TF_VECTOR,                           QO_CHAR},
    [QO_BOOL_VEC]    = {1, SC_U8,  TF_VECTOR | TF_NUMERIC,              QO_BOOL},
    [QO_BYTE_VEC]    = {1, SC_U8,  TF_VECTOR | TF_NUMERIC | TF_INTEGER, QO_BYTE},
    [QO_SYM_VEC]     = {8, SC_PTR, TF_VECTOR,                           QO_SYMBOL},
    /* pointer vectors */
    [QO_LIST]        = {8, SC_PTR, TF_VECTOR | TF_COMPLEX,              0},
    /* complex types */
    [QO_DICT]        = {0, SC_PTR, TF_COMPLEX,                          0},
    [QO_FUNCTION]    = {0, SC_PTR, TF_COMPLEX,                          0},
    [QO_PROJECTION]  = {0, SC_PTR, TF_COMPLEX,                          0},
    [QO_ADVERBED]    = {0, SC_PTR, TF_COMPLEX,                          0},
    [QO_TABLE]       = {0, SC_PTR, TF_COMPLEX,                          0},
};

static Qo alloc_block(size_t payload_size) {
    Qo q = (Qo)xmalloc(sizeof(QoObject) + payload_size);
    q->attribute = 0;
    q->type_tag = 0;
    q->reference_count = 1;
    return q;
}

Qo alloc_atom(uint8_t type) {
    Qo q = (Qo)xmalloc(sizeof(QoObject));
    q->attribute = 0;
    q->type_tag = type;
    q->reference_count = 1;
    return q;
}

Qo make_scalar_value(uint8_t type, const void *value) {
    Qo q = alloc_atom(type);
    size_t sz = type_elem_size(type);
    if (sz > 0) memcpy(&q->long_val, value, sz);
    return q;
}

Qo qo_alloc_raw(size_t size) {
    return alloc_block(size);
}

Qo qo_retain(Qo q) {
    if (q != NULL) {
        q->reference_count += 1;
    }
    return q;
}

uint32_t qo_refcount(Qo q) {
    if (q == NULL) return 0;
    return q->reference_count;
}

void qo_release(Qo q) {
    if (q == NULL) return;
    if (q->reference_count == 0) return;
    q->reference_count -= 1;
    if (q->reference_count == 0) {
        qo_destroy(q);
    }
}

double value_as_double(Qo q) {
    switch (type_storage(qo_type(q))) {
        case SC_I64: return (double)qo_long(q);
        case SC_I32: return (double)qo_int(q);
        case SC_I16: return (double)qo_short(q);
        case SC_F64: return qo_float(q);
        case SC_U8:  return (double)qo_bool(q);
        default: return 0.0;
    }
}

Qo alloc_ptr_vec(uint8_t type, int64_t count) {
    size_t size = (count > 0 ? (size_t)count : 1) * sizeof(Qo);
    Qo q = alloc_block(size);
    q->type_tag = type;
    QO_SET_COUNT(q, count);
    return q;
}

Qo alloc_data_vec(uint8_t type, int64_t count) {
    size_t elem_size = type_elem_size(type);
    size_t size = (count > 0 ? (size_t)count : 1) * elem_size;
    Qo q = alloc_block(size);
    q->type_tag = type;
    QO_SET_COUNT(q, count);
    return q;
}

Qo alloc_charlike(uint8_t type, int64_t count) {
    size_t size = (size_t)(count + 1);
    Qo q = alloc_block(size);
    q->type_tag = type;
    QO_SET_COUNT(q, count);
    q->data[count] = '\0';
    return q;
}

Qo alloc_dict_block(int64_t count) {
    size_t sz = 2 + (size_t)(count > 0 ? count : 1) * 2 * sizeof(Qo);
    Qo q = alloc_block(sz);
    q->type_tag = QO_DICT;
    QO_SET_COUNT(q, count);
    return q;
}

Qo alloc_table_block(void) {
    Qo q = alloc_block(sizeof(Qo));
    q->type_tag = QO_TABLE;
    QO_SET_COUNT(q, 0);
    QO_TABLE_DICT(q) = NULL;
    return q;
}

Qo make_short_value(int16_t value) {
    return make_scalar_value(QO_SHORT, &value);
}

Qo make_int_value(int32_t value) {
    return make_scalar_value(QO_INT, &value);
}

Qo make_long_value(int64_t value) {
    return make_scalar_value(QO_LONG, &value);
}

Qo make_timestamp_value(int64_t nanos_since_epoch) {
    return make_scalar_value(QO_TIMESTAMP, &nanos_since_epoch);
}

Qo make_timespan_value(int64_t nanos) {
    return make_scalar_value(QO_TIMESPAN, &nanos);
}

Qo make_float_value(double value) {
    return make_scalar_value(QO_FLOAT, &value);
}

Qo make_char_value(char c) {
    return make_scalar_value(QO_CHAR, &c);
}

Qo make_bool_value(int v) {
    uint8_t value = (uint8_t)(v != 0);
    return make_scalar_value(QO_BOOL, &value);
}

Qo make_byte_value(uint8_t v) {
    return make_scalar_value(QO_BYTE, &v);
}

Qo make_projector_value(void) {
    return alloc_atom(QO_PROJECTOR);
}

Qo make_adverbed_value(Qo func, uint8_t kind) {
    Qo q = alloc_block(sizeof(Qo) + sizeof(Qo));
    q->type_tag = QO_ADVERBED;
    QO_ADVERBED_KIND(q) = kind;
    QO_ADVERBED_FUNC(q) = qo_retain(func);
    return q;
}

Qo make_adverb_value(uint8_t kind) {
    Qo q = alloc_atom(QO_ADVERB);
    q->byte_val = kind;
    return q;
}

Qo make_null_value(void) {
    return NULL;
}

Qo make_symbol_value(const char *text) {
    return qo_symbol_intern(text);
}

Qo make_operator_value(TokenType op) {
    Qo q = alloc_atom(QO_OPERATOR);
    q->byte_val = (uint8_t)op;
    return q;
}

Qo make_builtin_value(uint8_t id) {
    Qo q = alloc_atom(QO_BUILTIN);
    q->byte_val = id;
    return q;
}

Qo qo_make_list_take(Qo *elements, int64_t count) {
    Qo q = alloc_ptr_vec(QO_LIST, count);
    if (count > 0) {
        memcpy(QO_LIST_DATA(q), elements, (size_t)count * sizeof(Qo));
    }
    free(elements);
    return q;
}

Qo make_function_value(char **params, int param_count,
                       Qo *body, int body_count,
                       const char *source, int source_len) {
    size_t data_size = (size_t)(param_count + body_count) * sizeof(Qo);
    size_t size = 24 + data_size + (size_t)source_len + 1;
    Qo q = alloc_block(size);
    q->type_tag = QO_FUNCTION;
    QO_FN_PC(q) = (int64_t)param_count;
    QO_FN_BC(q) = (int64_t)body_count;
    QO_FN_SL(q) = (int64_t)source_len;
    for (int i = 0; i < param_count; i++) {
        QO_FN_PARAMS(q)[i] = make_symbol_value(params[i]);
    }
    for (int i = 0; i < body_count; i++) {
        QO_FN_BODY(q)[i] = qo_retain(body[i]);  /* take ownership of each body expression */
    }
    memcpy(QO_FN_SOURCE(q), source, (size_t)source_len);
    QO_FN_SOURCE(q)[source_len] = '\0';
    return q;
}

Qo qo_clone(Qo q) {
    if (q == NULL) return NULL;
    uint8_t t = QO_TYPE(q);

    if (t == QO_SYMBOL) return qo_retain(q);
    if (t == QO_ADVERBED) {
        Qo c = alloc_block(sizeof(Qo) + sizeof(Qo));
        c->type_tag = QO_ADVERBED;
        c->attribute = q->attribute;
        QO_ADVERBED_KIND(c) = QO_ADVERBED_KIND(q);
        QO_ADVERBED_FUNC(c) = qo_clone(QO_ADVERBED_FUNC(q));
        return c;
    }
    if (type_has_flag(t, TF_SCALAR)) {
        Qo c = alloc_atom(t);
        c->attribute = q->attribute;
        memcpy(&c->long_val, &q->long_val, sizeof(q->long_val));
        return c;
    }
    if (type_has_flag(t, TF_VECTOR)) {
        int64_t n = QO_COUNT(q);
        if (t == QO_CHAR_VEC) {
            Qo c = alloc_block((size_t)n + 1);
            c->type_tag = t;
            c->attribute = q->attribute;
            QO_SET_COUNT(c, n);
            memcpy(c->data, q->data, (size_t)n + 1);
            return c;
        }
        if (type_storage(t) == SC_PTR) {
            Qo c = alloc_ptr_vec(t, n);
            for (int64_t i = 0; i < n; i++) QO_LIST_DATA(c)[i] = qo_clone(QO_LIST_DATA(q)[i]);
            return c;
        }
        /* data vector: raw memcpy by element size */
        size_t elem_size = type_elem_size(t);
        size_t sz = (size_t)n * elem_size;
        Qo c = alloc_block(sz);
        c->type_tag = t;
        c->attribute = q->attribute;
        QO_SET_COUNT(c, n);
        memcpy(c->data, q->data, sz);
        return c;
    }
    /* complex types with custom layouts */
    switch (t) {
        case QO_DICT: {
            int64_t n = QO_DICT_COUNT(q);
            Qo c = alloc_dict_block(n);
            QO_DICT_KTYPE(c) = QO_DICT_KTYPE(q);
            QO_DICT_VTYPE(c) = QO_DICT_VTYPE(q);
            for (int64_t i = 0; i < n; i++) {
                QO_DICT_KEYS(c)[i] = qo_clone(QO_DICT_KEYS(q)[i]);
                QO_DICT_VALS(c)[i] = qo_clone(QO_DICT_VALS(q)[i]);
            }
            return c;
        }
        case QO_FUNCTION: {
            int64_t pc = QO_FN_PC(q);
            int64_t bc = QO_FN_BC(q);
            int64_t sl = QO_FN_SL(q);
            size_t sz = 24 + (size_t)(pc + bc) * sizeof(Qo) + (size_t)sl + 1;
            Qo c = alloc_block(sz);
            c->type_tag = QO_FUNCTION;
            QO_FN_PC(c) = pc;
            QO_FN_BC(c) = bc;
            QO_FN_SL(c) = sl;
            for (int64_t i = 0; i < pc; i++) QO_FN_PARAMS(c)[i] = qo_clone(QO_FN_PARAMS(q)[i]);
            for (int64_t i = 0; i < bc; i++) QO_FN_BODY(c)[i] = qo_clone(QO_FN_BODY(q)[i]);
            memcpy(QO_FN_SOURCE(c), QO_FN_SOURCE(q), (size_t)sl + 1);
            return c;
        }
        case QO_PROJECTION: {
            int64_t ac = QO_PROJ_AC(q);
            size_t sz = 16 + (size_t)ac * sizeof(Qo);
            Qo c = alloc_block(sz);
            c->type_tag = QO_PROJECTION;
            QO_PROJ_AC(c) = ac;
            QO_PROJ_FUNC(c) = qo_clone(QO_PROJ_FUNC(q));
            for (int64_t i = 0; i < ac; i++) QO_PROJ_ARGS(c)[i] = qo_clone(QO_PROJ_ARGS(q)[i]);
            return c;
        }
        case QO_TABLE: {
            Qo clone = alloc_table_block();
            QO_SET_COUNT(clone, QO_TABLE_ROWS(q));
            Qo d = QO_TABLE_DICT(q);
            if (d) QO_TABLE_DICT(clone) = qo_clone(d);
            return clone;
        }
        default:
            return NULL;
    }
}

static void qo_destroy(Qo q) {
    if (q == NULL) return;
    uint8_t t = QO_TYPE(q);

    if (t == QO_SYMBOL) {
        qo_symbol_intern_remove(q);
    } else if (t == QO_ADVERBED) {
        qo_release(QO_ADVERBED_FUNC(q));
    } else if (type_has_flag(t, TF_COMPLEX)) {
        switch (t) {
            case QO_DICT: {
                int64_t n = QO_DICT_COUNT(q);
                for (int64_t i = 0; i < n; i++) {
                    qo_release(QO_DICT_KEYS(q)[i]);
                    qo_release(QO_DICT_VALS(q)[i]);
                }
                break;
            }
            case QO_FUNCTION: {
                int64_t pc = QO_FN_PC(q);
                int64_t bc = QO_FN_BC(q);
                for (int64_t i = 0; i < pc; i++) qo_release(QO_FN_PARAMS(q)[i]);
                for (int64_t i = 0; i < bc; i++) qo_release(QO_FN_BODY(q)[i]);
                break;
            }
            case QO_PROJECTION: {
                int64_t ac = QO_PROJ_AC(q);
                qo_release(QO_PROJ_FUNC(q));
                for (int64_t i = 0; i < ac; i++) qo_release(QO_PROJ_ARGS(q)[i]);
                break;
            }
            case QO_TABLE: {
                Qo d = QO_TABLE_DICT(q);
                if (d) qo_release(d);
                break;
            }
            default:
                break;
        }
    } else if (is_vector_type(t) && type_storage(t) == SC_PTR) {
        int64_t n = QO_COUNT(q);
        for (int64_t i = 0; i < n; i++) qo_release(QO_LIST_DATA(q)[i]);
    }

    free(q);
}

int value_equals(Qo a, Qo b) {
    if (a == NULL && b == NULL) return 1;
    if (a == NULL || b == NULL) return 0;
    if (QO_TYPE(a) != QO_TYPE(b)) return 0;

    switch (QO_TYPE(a)) {
        case QO_SHORT:
            return QO_SHORT_VAL(a) == QO_SHORT_VAL(b);
        case QO_INT:
            return QO_INT_VAL(a) == QO_INT_VAL(b);
        case QO_LONG:
            return QO_LONG_VAL(a) == QO_LONG_VAL(b);
        case QO_TIMESTAMP:
            return qo_timestamp(a) == qo_timestamp(b);
        case QO_TIMESPAN:
            return qo_timespan(a) == qo_timespan(b);
        case QO_FLOAT:
            return QO_FLOAT_VAL(a) == QO_FLOAT_VAL(b);
        case QO_CHAR:
            return QO_CHAR_VAL(a) == QO_CHAR_VAL(b);
        case QO_BOOL:
            return QO_BOOL_VAL(a) == QO_BOOL_VAL(b);
        case QO_BYTE:
            return QO_BYTE_VAL(a) == QO_BYTE_VAL(b);
        case QO_PROJECTOR:
            return 1;
        case QO_ADVERBED:
            return QO_ADVERBED_KIND(a) == QO_ADVERBED_KIND(b) &&
                   value_equals(QO_ADVERBED_FUNC(a), QO_ADVERBED_FUNC(b));
        case QO_ADVERB:
            return QO_ADVERB_KIND(a) == QO_ADVERB_KIND(b);
        case QO_SYMBOL:
            return QO_SYMBOL_ID(a) == QO_SYMBOL_ID(b);
        case QO_OPERATOR:
            return QO_OPERATOR_OP(a) == QO_OPERATOR_OP(b);
        case QO_BUILTIN:
            return QO_BUILTIN_ID(a) == QO_BUILTIN_ID(b);
        case QO_CHAR_VEC: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            return memcmp(QO_CHAR_DATA(a), QO_CHAR_DATA(b), (size_t)n) == 0;
        }
        case QO_SHORT_VEC: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            return memcmp(QO_SHORT_DATA(a), QO_SHORT_DATA(b), (size_t)n * 2) == 0;
        }
        case QO_INT_VEC: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            return memcmp(QO_INT_DATA(a), QO_INT_DATA(b), (size_t)n * 4) == 0;
        }
        case QO_LONG_VEC:
        case QO_TIMESTAMP_VEC:
        case QO_TIMESPAN_VEC: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            return memcmp(QO_LONG_DATA(a), QO_LONG_DATA(b), (size_t)n * 8) == 0;
        }
        case QO_FLOAT_VEC: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            return memcmp(QO_FLOAT_DATA(a), QO_FLOAT_DATA(b), (size_t)n * 8) == 0;
        }
        case QO_BOOL_VEC: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            return memcmp(QO_BOOL_DATA(a), QO_BOOL_DATA(b), (size_t)n) == 0;
        }
        case QO_BYTE_VEC: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            return memcmp(QO_BYTE_DATA(a), QO_BYTE_DATA(b), (size_t)n) == 0;
        }
        case QO_SYM_VEC:
        case QO_LIST: {
            int64_t n = QO_COUNT(a);
            if (n != QO_COUNT(b)) return 0;
            for (int64_t i = 0; i < n; i++) {
                if (!value_equals(QO_LIST_DATA(a)[i], QO_LIST_DATA(b)[i])) return 0;
            }
            return 1;
        }
        case QO_DICT: {
            int64_t n = QO_DICT_COUNT(a);
            if (n != QO_DICT_COUNT(b)) return 0;
            for (int64_t i = 0; i < n; i++) {
                if (!value_equals(QO_DICT_KEYS(a)[i], QO_DICT_KEYS(b)[i])) return 0;
                if (!value_equals(QO_DICT_VALS(a)[i], QO_DICT_VALS(b)[i])) return 0;
            }
            return 1;
        }
        case QO_FUNCTION: {
            if (QO_FN_PC(a) != QO_FN_PC(b) || QO_FN_BC(a) != QO_FN_BC(b)) return 0;
            for (int64_t i = 0; i < QO_FN_PC(a); i++) {
                if (!value_equals(QO_FN_PARAMS(a)[i], QO_FN_PARAMS(b)[i])) return 0;
            }
            for (int64_t i = 0; i < QO_FN_BC(a); i++) {
                if (!value_equals(QO_FN_BODY(a)[i], QO_FN_BODY(b)[i])) return 0;
            }
            return 1;
        }
        case QO_PROJECTION: {
            if (QO_PROJ_AC(a) != QO_PROJ_AC(b)) return 0;
            if (!value_equals(QO_PROJ_FUNC(a), QO_PROJ_FUNC(b))) return 0;
            for (int64_t i = 0; i < QO_PROJ_AC(a); i++) {
                if (!value_equals(QO_PROJ_ARGS(a)[i], QO_PROJ_ARGS(b)[i])) return 0;
            }
            return 1;
        }
        case QO_TABLE: {
            if (QO_TABLE_ROWS(a) != QO_TABLE_ROWS(b)) return 0;
            Qo da = QO_TABLE_DICT(a);
            Qo db = QO_TABLE_DICT(b);
            if (!da && !db) return 1;
            if (!da || !db) return 0;
            return value_equals(da, db);
        }
        default:
            return 0;
    }
}

int64_t dict_collection_count(Qo v) {
    if (v == NULL) return 1;
    return is_vector_type(QO_TYPE(v)) ? QO_COUNT(v) : 1;
}

void set_vec_elem_from_scalar(Qo vec, int64_t i, Qo scalar) {
    switch (type_storage(QO_TYPE(vec))) {
        case SC_I64: qo_long_data(vec)[i] = qo_long(scalar); break;
        case SC_I32: qo_int_data(vec)[i]  = qo_int(scalar);  break;
        case SC_I16: qo_short_data(vec)[i] = qo_short(scalar); break;
        case SC_F64: qo_float_data(vec)[i] = qo_float(scalar); break;
        case SC_U8:  qo_bool_data(vec)[i]  = qo_bool(scalar);  break;
        default: break;
    }
}

Qo dict_elem_copy(Qo v, int64_t i) {
    if (v == NULL) return NULL;
    uint8_t t = QO_TYPE(v);
    if (!type_has_flag(t, TF_VECTOR)) return qo_clone(v);
    if (type_storage(t) == SC_PTR) return qo_clone(QO_LIST_DATA(v)[i]);
    switch (type_storage(t)) {
        case SC_I64:
            if (t == QO_TIMESTAMP_VEC) return make_timestamp_value(QO_LONG_DATA(v)[i]);
            if (t == QO_TIMESPAN_VEC) return make_timespan_value(QO_LONG_DATA(v)[i]);
            return make_long_value(QO_LONG_DATA(v)[i]);
        case SC_I32: return make_int_value(QO_INT_DATA(v)[i]);
        case SC_I16: return make_short_value(QO_SHORT_DATA(v)[i]);
        case SC_F64: return make_float_value(QO_FLOAT_DATA(v)[i]);
        case SC_U8:  if (t == QO_CHAR_VEC) return make_char_value(QO_CHAR_DATA(v)[i]);
                     if (t == QO_BOOL_VEC) return make_bool_value((int)QO_BOOL_DATA(v)[i]);
                     return make_byte_value(QO_BYTE_DATA(v)[i]);
        default:     return qo_clone(v);
    }
}

Qo extract_dict_side(Qo dict, int want_keys) {
    int64_t n = QO_DICT_COUNT(dict);
    uint8_t side_type = want_keys ? QO_DICT_KTYPE(dict) : QO_DICT_VTYPE(dict);
    Qo *elems = want_keys ? QO_DICT_KEYS(dict) : QO_DICT_VALS(dict);

    if (side_type == QO_CHAR_VEC) {
        Qo result = alloc_charlike(QO_CHAR_VEC, n);
        for (int64_t i = 0; i < n; i++) QO_CHAR_DATA(result)[i] = QO_CHAR_VAL(elems[i]);
        return result;
    }

    if (type_storage(side_type) != SC_PTR) {
        if (type_has_flag(side_type, TF_SCALAR))
            side_type = type_base_type(side_type);
        Qo result = alloc_data_vec(side_type, n);
        for (int64_t i = 0; i < n; i++) set_vec_elem_from_scalar(result, i, elems[i]);
        return result;
    }

    if (type_has_flag(side_type, TF_SCALAR))
        side_type = type_base_type(side_type);
    Qo result = alloc_ptr_vec(side_type, n);
    for (int64_t i = 0; i < n; i++) QO_LIST_DATA(result)[i] = qo_clone(elems[i]);
    return result;
}


