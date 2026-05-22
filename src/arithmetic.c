#include <stdlib.h>
#include <math.h>
#include <stdio.h>
#include <string.h>
#include "evaluator_internal.h"
#include "internal.h"

static int is_numeric_int_type(uint8_t t) {
    return t == QO_SHORT || t == QO_INT || t == QO_LONG;
}

static int64_t get_integer_value(Qo q) {
    uint8_t t = qo_type(q);
    if (t == QO_SHORT) return (int64_t)qo_short(q);
    if (t == QO_INT) return (int64_t)qo_int(q);
    if (t == QO_LONG) return qo_long(q);
    return 0;
}

static int64_t int_elem_value(Qo q, int64_t i) {
    uint8_t t = qo_type(q);
    if (t == QO_SHORT) return (int64_t)qo_short(q);
    if (t == QO_INT) return (int64_t)qo_int(q);
    if (t == QO_LONG) return qo_long(q);
    if (t == QO_SHORT_VEC) return (int64_t)qo_short_data(q)[i];
    if (t == QO_INT_VEC) return (int64_t)qo_int_data(q)[i];
    if (t == QO_LONG_VEC) return qo_long_data(q)[i];
    return 0;
}

static int int_rank_from_type(uint8_t t) {
    if (t == QO_SHORT || t == QO_SHORT_VEC) return 0;
    if (t == QO_INT || t == QO_INT_VEC) return 1;
    if (t == QO_LONG || t == QO_LONG_VEC) return 2;
    return -1;
}

static uint8_t scalar_type_from_int_rank(int rank) {
    if (rank <= 0) return QO_SHORT;
    if (rank == 1) return QO_INT;
    return QO_LONG;
}

static uint8_t vector_type_from_int_rank(int rank) {
    if (rank <= 0) return QO_SHORT_VEC;
    if (rank == 1) return QO_INT_VEC;
    return QO_LONG_VEC;
}

static void store_int_vec_elem(Qo v, int64_t i, int64_t value) {
    uint8_t t = qo_type(v);
    if (t == QO_SHORT_VEC) qo_short_data(v)[i] = (int16_t)value;
    else if (t == QO_INT_VEC) qo_int_data(v)[i] = (int32_t)value;
    else qo_long_data(v)[i] = value;
}

static Qo make_int_scalar_of_type(uint8_t t, int64_t value) {
    if (t == QO_SHORT) return make_short_value((int16_t)value);
    if (t == QO_INT) return make_int_value((int32_t)value);
    return make_long_value(value);
}

static Qo make_numeric_result(double value, int use_float) {
    return use_float ? make_float_value(value) : make_long_value((int64_t)value);
}

static int apply_numeric_op(double left, double right, TokenType op,
                            double *out, int *is_div_or_mod) {
    *is_div_or_mod = 0;
    switch (op) {
        case TOKEN_PLUS:
            *out = left + right;
            return 1;
        case TOKEN_MINUS:
            *out = left - right;
            return 1;
        case TOKEN_STAR:
            *out = left * right;
            return 1;
        case TOKEN_DIVIDE:
            *out = left / right;
            *is_div_or_mod = 1;
            return 1;
        case TOKEN_STAR_STAR:
            *out = pow(left, right);
            *is_div_or_mod = 1;
            return 1;
        case TOKEN_SLASH:
            fprintf(stderr, "Error: operator '/' is undefined\n");
            evaluator_request_error();
            return 0;
        default:
            fprintf(stderr, "Error: unknown operator\n");
            evaluator_request_error();
            return 0;
    }
}

static double vec_elem_double(Qo v, int64_t i) {
    if (v == NULL) return 0.0;
    uint8_t t = qo_type(v);
    if (t == QO_SHORT_VEC) return (double)qo_short_data(v)[i];
    if (t == QO_INT_VEC) return (double)qo_int_data(v)[i];
    if (t == QO_LONG_VEC) return (double)qo_long_data(v)[i];
    if (t == QO_FLOAT_VEC) return qo_float_data(v)[i];
    if (t == QO_BOOL_VEC) return (double)qo_bool_data(v)[i];
    if (t == QO_CHAR_VEC) return (double)(unsigned char)qo_char_data(v)[i];
    if (t == QO_LIST) return value_as_double(qo_ptr_data(v)[i]);
    return value_as_double(v);
}

static void copy_vec_elem_to(Qo dest, int64_t di, Qo src, int64_t si) {
    uint8_t t = qo_type(src);
    if (t == QO_SHORT_VEC) qo_short_data(dest)[di] = qo_short_data(src)[si];
    else if (t == QO_INT_VEC) qo_int_data(dest)[di] = qo_int_data(src)[si];
    else if (t == QO_LONG_VEC) qo_long_data(dest)[di] = qo_long_data(src)[si];
    else if (t == QO_FLOAT_VEC) qo_float_data(dest)[di] = qo_float_data(src)[si];
    else if (t == QO_BOOL_VEC) qo_bool_data(dest)[di] = qo_bool_data(src)[si];
    else if (t == QO_BYTE_VEC) qo_byte_data(dest)[di] = qo_byte_data(src)[si];
    else if (t == QO_CHAR_VEC) qo_char_data(dest)[di] = qo_char_data(src)[si];
    else qo_ptr_data(dest)[di] = qo_clone(qo_ptr_data(src)[si]);
}

