#pragma once

#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <math.h>

/*
 * Qo object model (k0-like):
 * - attribute: reserved metadata byte (currently unused by runtime)
 * - type_tag: runtime type discriminator
 * - reference_count: in-object reference count
 * - union: atoms store their value inline; collections have count + data[]
 */
typedef struct QoObject {
    uint8_t attribute;
    uint8_t type_tag;
    uint32_t reference_count;
    union {
        int16_t short_val;
        int32_t int_val;
        int64_t long_val;
        double double_val;
        char char_val;
        uint8_t byte_val;
        uint8_t bool_val;
        int64_t symbol_id;
        struct {
            int64_t count;
            _Alignas(int64_t) uint8_t data[];
        };
    };
} QoObject;

typedef QoObject *Qo;

/* ── Type bytes ─────────────────────────────────────────────────────────── */
#define QO_SHORT       0x01
#define QO_INT         0x02
#define QO_LONG        0x03
#define QO_FLOAT       0x04
#define QO_CHAR        0x05
#define QO_BOOL        0x06
#define QO_SYMBOL      0x07
#define QO_OPERATOR    0x08
#define QO_PROJECTOR   0x09
#define QO_BUILTIN     0x0B
#define QO_BYTE        0x0A
#define QO_TIMESTAMP   0x0C
#define QO_TIMESPAN    0x0D
#define QO_SHORT_VEC   0x11
#define QO_INT_VEC     0x12
#define QO_LONG_VEC    0x13
#define QO_FLOAT_VEC   0x14
#define QO_CHAR_VEC    0x15
#define QO_BOOL_VEC    0x16
#define QO_SYM_VEC     0x17
#define QO_BYTE_VEC    0x18
#define QO_TIMESTAMP_VEC 0x19
#define QO_TIMESPAN_VEC  0x1A
#define QO_LIST        0x20
#define QO_DICT        0x21
#define QO_FUNCTION    0x22
#define QO_PROJECTION  0x23
#define QO_EACHED      0x24
#define QO_ADVERB      0x25

/* ── Sentinel values for null and infinity ──────────────────────────── */
#define QO_SHORT_NULL    INT16_MIN
#define QO_INT_NULL      INT32_MIN
#define QO_INT_INF       INT32_MAX
#define QO_INT_NEGINF    (INT32_MIN + 1)
#define QO_LONG_NULL     INT64_MIN
#define QO_LONG_INF      INT64_MAX
#define QO_LONG_NEGINF   (INT64_MIN + 1)
#define QO_FLOAT_NULL    NAN

/* ── Stable external accessor API ───────────────────────────────────────── */
static inline int qo_is_null(Qo q) {
    return q == NULL;
}

static inline uint8_t qo_type(Qo q) {
    return q->type_tag;
}

static inline uint8_t qo_attrs(Qo q) {
    return q->attribute;
}

static inline int64_t qo_count(Qo q) {
    return q->count;
}

static inline int16_t qo_short(Qo q) {
    return q->short_val;
}

static inline int32_t qo_int(Qo q) {
    return q->int_val;
}

static inline int64_t qo_long(Qo q) {
    return q->long_val;
}

static inline int64_t qo_timestamp(Qo q) {
    return q->long_val;
}

static inline int64_t *qo_timestamp_data(Qo q) {
    return (int64_t *)q->data;
}

static inline int64_t qo_timespan(Qo q) {
    return q->long_val;
}

static inline int64_t *qo_timespan_data(Qo q) {
    return (int64_t *)q->data;
}

static inline int64_t qo_symbol_id(Qo q) {
    return q->symbol_id;
}

static inline double qo_float(Qo q) {
    return q->double_val;
}

static inline char qo_char(Qo q) {
    return q->char_val;
}

static inline uint8_t qo_bool(Qo q) {
    return q->bool_val;
}

static inline uint8_t qo_byte(Qo q) {
    return q->byte_val;
}

static inline void qo_set_count(Qo q, int64_t value) {
    q->count = value;
}

static inline void qo_set_short(Qo q, int16_t value) {
    q->short_val = value;
}

static inline void qo_set_int(Qo q, int32_t value) {
    q->int_val = value;
}

static inline void qo_set_long(Qo q, int64_t value) {
    q->long_val = value;
}

static inline void qo_set_symbol_id(Qo q, int64_t value) {
    q->symbol_id = value;
}

static inline void qo_set_float(Qo q, double value) {
    q->double_val = value;
}

static inline void qo_set_char(Qo q, char value) {
    q->char_val = value;
}

static inline void qo_set_bool(Qo q, uint8_t value) {
    q->bool_val = value;
}

static inline void qo_set_byte(Qo q, uint8_t value) {
    q->byte_val = value;
}

static inline int16_t *qo_short_data(Qo q) {
    return (int16_t *)q->data;
}

static inline int32_t *qo_int_data(Qo q) {
    return (int32_t *)q->data;
}

static inline int64_t *qo_long_data(Qo q) {
    return (int64_t *)q->data;
}

static inline double *qo_float_data(Qo q) {
    return (double *)q->data;
}

static inline uint8_t *qo_bool_data(Qo q) {
    return (uint8_t *)q->data;
}

static inline uint8_t *qo_byte_data(Qo q) {
    return (uint8_t *)q->data;
}

static inline char *qo_char_data(Qo q) {
    return (char *)q->data;
}

static inline Qo *qo_ptr_data(Qo q) {
    return (Qo *)q->data;
}

const char *qo_symbol_name(Qo q);

Qo qo_retain(Qo q);
void qo_release(Qo q);
uint32_t qo_refcount(Qo q);
Qo qo_clone(Qo q);

void qo_print(Qo q);
void qo_print_with_limits(Qo q, int max_lines, int max_chars_per_line);
int value_equals(Qo a, Qo b);
