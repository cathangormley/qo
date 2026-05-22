#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "evaluator_internal.h"
#include "internal.h"
#include "symbol_intern.h"

static void qo_destroy(Qo q);

static Qo alloc_block(size_t payload_size) {
    Qo q = (Qo)xmalloc(sizeof(QoObject) + payload_size);
    q->attribute = 0;
    q->type_tag = 0;
    q->reference_count = 1;
    q->count = 0;
    return q;
}

static Qo alloc_scalar(uint8_t type, size_t scalar_size) {
    Qo q = alloc_block(scalar_size);
    q->type_tag = type;
    return q;
}

static size_t scalar_payload_size(uint8_t type) {
    if (type == QO_SHORT) return sizeof(int16_t);
    if (type == QO_INT) return sizeof(int32_t);
    if (type == QO_LONG || type == QO_SYMBOL || type == QO_PROJECTOR || type == QO_EACHED) return sizeof(int64_t);
    if (type == QO_FLOAT) return sizeof(double);
    if (type == QO_CHAR) return sizeof(char);
    if (type == QO_BOOL || type == QO_BYTE || type == QO_ADVERB || type == QO_OPERATOR || type == QO_BUILTIN) return sizeof(uint8_t);
    return 0;
}

static Qo make_scalar_value(uint8_t type, const void *value) {
    size_t sz = scalar_payload_size(type);
    Qo q = alloc_scalar(type, sz);
    memcpy(q->storage, value, sz);
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

int is_vector_type(uint8_t t) {
    return t == QO_SHORT_VEC || t == QO_INT_VEC || t == QO_LONG_VEC || t == QO_FLOAT_VEC ||
           t == QO_SYM_VEC || t == QO_CHAR_VEC || t == QO_BOOL_VEC || t == QO_BYTE_VEC || t == QO_LIST;
}

int is_numeric_scalar_type(uint8_t t) {
    return t == QO_SHORT || t == QO_INT || t == QO_LONG || t == QO_FLOAT || t == QO_BOOL;
}

int is_numeric_vector_type(uint8_t t) {
    return t == QO_SHORT_VEC || t == QO_INT_VEC || t == QO_LONG_VEC || t == QO_FLOAT_VEC || t == QO_BOOL_VEC;
}

int is_symbolish_type(uint8_t t) {
    return t == QO_SYMBOL || t == QO_SYM_VEC;
}

int is_charish_type(uint8_t t) {
    return t == QO_CHAR || t == QO_CHAR_VEC;
}

int is_non_numeric_operand(Qo q) {
    if (q == NULL) return 1;
    uint8_t t = qo_type(q);
    return t == QO_SYMBOL || t == QO_SYM_VEC || t == QO_DICT || t == QO_OPERATOR || t == QO_BUILTIN ||
           t == QO_CHAR_VEC || t == QO_CHAR || t == QO_BYTE || t == QO_BYTE_VEC || t == QO_FUNCTION ||
           t == QO_PROJECTOR || t == QO_PROJECTION;
}

double value_as_double(Qo q) {
    uint8_t t = qo_type(q);
    if (t == QO_SHORT) return (double)qo_short(q);
    if (t == QO_INT) return (double)qo_int(q);
    if (t == QO_LONG) return (double)qo_long(q);
    if (t == QO_FLOAT) return qo_float(q);
    if (t == QO_BOOL) return (double)qo_bool(q);
    return 0.0;
}

Qo alloc_ptr_vec(uint8_t type, int64_t count) {
    size_t size = (count > 0 ? (size_t)count : 1) * sizeof(Qo);
    Qo q = alloc_block(size);
    q->type_tag = type;
    QO_SET_COUNT(q, count);
    return q;
}

Qo alloc_data_vec(uint8_t type, int64_t count) {
    size_t elem_size;
    if (type == QO_SHORT_VEC) elem_size = 2;
    else if (type == QO_INT_VEC) elem_size = 4;
    else if (type == QO_BOOL_VEC || type == QO_BYTE_VEC) elem_size = 1;
    else elem_size = 8;
    {
        size_t size = (count > 0 ? (size_t)count : 1) * elem_size;
        Qo q = alloc_block(size);
        q->type_tag = type;
        QO_SET_COUNT(q, count);
        return q;
    }
}

Qo alloc_charlike(uint8_t type, int64_t count) {
    size_t size = (size_t)(count + 1);
    Qo q = alloc_block(size);
    q->type_tag = type;
    QO_SET_COUNT(q, count);
    q->storage[count] = '\0';
    return q;
}

Qo alloc_dict_block(int64_t count) {
    size_t sz = 2 + (size_t)(count > 0 ? count : 1) * 2 * sizeof(Qo);
    Qo q = alloc_block(sz);
    q->type_tag = QO_DICT;
    QO_SET_COUNT(q, count);
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
    Qo q = alloc_block(0);
    q->type_tag = QO_PROJECTOR;
    QO_SET_COUNT(q, 0);
    return q;
}

Qo make_eached_value(Qo func) {
    Qo q = alloc_block(sizeof(Qo));
    q->type_tag = QO_EACHED;
    QO_EACHED_FUNC(q) = qo_retain(func);
    return q;
}

Qo make_adverb_value(uint8_t kind) {
    Qo q = alloc_scalar(QO_ADVERB, sizeof(uint8_t));
    QO_ADVERB_KIND(q) = kind;
    return q;
}

Qo make_null_value(void) {
    return NULL;
}

static Qo make_strlike(uint8_t type, const char *text, int64_t len) {
    Qo q = alloc_charlike(type, len);
    memcpy(QO_STR(q), text, (size_t)len);
    return q;
}

Qo make_symbol_value(const char *text) {
    return qo_symbol_intern(text);
}

Qo make_operator_value(TokenType op) {
    Qo q = alloc_scalar(QO_OPERATOR, sizeof(uint8_t));
    QO_OPERATOR_OP(q) = (uint8_t)op;
    return q;
}

Qo make_builtin_value(uint8_t id) {
    Qo q = alloc_scalar(QO_BUILTIN, sizeof(uint8_t));
    QO_BUILTIN_ID(q) = id;
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
        QO_FN_BODY(q)[i] = body[i];
    }
    memcpy(QO_FN_SOURCE(q), source, (size_t)source_len);
    QO_FN_SOURCE(q)[source_len] = '\0';
    return q;
}