static Qo alloc_same_type(uint8_t t, int64_t count) {
    if (t == QO_SHORT_VEC || t == QO_INT_VEC || t == QO_LONG_VEC || 
        t == QO_FLOAT_VEC || t == QO_BOOL_VEC || t == QO_BYTE_VEC) {
        return alloc_data_vec(t, count);
    }
    if (t == QO_CHAR_VEC) {
        return alloc_charlike(t, count);
    }
    return alloc_ptr_vec(t, count);
}

Qo eval_take(Qo lhs, Qo rhs) {
    long take_count;
    long output_count;
    if (lhs == NULL || !is_numeric_int_type(qo_type(lhs))) EVAL_ERROR("take count must be an integer");
    take_count = get_integer_value(lhs);
    output_count = (take_count < 0) ? -take_count : take_count;
    if (rhs == NULL) EVAL_ERROR("take right argument must be a list, vector, or atom");
    if (qo_type(rhs) == QO_DICT || qo_type(rhs) == QO_OPERATOR || qo_type(rhs) == QO_BUILTIN) {
        EVAL_ERROR("take right argument must be a list, vector, or atom");
    }
 
    if (is_vector_type(qo_type(rhs))) {
        int64_t source_count = qo_count(rhs);
        Qo result;
        if (source_count == 0 && output_count > 0) EVAL_ERROR("cannot take from empty vector");
        result = alloc_same_type(qo_type(rhs), (int64_t)output_count);
        for (long i = 0; i < output_count; i++) {
            int64_t src_index;
            if (take_count >= 0) {
                src_index = (source_count == 0) ? 0 : (int64_t)i % source_count;
            } else {
                int64_t start = source_count - ((int64_t)output_count % source_count);
                src_index = (start + (int64_t)i) % source_count;
            }
            copy_vec_elem_to(result, i, rhs, src_index);
        }
        if (qo_type(rhs) == QO_CHAR_VEC) qo_char_data(result)[output_count] = '\0';
        return result;
    }

    {
        uint8_t out_type;
        Qo result;
        if (is_numeric_int_type(qo_type(rhs))) out_type = QO_LONG_VEC;
        else if (qo_type(rhs) == QO_FLOAT) out_type = QO_FLOAT_VEC;
        else if (qo_type(rhs) == QO_CHAR) out_type = QO_CHAR_VEC;
        else if (qo_type(rhs) == QO_SYMBOL) out_type = QO_SYM_VEC;
        else EVAL_ERROR("take right argument must be a list, vector, or atom");

        result = alloc_same_type(out_type, (int64_t)output_count);
        for (long i = 0; i < output_count; i++) {
            if (out_type == QO_LONG_VEC) qo_long_data(result)[i] = get_integer_value(rhs);
            else if (out_type == QO_FLOAT_VEC) qo_float_data(result)[i] = qo_float(rhs);
            else if (out_type == QO_CHAR_VEC) qo_char_data(result)[i] = qo_char(rhs);
            else qo_ptr_data(result)[i] = qo_clone(rhs);
        }
        if (out_type == QO_CHAR_VEC) qo_char_data(result)[output_count] = '\0';
        return result;
    }
}

Qo eval_drop(Qo lhs, Qo rhs) {
    long drop_count;
    if (lhs == NULL || !is_numeric_int_type(qo_type(lhs))) EVAL_ERROR("drop count must be an integer");
    drop_count = get_integer_value(lhs);
    if (rhs == NULL) EVAL_ERROR("drop right argument must be a list, vector, or atom");
    if (qo_type(rhs) == QO_DICT || qo_type(rhs) == QO_OPERATOR || qo_type(rhs) == QO_BUILTIN) {
        EVAL_ERROR("drop right argument must be a list, vector, or atom");
    }

    if (is_vector_type(qo_type(rhs))) {
        int64_t source_count = qo_count(rhs);
        int64_t start_idx;
        int64_t result_count;
        Qo result;

        if (drop_count >= source_count || drop_count <= -source_count) {
            result = alloc_same_type(qo_type(rhs), 0);
            if (qo_type(rhs) == QO_CHAR_VEC) qo_char_data(result)[0] = '\0';
            return result;
        }

        if (drop_count >= 0) {
            start_idx = (int64_t)drop_count;
            result_count = source_count - (int64_t)drop_count;
        } else {
            start_idx = 0;
            result_count = source_count + (int64_t)drop_count;
        }

        result = alloc_same_type(qo_type(rhs), result_count);
        for (int64_t i = 0; i < result_count; i++) {
            copy_vec_elem_to(result, i, rhs, start_idx + i);
        }
        if (qo_type(rhs) == QO_CHAR_VEC) qo_char_data(result)[result_count] = '\0';
        return result;
    }

    if (drop_count == 0) return qo_clone(rhs);

    {
        uint8_t out_type;
        Qo result;
        if (is_numeric_int_type(qo_type(rhs))) out_type = QO_LONG_VEC;
        else if (qo_type(rhs) == QO_FLOAT) out_type = QO_FLOAT_VEC;
        else if (qo_type(rhs) == QO_CHAR) out_type = QO_CHAR_VEC;
        else if (qo_type(rhs) == QO_SYMBOL) out_type = QO_SYM_VEC;
        else EVAL_ERROR("drop right argument type error");

        result = alloc_same_type(out_type, 0);
        if (out_type == QO_CHAR_VEC) qo_char_data(result)[0] = '\0';
        return result;
    }
}

