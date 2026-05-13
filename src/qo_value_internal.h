#pragma once

#include "qo_value.h"

#define QO_TYPE(q)          qo_type(q)
#define QO_ATTRS(q)         qo_attrs(q)

#define QO_SHORT_VAL(q)     qo_short(q)
#define QO_INT_VAL(q)       qo_int(q)
#define QO_LONG_VAL(q)      qo_long(q)
#define QO_FLOAT_VAL(q)     qo_float(q)
#define QO_CHAR_VAL(q)      qo_char(q)
#define QO_BOOL_VAL(q)      qo_bool(q)
#define QO_BYTE_VAL(q)      qo_byte(q)
#define QO_SYMBOL_ID(q)     qo_symbol_id(q)
#define QO_SET_SHORT_VAL(q, v)  qo_set_short((q), (v))
#define QO_SET_INT_VAL(q, v)    qo_set_int((q), (v))
#define QO_SET_LONG_VAL(q, v)   qo_set_long((q), (v))
#define QO_SET_FLOAT_VAL(q, v)  qo_set_float((q), (v))
#define QO_SET_CHAR_VAL(q, v)   qo_set_char((q), (v))
#define QO_SET_BOOL_VAL(q, v)   qo_set_bool((q), (v))
#define QO_SET_BYTE_VAL(q, v)   qo_set_byte((q), (v))
#define QO_SET_SYMBOL_ID(q, v)  qo_set_symbol_id((q), (v))

#define QO_COUNT(q)         qo_count(q)
#define QO_SET_COUNT(q, v)  qo_set_count((q), (v))

#define QO_SHORT_DATA(q)    ((int16_t *)((q)->storage))
#define QO_INT_DATA(q)      ((int32_t *)((q)->storage))
#define QO_LONG_DATA(q)     ((int64_t *)((q)->storage))
#define QO_FLOAT_DATA(q)    ((double *)((q)->storage))
#define QO_BOOL_DATA(q)     ((uint8_t *)((q)->storage))
#define QO_BYTE_DATA(q)     ((uint8_t *)((q)->storage))
#define QO_CHAR_DATA(q)     ((char *)((q)->storage))
#define QO_LIST_DATA(q)     ((Qo *)((q)->storage))
#define QO_STR(q)           ((char *)((q)->storage))
#define QO_STR_LEN(q)       QO_COUNT(q)

/* dict payload: [0] ktype [1] vtype [2..] keys then vals */
#define QO_DICT_COUNT(q)    QO_COUNT(q)
#define QO_DICT_KTYPE(q)    ((q)->storage[0])
#define QO_DICT_VTYPE(q)    ((q)->storage[1])
#define QO_DICT_KEYS(q)     ((Qo *)((q)->storage + 2))
#define QO_DICT_VALS(q)     ((Qo *)((q)->storage + 2 + (size_t)QO_DICT_COUNT(q) * sizeof(Qo)))

/* function payload: [0..7] pc [8..15] bc [16..23] sl [24..] params/body/source */
#define QO_FN_PC(q)         (*(int64_t *)((q)->storage + 0))
#define QO_FN_BC(q)         (*(int64_t *)((q)->storage + 8))
#define QO_FN_SL(q)         (*(int64_t *)((q)->storage + 16))
#define QO_FN_PARAMS(q)     ((Qo *)((q)->storage + 24))
#define QO_FN_BODY(q)       ((Qo *)((q)->storage + 24 + (size_t)QO_FN_PC(q) * sizeof(Qo)))
#define QO_FN_SOURCE(q)     ((char *)((q)->storage + 24 + (size_t)(QO_FN_PC(q) + QO_FN_BC(q)) * sizeof(Qo)))

/* projection payload: [0..7] ac [8..15] fn [16..] args */
#define QO_PROJ_AC(q)       (*(int64_t *)((q)->storage + 0))
#define QO_PROJ_FUNC(q)     (*(Qo *)((q)->storage + 8))
#define QO_PROJ_ARGS(q)     ((Qo *)((q)->storage + 16))