Qo qo_clone(Qo q) {
    int64_t n;
    if (q == NULL) return NULL;

    switch (QO_TYPE(q)) {
        case QO_SHORT:
        case QO_INT:
        case QO_LONG:
        case QO_FLOAT:
        case QO_CHAR:
        case QO_BOOL:
        case QO_BYTE: {
            size_t sz = scalar_payload_size(QO_TYPE(q));
            Qo c = alloc_scalar(QO_TYPE(q), sz);
            c->attribute = q->attribute;
            memcpy(c->storage, q->storage, sz);
            return c;
        }
        case QO_PROJECTOR:
        case QO_ADVERB:
        case QO_OPERATOR:
        case QO_BUILTIN: {
            size_t sz = scalar_payload_size(QO_TYPE(q));
            Qo c = alloc_scalar(QO_TYPE(q), sz);
            c->attribute = q->attribute;
            memcpy(c->storage, q->storage, sz);
            return c;
        }
        case QO_EACHED: {
            Qo c = alloc_block(sizeof(Qo));
            c->type_tag = QO_EACHED;
            c->attribute = q->attribute;
            QO_EACHED_FUNC(c) = qo_clone(QO_EACHED_FUNC(q));
            return c;
        }
        case QO_SYMBOL:
            return qo_retain(q);
        case QO_CHAR_VEC: {
            n = QO_COUNT(q);
            {
                Qo c = alloc_block((size_t)n + 1);
                c->type_tag = q->type_tag;
                c->attribute = q->attribute;
                QO_SET_COUNT(c, QO_COUNT(q));
                    memcpy(c->storage, q->storage, (size_t)n + 1);
                return c;
            }
        }
        case QO_SHORT_VEC:
        case QO_INT_VEC:
        case QO_LONG_VEC:
        case QO_FLOAT_VEC:
        case QO_BOOL_VEC:
        case QO_BYTE_VEC: {
            n = QO_COUNT(q);
            {
                size_t elem_size;
                if (QO_TYPE(q) == QO_SHORT_VEC) elem_size = 2;
                else if (QO_TYPE(q) == QO_INT_VEC) elem_size = 4;
                else if (QO_TYPE(q) == QO_BOOL_VEC || QO_TYPE(q) == QO_BYTE_VEC) elem_size = 1;
                else elem_size = 8;
                {
                    size_t sz = (size_t)n * elem_size;
                    Qo c = alloc_block(sz);
                    c->type_tag = q->type_tag;
                    c->attribute = q->attribute;
                    QO_SET_COUNT(c, QO_COUNT(q));
                        memcpy(c->storage, q->storage, sz);
                    return c;
                }
            }
        }
        case QO_SYM_VEC:
        case QO_LIST: {
            n = QO_COUNT(q);
            {
                Qo c = alloc_ptr_vec(QO_TYPE(q), n);
                for (int64_t i = 0; i < n; i++) {
                    QO_LIST_DATA(c)[i] = qo_clone(QO_LIST_DATA(q)[i]);
                }
                return c;
            }
        }
        case QO_DICT: {
            n = QO_DICT_COUNT(q);
            {
                Qo c = alloc_dict_block(n);
                QO_DICT_KTYPE(c) = QO_DICT_KTYPE(q);
                QO_DICT_VTYPE(c) = QO_DICT_VTYPE(q);
                for (int64_t i = 0; i < n; i++) {
                    QO_DICT_KEYS(c)[i] = qo_clone(QO_DICT_KEYS(q)[i]);
                    QO_DICT_VALS(c)[i] = qo_clone(QO_DICT_VALS(q)[i]);
                }
                return c;
            }
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
        default:
            return NULL;
    }
}

Qo value_copy(Qo q) {
    return qo_clone(q);
}

static void qo_destroy(Qo q) {
    if (q == NULL) return;

    switch (QO_TYPE(q)) {
        case QO_SHORT:
        case QO_INT:
        case QO_LONG:
        case QO_FLOAT:
        case QO_CHAR:
        case QO_BOOL:
        case QO_BYTE:
        case QO_PROJECTOR:
        case QO_ADVERB:
        case QO_OPERATOR:
        case QO_BUILTIN:
            break;
        case QO_EACHED:
            qo_release(QO_EACHED_FUNC(q));
            break;
        case QO_SYMBOL:
            qo_symbol_intern_remove(q);
            break;
        case QO_CHAR_VEC:
        case QO_SHORT_VEC:
        case QO_INT_VEC:
        case QO_LONG_VEC:
        case QO_FLOAT_VEC:
        case QO_BOOL_VEC:
        case QO_BYTE_VEC:
            break;
        case QO_SYM_VEC:
        case QO_LIST: {
            int64_t n = QO_COUNT(q);
            for (int64_t i = 0; i < n; i++) qo_release(QO_LIST_DATA(q)[i]);
            break;
        }
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
        default:
            break;
    }

    free(q);
}

void value_free(Qo q) {
    qo_release(q);
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
        case QO_LONG_VEC: {
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
        default:
            return 0;
    }
}

int64_t dict_collection_count(Qo v) {
    if (v == NULL) return 1;
    return is_vector_type(QO_TYPE(v)) ? QO_COUNT(v) : 1;
}

Qo dict_elem_copy(Qo v, int64_t i) {
    uint8_t t;
    if (v == NULL) return NULL;
    t = QO_TYPE(v);
    if (t == QO_LIST || t == QO_SYM_VEC) return value_copy(QO_LIST_DATA(v)[i]);
    if (t == QO_SHORT_VEC) return make_short_value(QO_SHORT_DATA(v)[i]);
    if (t == QO_INT_VEC) return make_int_value(QO_INT_DATA(v)[i]);
    if (t == QO_LONG_VEC) return make_long_value(QO_LONG_DATA(v)[i]);
    if (t == QO_FLOAT_VEC) return make_float_value(QO_FLOAT_DATA(v)[i]);
    if (t == QO_CHAR_VEC) return make_char_value(QO_CHAR_DATA(v)[i]);
    if (t == QO_BOOL_VEC) return make_bool_value((int)QO_BOOL_DATA(v)[i]);
    if (t == QO_BYTE_VEC) return make_byte_value(QO_BYTE_DATA(v)[i]);
    return value_copy(v);
}

Qo extract_dict_side(Qo dict, int want_keys) {
    int64_t n = QO_DICT_COUNT(dict);
    uint8_t side_type = want_keys ? QO_DICT_KTYPE(dict) : QO_DICT_VTYPE(dict);
    Qo *elems = want_keys ? QO_DICT_KEYS(dict) : QO_DICT_VALS(dict);

    if (side_type == QO_CHAR_VEC) {
        Qo result = alloc_charlike(QO_CHAR_VEC, n);
        for (int64_t i = 0; i < n; i++) {
            QO_CHAR_DATA(result)[i] = QO_CHAR_VAL(elems[i]);
        }
        return result;
    }

    if (side_type == QO_SHORT_VEC || side_type == QO_INT_VEC || side_type == QO_LONG_VEC ||
        side_type == QO_FLOAT_VEC || side_type == QO_BOOL_VEC || side_type == QO_BYTE_VEC) {
        Qo result = alloc_data_vec(side_type, n);
        for (int64_t i = 0; i < n; i++) {
            if (side_type == QO_SHORT_VEC) QO_SHORT_DATA(result)[i] = QO_SHORT_VAL(elems[i]);
            else if (side_type == QO_INT_VEC) QO_INT_DATA(result)[i] = QO_INT_VAL(elems[i]);
            else if (side_type == QO_LONG_VEC) QO_LONG_DATA(result)[i] = QO_LONG_VAL(elems[i]);
            else if (side_type == QO_FLOAT_VEC) QO_FLOAT_DATA(result)[i] = QO_FLOAT_VAL(elems[i]);
            else if (side_type == QO_BOOL_VEC) QO_BOOL_DATA(result)[i] = QO_BOOL_VAL(elems[i]);
            else QO_BYTE_DATA(result)[i] = QO_BYTE_VAL(elems[i]);
        }
        return result;
    }

    {
        Qo result = alloc_ptr_vec(side_type, n);
        for (int64_t i = 0; i < n; i++) QO_LIST_DATA(result)[i] = value_copy(elems[i]);
        return result;
    }
}

Qo build_collection_from_copies(Qo *elements, int count, uint8_t type) {
    Qo result = alloc_ptr_vec(type, (int64_t)count);
    if (count > 0) {
        memcpy(QO_LIST_DATA(result), elements, (size_t)count * sizeof(Qo));
    }
    return result;
}