Qo eval_equals(Qo left, Qo right) {
    int lv = (left != NULL) && is_vector_type(qo_type(left));
    int rv = (right != NULL) && is_vector_type(qo_type(right));
    int64_t count;
    Qo result;

    if (!lv && !rv) return make_bool_value(value_equals(left, right));
    if (lv && rv && qo_count(left) != qo_count(right)) {
        EVAL_ERROR("cannot compare vectors of different lengths");
    }

    count = lv ? qo_count(left) : qo_count(right);
    result = alloc_data_vec(QO_BOOL_VEC, count);
    for (int64_t i = 0; i < count; i++) {
        Qo l = lv ? dict_elem_copy(left, i) : qo_clone(left);
        Qo r = rv ? dict_elem_copy(right, i) : qo_clone(right);
        qo_bool_data(result)[i] = value_equals(l, r) ? 1 : 0;
        qo_release(l);
        qo_release(r);
    }
    return result;
}

static int scalar_order_value(Qo v, double *out) {
    if (v == NULL) return 0;
    uint8_t t = qo_type(v);
    if (t == QO_SHORT) {
        *out = (double)qo_short(v);
        return 1;
    }
    if (t == QO_INT) {
        *out = (double)qo_int(v);
        return 1;
    }
    if (t == QO_LONG) {
        *out = (double)qo_long(v);
        return 1;
    }
    if (t == QO_FLOAT) {
        *out = qo_float(v);
        return 1;
    }
    if (t == QO_BOOL) {
        *out = (double)qo_bool(v);
        return 1;
    }
    if (t == QO_CHAR) {
        *out = (double)(unsigned char)qo_char(v);
        return 1;
    }
    return 0;
}

static int is_order_scalar_type(uint8_t t);
static int is_order_vector_type(uint8_t t);

static Qo eval_order_compare(Qo left, Qo right, int want_less) {
    uint8_t lt = qo_type(left);
    uint8_t rt = qo_type(right);
    int lv = (left != NULL) && is_vector_type(lt);
    int rv = (right != NULL) && is_vector_type(rt);

    if ((lv && !is_order_vector_type(lt)) || (!lv && !is_order_scalar_type(lt)) ||
        (rv && !is_order_vector_type(rt)) || (!rv && !is_order_scalar_type(rt))) {
        EVAL_ERROR("comparison requires int/float/bool/char operands");
    }

    if (!lv && !rv) {
        return make_bool_value(want_less ? (value_as_double(left) < value_as_double(right))
                                        : (value_as_double(left) > value_as_double(right)));
    }

    if (lv && rv && qo_count(left) != qo_count(right)) {
        EVAL_ERROR("cannot compare vectors of different lengths");
    }

    {
        int64_t count = lv ? qo_count(left) : qo_count(right);
        Qo result = alloc_data_vec(QO_BOOL_VEC, count);
        for (int64_t i = 0; i < count; i++) {
            double l = lv ? vec_elem_double(left, i) : value_as_double(left);
            double r = rv ? vec_elem_double(right, i) : value_as_double(right);
            qo_bool_data(result)[i] = want_less ? (l < r) : (l > r);
        }
        return result;
    }
}

Qo eval_less(Qo left, Qo right) {
    return eval_order_compare(left, right, 1);
}

Qo eval_greater(Qo left, Qo right) {
    return eval_order_compare(left, right, 0);
}

static Qo negate_bool_result(Qo result) {
    if (result == NULL) return result;
    uint8_t t = qo_type(result);
    if (t == QO_BOOL) {
        return make_bool_value(!qo_bool(result));
    }
    if (t == QO_BOOL_VEC) {
        Qo negated = alloc_data_vec(QO_BOOL_VEC, qo_count(result));
        for (int64_t i = 0; i < qo_count(result); i++) {
            qo_bool_data(negated)[i] = !qo_bool_data(result)[i];
        }
        qo_release(result);
        return negated;
    }
    return result;
}

Qo eval_lte(Qo left, Qo right) {
    return negate_bool_result(eval_greater(left, right));
}

Qo eval_gte(Qo left, Qo right) {
    return negate_bool_result(eval_less(left, right));
}

static int is_order_scalar_type(uint8_t t) {
    /* Scalar types accepted by | and &: they compare by numeric ordering. */
    return t == QO_SHORT || t == QO_INT || t == QO_LONG ||
           t == QO_FLOAT || t == QO_BOOL || t == QO_CHAR;
}

static int is_order_vector_type(uint8_t t) {
    /* Vector counterparts accepted by | and &. */
    return t == QO_SHORT_VEC || t == QO_INT_VEC || t == QO_LONG_VEC ||
           t == QO_FLOAT_VEC || t == QO_BOOL_VEC || t == QO_CHAR_VEC;
}

