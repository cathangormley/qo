#pragma once

#include <stdio.h>
#include "evaluator.h"
#include "qo_value_internal.h"
#include "token.h"

/* Report a runtime error, halt evaluation, return Null. */
#define EVAL_ERROR(msg) \
    do { \
        fprintf(stderr, "Error: " msg "\n"); \
        evaluator_request_error(); \
        return make_null_value(); \
    } while (0)

#define EVAL_ERROR_FMT(fmt, ...) \
    do { \
        fprintf(stderr, "Error: " fmt "\n", __VA_ARGS__); \
        evaluator_request_error(); \
        return make_null_value(); \
    } while (0)

typedef Qo (*BuiltinFn)(Qo arg, Environment *env);

typedef struct {
    const char *name;
    BuiltinFn fn;
} BuiltinSpec;

Qo make_symbol_value(const char *text);
Qo make_keyword_value(const char *text);
Qo make_null_value(void);
Qo make_short_value(int16_t value);
Qo make_int_value(int32_t value);
Qo make_long_value(int64_t value);
Qo make_float_value(double value);
Qo make_char_value(char c);
Qo make_bool_value(int v);
Qo make_byte_value(uint8_t v);
Qo make_projector_value(void);
Qo make_eached_value(Qo func);
Qo qo_alloc_raw(size_t size);
Qo qo_make_list_take(Qo *elements, int64_t count);
Qo make_function_value(char **params, int param_count,
                       Qo *body, int body_count,
                       const char *source, int source_len);
/* Block / collection allocators (defined in values.c) */
Qo alloc_data_vec(uint8_t type, int64_t count);
Qo alloc_ptr_vec(uint8_t type, int64_t count);
Qo alloc_charlike(uint8_t type, int64_t count);
Qo alloc_dict_block(int64_t count);
int64_t dict_collection_count(Qo v);
Qo dict_elem_copy(Qo v, int64_t i);
int is_vector_type(uint8_t type);
int is_numeric_scalar_type(uint8_t type);
int is_numeric_vector_type(uint8_t type);
int is_symbolish_type(uint8_t type);
int is_charish_type(uint8_t type);
int is_non_numeric_operand(Qo value);
double value_as_double(Qo value);
Qo build_collection_from_copies(Qo *elements, int count, uint8_t collection_type);
Qo extract_dict_side(Qo dict, int want_keys);
Qo eval_dict_creation(Qo left, Qo right);
Qo eval_comma_binop(Qo left, Qo right);
Qo eval_take(Qo left, Qo right);
Qo eval_drop(Qo left, Qo right);
Qo eval_equals(Qo left, Qo right);
Qo eval_less(Qo left, Qo right);
Qo eval_greater(Qo left, Qo right);
Qo eval_lte(Qo left, Qo right);
Qo eval_gte(Qo left, Qo right);
Qo eval_max_of(Qo left, Qo right);
Qo eval_min_of(Qo left, Qo right);
Qo eval_add(Qo left, Qo right);
Qo eval_subtract(Qo left, Qo right);
Qo eval_multiply(Qo left, Qo right);
Qo eval_divide(Qo left, Qo right);
Qo eval_power(Qo left, Qo right);
int operator_name_to_token(const char *name, TokenType *op);