static uint8_t extrema_vector_type(uint8_t lt, uint8_t rt) {
    int left_float = (lt == QO_FLOAT || lt == QO_FLOAT_VEC);
    int right_float = (rt == QO_FLOAT || rt == QO_FLOAT_VEC);
    int left_bool = (lt == QO_BOOL || lt == QO_BOOL_VEC);
    int right_bool = (rt == QO_BOOL || rt == QO_BOOL_VEC);
    int left_char = (lt == QO_CHAR || lt == QO_CHAR_VEC);
    int right_char = (rt == QO_CHAR || rt == QO_CHAR_VEC);

    /* Result type policy: float wins, bool/bool stays bool, char/char stays char, else long. */
    if (left_float || right_float) return QO_FLOAT_VEC;
    if (left_bool && right_bool) return QO_BOOL_VEC;
    if (left_char && right_char) return QO_CHAR_VEC;
    return QO_LONG_VEC;
}

static void store_extrema_elem(Qo result, int64_t i, double value) {
    /* Store a chosen numeric value into the already-selected output vector type. */
    uint8_t t = qo_type(result);
    if (t == QO_FLOAT_VEC) {
        qo_float_data(result)[i] = value;
    } else if (t == QO_BOOL_VEC) {
        qo_bool_data(result)[i] = (value != 0.0) ? 1 : 0;
    } else if (t == QO_CHAR_VEC) {
        qo_char_data(result)[i] = (char)(unsigned char)((int)value);
    } else {
        qo_long_data(result)[i] = (int64_t)value;
    }
}

static Qo eval_order_extrema(Qo left, Qo right, int want_greater) {
    if (left == NULL || right == NULL) EVAL_ERROR("operator requires non-null operands");

    uint8_t lt = qo_type(left);
    uint8_t rt = qo_type(right);
    int lv = is_vector_type(lt);
    int rv = is_vector_type(rt);

    /* Reject unsupported domains early (e.g. symbols, dicts, functions). */
    if ((lv && !is_order_vector_type(lt)) || (!lv && !is_order_scalar_type(lt)) ||
        (rv && !is_order_vector_type(rt)) || (!rv && !is_order_scalar_type(rt))) {
        EVAL_ERROR("operator requires int/float/bool/char operands");
    }

    if (!lv && !rv) {
        double l;
        double r;
        if (!scalar_order_value(left, &l) || !scalar_order_value(right, &r)) {
            EVAL_ERROR("operator requires int/float/bool/char operands");
        }
        /* Scalar/scalar: return the original winning operand to preserve scalar type. */
        if (want_greater) return qo_clone((l >= r) ? left : right);
        return qo_clone((l <= r) ? left : right);
    }

    if (lv && rv && qo_count(left) != qo_count(right)) {
        EVAL_ERROR("cannot compare vectors of different lengths");
    }

    {
        /* Vector path supports vector-vector and scalar-vector broadcasting. */
        int64_t count = lv ? qo_count(left) : qo_count(right);
        uint8_t out_type = extrema_vector_type(lt, rt);
        Qo result = alloc_same_type(out_type, count);

        for (int64_t i = 0; i < count; i++) {
            double l;
            double r;
            double selected;

            if (lv) l = vec_elem_double(left, i);
            else if (!scalar_order_value(left, &l)) {
                qo_release(result);
                EVAL_ERROR("operator requires int/float/bool/char operands");
            }

            if (rv) r = vec_elem_double(right, i);
            else if (!scalar_order_value(right, &r)) {
                qo_release(result);
                EVAL_ERROR("operator requires int/float/bool/char operands");
            }

            selected = want_greater ? ((l >= r) ? l : r) : ((l <= r) ? l : r);
            store_extrema_elem(result, i, selected);
        }

        /* CHAR vectors are NUL-terminated in this runtime. */
        if (out_type == QO_CHAR_VEC) qo_char_data(result)[count] = '\0';
        return result;
    }
}

Qo eval_max_of(Qo left, Qo right) {
    return eval_order_extrema(left, right, 1);
}

Qo eval_min_of(Qo left, Qo right) {
    return eval_order_extrema(left, right, 0);
}

Qo eval_dict_creation(Qo left, Qo right) {
    int64_t ln = dict_collection_count(left);
    int64_t rn = dict_collection_count(right);
    if (ln != rn) EVAL_ERROR("dictionary key and value counts must match");

    {
        Qo result = alloc_dict_block(ln);
        QO_DICT_KTYPE(result) = (left != NULL && is_vector_type(QO_TYPE(left))) ? QO_TYPE(left)
                                               : (left != NULL ? QO_TYPE(left) : 0);
        QO_DICT_VTYPE(result) = (right != NULL && is_vector_type(QO_TYPE(right))) ? QO_TYPE(right)
                                               : (right != NULL ? QO_TYPE(right) : 0);
        for (int64_t i = 0; i < ln; i++) {
            QO_DICT_KEYS(result)[i] = dict_elem_copy(left, i);
            QO_DICT_VALS(result)[i] = dict_elem_copy(right, i);
        }
        return result;
    }
}

static Qo join_int_values(Qo left, Qo right) {
    int64_t ln = is_vector_type(QO_TYPE(left)) ? QO_COUNT(left) : 1;
    int64_t rn = is_vector_type(QO_TYPE(right)) ? QO_COUNT(right) : 1;
    Qo result = alloc_data_vec(QO_LONG_VEC, ln + rn);
    int64_t di = 0;

    if (is_vector_type(QO_TYPE(left))) {
        for (int64_t i = 0; i < ln; i++) {
            QO_LONG_DATA(result)[di++] = int_elem_value(left, i);
        }
    } else {
        QO_LONG_DATA(result)[di++] = int_elem_value(left, 0);
    }

    if (is_vector_type(QO_TYPE(right))) {
        for (int64_t i = 0; i < rn; i++) {
            QO_LONG_DATA(result)[di++] = int_elem_value(right, i);
        }
    } else {
        QO_LONG_DATA(result)[di] = int_elem_value(right, 0);
    }

    return result;
}

static Qo join_float_values(Qo left, Qo right) {
    int64_t ln = is_vector_type(QO_TYPE(left)) ? QO_COUNT(left) : 1;
    int64_t rn = is_vector_type(QO_TYPE(right)) ? QO_COUNT(right) : 1;
    Qo result = alloc_data_vec(QO_FLOAT_VEC, ln + rn);
    int64_t di = 0;

    if (QO_TYPE(left) == QO_FLOAT_VEC) {
        memcpy(QO_FLOAT_DATA(result), QO_FLOAT_DATA(left), (size_t)ln * 8);
        di = ln;
    } else {
        QO_FLOAT_DATA(result)[di++] = QO_FLOAT_VAL(left);
    }

    if (QO_TYPE(right) == QO_FLOAT_VEC) {
        memcpy(QO_FLOAT_DATA(result) + di, QO_FLOAT_DATA(right), (size_t)rn * 8);
    } else {
        QO_FLOAT_DATA(result)[di] = QO_FLOAT_VAL(right);
    }

    return result;
}

static Qo join_char_values(Qo left, Qo right) {
    int64_t ln = QO_TYPE(left) == QO_CHAR_VEC ? QO_COUNT(left) : 1;
    int64_t rn = QO_TYPE(right) == QO_CHAR_VEC ? QO_COUNT(right) : 1;
    Qo result = alloc_charlike(QO_CHAR_VEC, ln + rn);
    char *d = QO_CHAR_DATA(result);

    if (QO_TYPE(left) == QO_CHAR_VEC) memcpy(d, QO_CHAR_DATA(left), (size_t)ln);
    else d[0] = QO_CHAR_VAL(left);

    if (QO_TYPE(right) == QO_CHAR_VEC) memcpy(d + ln, QO_CHAR_DATA(right), (size_t)rn);
    else d[ln] = QO_CHAR_VAL(right);

    d[ln + rn] = '\0';
    return result;
}

static Qo join_ptr_values(uint8_t out_type, Qo left, Qo right) {
    int64_t ln = is_vector_type(QO_TYPE(left)) ? QO_COUNT(left) : 1;
    int64_t rn = is_vector_type(QO_TYPE(right)) ? QO_COUNT(right) : 1;
    Qo result = alloc_ptr_vec(out_type, ln + rn);
    Qo *d = QO_LIST_DATA(result);

    if (is_vector_type(QO_TYPE(left))) {
        for (int64_t i = 0; i < ln; i++) d[i] = qo_clone(QO_LIST_DATA(left)[i]);
    } else {
        d[0] = qo_clone(left);
    }

    if (is_vector_type(QO_TYPE(right))) {
        for (int64_t i = 0; i < rn; i++) d[ln + i] = qo_clone(QO_LIST_DATA(right)[i]);
    } else {
        d[ln] = qo_clone(right);
    }

    return result;
}

static Qo join_mixed_values(Qo left, Qo right) {
    int64_t ln = is_vector_type(QO_TYPE(left)) ? QO_COUNT(left) : 1;
    int64_t rn = is_vector_type(QO_TYPE(right)) ? QO_COUNT(right) : 1;
    Qo result = alloc_ptr_vec(QO_LIST, ln + rn);
    Qo *d = QO_LIST_DATA(result);

    for (int64_t i = 0; i < ln; i++) d[i] = dict_elem_copy(left, i);
    for (int64_t i = 0; i < rn; i++) d[ln + i] = dict_elem_copy(right, i);

    return result;
}

typedef enum {
    JF_INT,
    JF_FLOAT,
    JF_CHAR,
    JF_SYMBOL,
    JF_LIST,
    JF_OTHER,
} JoinFamily;

static JoinFamily join_family(uint8_t t) {
    if (t == QO_SHORT || t == QO_INT || t == QO_LONG ||
        t == QO_SHORT_VEC || t == QO_INT_VEC || t == QO_LONG_VEC)
        return JF_INT;
    if (t == QO_FLOAT || t == QO_FLOAT_VEC)
        return JF_FLOAT;
    if (t == QO_CHAR || t == QO_CHAR_VEC)
        return JF_CHAR;
    if (t == QO_SYMBOL || t == QO_SYM_VEC)
        return JF_SYMBOL;
    if (t == QO_LIST)
        return JF_LIST;
    return JF_OTHER;
}

Qo eval_comma_binop(Qo left, Qo right) {
    if (left == NULL || right == NULL) EVAL_ERROR("comma operator requires non-null operands");
    uint8_t lt = QO_TYPE(left);
    uint8_t rt = QO_TYPE(right);
    JoinFamily lf = join_family(lt);
    JoinFamily rf = join_family(rt);

    if (lf == rf) {
        switch (lf) {
            case JF_INT:    return join_int_values(left, right);
            case JF_FLOAT:  return join_float_values(left, right);
            case JF_CHAR:   return join_char_values(left, right);
            case JF_SYMBOL: return join_ptr_values(QO_SYM_VEC, left, right);
            case JF_LIST:   return join_ptr_values(QO_LIST, left, right);
            default:        break;
        }
    }

    return join_mixed_values(left, right);
}

typedef int64_t (*IntBinaryScalarKernel)(int64_t left, int64_t right);
typedef void (*IntVectorKernel)(Qo result,
                                Qo left,
                                Qo right,
                                int left_is_vec,
                                int right_is_vec,
                                int64_t count,
                                IntBinaryScalarKernel kernel);

/* Centralize integer arithmetic policy here so scalar and future SIMD backends
   share one definition of +, -, and *. */
static int64_t int_add_values(int64_t left, int64_t right) {
    return left + right;
}

static int64_t int_subtract_values(int64_t left, int64_t right) {
    return left - right;
}

static int64_t int_multiply_values(int64_t left, int64_t right) {
    return left * right;
}

static IntBinaryScalarKernel select_int_scalar_kernel(TokenType op) {
    if (op == TOKEN_PLUS) return int_add_values;
    if (op == TOKEN_MINUS) return int_subtract_values;
    if (op == TOKEN_STAR) return int_multiply_values;
    return NULL;
}

/* This is the extension point for future SIMD work. The current backend is a
   scalar loop, but callers do not know that. */
static void execute_int_vector_kernel_scalar(Qo result,
                                             Qo left,
                                             Qo right,
                                             int left_is_vec,
                                             int right_is_vec,
                                             int64_t count,
                                             IntBinaryScalarKernel kernel) {
    for (int64_t i = 0; i < count; i++) {
        int64_t li = left_is_vec ? int_elem_value(left, i) : get_integer_value(left);
        int64_t ri = right_is_vec ? int_elem_value(right, i) : get_integer_value(right);
        store_int_vec_elem(result, i, kernel(li, ri));
    }
}

static IntVectorKernel select_int_vector_kernel(TokenType op,
                                                uint8_t left_type,
                                                uint8_t right_type,
                                                uint8_t out_type) {
    (void)op;
    (void)left_type;
    (void)right_type;
    (void)out_type;
    return execute_int_vector_kernel_scalar;
}

static Qo execute_exact_int_scalar_binop(Qo left, Qo right, TokenType op) {
    uint8_t out_type = QO_LONG;
    if (op == TOKEN_PLUS || op == TOKEN_MINUS) {
        int rank = int_rank_from_type(QO_TYPE(left));
        int r_rank = int_rank_from_type(QO_TYPE(right));
        if (r_rank > rank) rank = r_rank;
        out_type = scalar_type_from_int_rank(rank);
    }
    return make_int_scalar_of_type(out_type,
                                   select_int_scalar_kernel(op)(get_integer_value(left), get_integer_value(right)));
}

static Qo execute_float_scalar_binop(Qo left, Qo right, TokenType op) {
    double out;
    int is_dm;

    if (!apply_numeric_op(value_as_double(left), value_as_double(right), op, &out, &is_dm)) {
        return NULL;
    }

    return make_numeric_result(out, QO_TYPE(left) == QO_FLOAT || QO_TYPE(right) == QO_FLOAT || is_dm);
}

static Qo execute_int_vector_binop(Qo left, Qo right, TokenType op) {
    int left_is_vec = is_numeric_vector_type(QO_TYPE(left));
    int right_is_vec = is_numeric_vector_type(QO_TYPE(right));
    int64_t count = left_is_vec ? QO_COUNT(left) : QO_COUNT(right);
    uint8_t out_type;
    Qo result;
    IntVectorKernel kernel;

    if (left_is_vec && right_is_vec && QO_COUNT(left) != QO_COUNT(right)) {
        EVAL_ERROR("cannot operate on vectors of different lengths");
    }

    if (op == TOKEN_PLUS || op == TOKEN_MINUS) {
        int rank = int_rank_from_type(QO_TYPE(left));
        int r_rank = int_rank_from_type(QO_TYPE(right));
        if (r_rank > rank) rank = r_rank;
        out_type = vector_type_from_int_rank(rank);
    } else {
        out_type = QO_LONG_VEC;
    }

    result = alloc_data_vec(out_type, count);
    kernel = select_int_vector_kernel(op, QO_TYPE(left), QO_TYPE(right), out_type);
    kernel(result, left, right, left_is_vec, right_is_vec, count, select_int_scalar_kernel(op));
    return result;
}

static uint8_t scalar_to_vector_type(uint8_t scalar_type) {
    if (scalar_type == QO_SHORT) return QO_SHORT_VEC;
    if (scalar_type == QO_INT) return QO_INT_VEC;
    if (scalar_type == QO_LONG) return QO_LONG_VEC;
    if (scalar_type == QO_FLOAT) return QO_FLOAT_VEC;
    return QO_LONG_VEC;
}

static Qo eval_numeric_vector_binop(Qo left, Qo right, TokenType op) {
    uint8_t lt = QO_TYPE(left);
    uint8_t rt = QO_TYPE(right);
    int use_float = lt == QO_FLOAT || lt == QO_FLOAT_VEC || rt == QO_FLOAT || rt == QO_FLOAT_VEC || op == TOKEN_DIVIDE || op == TOKEN_STAR_STAR;

    if (use_float) {
        int left_is_vec = is_numeric_vector_type(lt);
        int right_is_vec = is_numeric_vector_type(rt);
        int64_t count = left_is_vec ? QO_COUNT(left) : QO_COUNT(right);
        Qo result;

        if (left_is_vec && right_is_vec && QO_COUNT(left) != QO_COUNT(right)) {
            EVAL_ERROR("cannot operate on vectors of different lengths");
        }

        result = alloc_data_vec(QO_FLOAT_VEC, count);
        for (int64_t i = 0; i < count; i++) {
            double lv = left_is_vec ? vec_elem_double(left, i) : value_as_double(left);
            double rv = right_is_vec ? vec_elem_double(right, i) : value_as_double(right);
            double out;
            int is_dm;
            if (!apply_numeric_op(lv, rv, op, &out, &is_dm)) {
                qo_release(result);
                return NULL;
            }
            QO_FLOAT_DATA(result)[i] = out;
        }
        return result;
    }

    return execute_int_vector_binop(left, right, op);
}

static Qo eval_dict_scalar_binop(Qo left, Qo right, TokenType op) {
    Qo dict = (left != NULL && QO_TYPE(left) == QO_DICT) ? left : right;
    Qo scalar = (left != NULL && QO_TYPE(left) == QO_DICT) ? right : left;
    int dict_is_left = (left != NULL && QO_TYPE(left) == QO_DICT);
    int64_t n = QO_DICT_COUNT(dict);
    Qo result = alloc_dict_block(n);

    QO_DICT_KTYPE(result) = QO_DICT_KTYPE(dict);
    QO_DICT_VTYPE(result) = QO_DICT_VTYPE(dict);

    if (scalar == NULL || !is_numeric_scalar_type(QO_TYPE(scalar))) {
        EVAL_ERROR("dictionary arithmetic requires numeric scalar values");
    }

    for (int64_t i = 0; i < n; i++) {
        Qo dict_value = QO_DICT_VALS(dict)[i];
        Qo value_result;
        int value_is_float;

        if (dict_value == NULL || !is_numeric_scalar_type(QO_TYPE(dict_value))) {
            fprintf(stderr, "Error: dictionary arithmetic requires numeric scalar values\n");
            evaluator_request_error();
            for (int64_t j = i; j < n; j++) QO_DICT_KEYS(result)[j] = NULL;
            for (int64_t j = i; j < n; j++) QO_DICT_VALS(result)[j] = NULL;
            qo_release(result);
            return NULL;
        }

        QO_DICT_KEYS(result)[i] = qo_clone(QO_DICT_KEYS(dict)[i]);
        value_is_float = (QO_TYPE(dict_value) == QO_FLOAT || QO_TYPE(scalar) == QO_FLOAT);

        if (!value_is_float && (op == TOKEN_PLUS || op == TOKEN_MINUS || op == TOKEN_STAR)) {
            Qo lv = dict_is_left ? dict_value : scalar;
            Qo rv = dict_is_left ? scalar : dict_value;
            value_result = execute_exact_int_scalar_binop(lv, rv, op);
        } else {
            Qo lv = dict_is_left ? dict_value : scalar;
            Qo rv = dict_is_left ? scalar : dict_value;
            value_result = execute_float_scalar_binop(lv, rv, op);
        }

        if (value_result == NULL) {
            for (int64_t j = i + 1; j < n; j++) QO_DICT_KEYS(result)[j] = NULL;
            for (int64_t j = i; j < n; j++) QO_DICT_VALS(result)[j] = NULL;
            qo_release(result);
            return NULL;
        }

        QO_DICT_VALS(result)[i] = value_result;
    }

    if (n > 0 && QO_DICT_VALS(result)[0] != NULL) {
        QO_DICT_VTYPE(result) = scalar_to_vector_type(QO_TYPE(QO_DICT_VALS(result)[0]));
    }

    return result;
}

Qo eval_add(Qo left, Qo right) {
    if (left == NULL || right == NULL) EVAL_ERROR("arithmetic operations require non-null operands");
    uint8_t lt = QO_TYPE(left);
    uint8_t rt = QO_TYPE(right);

    if ((lt == QO_DICT && is_numeric_scalar_type(rt)) ||
        (rt == QO_DICT && is_numeric_scalar_type(lt))) {
        return eval_dict_scalar_binop(left, right, TOKEN_PLUS);
    }
    if (!is_numeric_operand(left) || !is_numeric_operand(right)) {
        EVAL_ERROR("arithmetic operations require numeric operands");
    }
    if (lt == QO_LIST || rt == QO_LIST) {
        EVAL_ERROR("list arithmetic is not supported");
    }
    if (is_numeric_vector_type(lt) || is_numeric_vector_type(rt)) {
        return eval_numeric_vector_binop(left, right, TOKEN_PLUS);
    }
    if (is_numeric_int_type(lt) && is_numeric_int_type(rt)) {
        return execute_exact_int_scalar_binop(left, right, TOKEN_PLUS);
    }

    return execute_float_scalar_binop(left, right, TOKEN_PLUS);
}

Qo eval_subtract(Qo left, Qo right) {
    if (left == NULL || right == NULL) EVAL_ERROR("arithmetic operations require non-null operands");
    uint8_t lt = QO_TYPE(left);
    uint8_t rt = QO_TYPE(right);

    if ((lt == QO_DICT && is_numeric_scalar_type(rt)) ||
        (rt == QO_DICT && is_numeric_scalar_type(lt))) {
        return eval_dict_scalar_binop(left, right, TOKEN_MINUS);
    }
    if (!is_numeric_operand(left) || !is_numeric_operand(right)) {
        EVAL_ERROR("arithmetic operations require numeric operands");
    }
    if (lt == QO_LIST || rt == QO_LIST) {
        EVAL_ERROR("list arithmetic is not supported");
    }
    if (is_numeric_vector_type(lt) || is_numeric_vector_type(rt)) {
        return eval_numeric_vector_binop(left, right, TOKEN_MINUS);
    }
    if (is_numeric_int_type(lt) && is_numeric_int_type(rt)) {
        return execute_exact_int_scalar_binop(left, right, TOKEN_MINUS);
    }

    return execute_float_scalar_binop(left, right, TOKEN_MINUS);
}

Qo eval_multiply(Qo left, Qo right) {
    if (left == NULL || right == NULL) EVAL_ERROR("arithmetic operations require non-null operands");
    uint8_t lt = QO_TYPE(left);
    uint8_t rt = QO_TYPE(right);

    if ((lt == QO_DICT && is_numeric_scalar_type(rt)) ||
        (rt == QO_DICT && is_numeric_scalar_type(lt))) {
        return eval_dict_scalar_binop(left, right, TOKEN_STAR);
    }
    if (!is_numeric_operand(left) || !is_numeric_operand(right)) {
        EVAL_ERROR("arithmetic operations require numeric operands");
    }
    if (lt == QO_LIST || rt == QO_LIST) {
        EVAL_ERROR("list arithmetic is not supported");
    }
    if (is_numeric_vector_type(lt) || is_numeric_vector_type(rt)) {
        return eval_numeric_vector_binop(left, right, TOKEN_STAR);
    }
    if (is_numeric_int_type(lt) && is_numeric_int_type(rt)) {
        return execute_exact_int_scalar_binop(left, right, TOKEN_STAR);
    }

    return execute_float_scalar_binop(left, right, TOKEN_STAR);
}

Qo eval_divide(Qo left, Qo right) {
    if (left == NULL || right == NULL) EVAL_ERROR("arithmetic operations require non-null operands");
    uint8_t lt = QO_TYPE(left);
    uint8_t rt = QO_TYPE(right);

    if ((lt == QO_DICT && is_numeric_scalar_type(rt)) ||
        (rt == QO_DICT && is_numeric_scalar_type(lt))) {
        return eval_dict_scalar_binop(left, right, TOKEN_DIVIDE);
    }
    if (!is_numeric_operand(left) || !is_numeric_operand(right)) {
        EVAL_ERROR("arithmetic operations require numeric operands");
    }
    if (lt == QO_LIST || rt == QO_LIST) {
        EVAL_ERROR("list arithmetic is not supported");
    }
    if (is_numeric_vector_type(lt) || is_numeric_vector_type(rt)) {
        return eval_numeric_vector_binop(left, right, TOKEN_DIVIDE);
    }

    return execute_float_scalar_binop(left, right, TOKEN_DIVIDE);
}

Qo eval_power(Qo left, Qo right) {
    if (left == NULL || right == NULL) EVAL_ERROR("arithmetic operations require non-null operands");
    uint8_t lt = QO_TYPE(left);
    uint8_t rt = QO_TYPE(right);

    if ((lt == QO_DICT && is_numeric_scalar_type(rt)) ||
        (rt == QO_DICT && is_numeric_scalar_type(lt))) {
        return eval_dict_scalar_binop(left, right, TOKEN_STAR_STAR);
    }
    if (!is_numeric_operand(left) || !is_numeric_operand(right)) {
        EVAL_ERROR("arithmetic operations require numeric operands");
    }
    if (lt == QO_LIST || rt == QO_LIST) {
        EVAL_ERROR("list arithmetic is not supported");
    }
    if (is_numeric_vector_type(lt) || is_numeric_vector_type(rt)) {
        return eval_numeric_vector_binop(left, right, TOKEN_STAR_STAR);
    }

    return execute_float_scalar_binop(left, right, TOKEN_STAR_STAR);
}

