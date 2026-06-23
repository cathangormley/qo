#include "random.h"
#include "evaluator_internal.h"
#include "symbol_intern.h"
#include "internal.h"

/* xoshiro256** PRNG — 256-bit state, period 2^256-1, passes BigCrush */
static uint64_t g_random_state[4];
static uint64_t g_last_seed;

static inline uint64_t rotl64(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

static uint64_t splitmix64(uint64_t *x) {
    uint64_t z = (*x += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

static uint64_t xoshiro_next(void) {
    uint64_t r = rotl64(g_random_state[1] * 5, 7) * 9;
    uint64_t t = g_random_state[1] << 17;
    g_random_state[2] ^= g_random_state[0];
    g_random_state[3] ^= g_random_state[1];
    g_random_state[1] ^= g_random_state[2];
    g_random_state[0] ^= g_random_state[3];
    g_random_state[2] ^= t;
    g_random_state[3] = rotl64(g_random_state[3], 45);
    return r;
}

void random_init(uint64_t seed) {
    uint64_t sm = seed;
    g_random_state[0] = splitmix64(&sm);
    g_random_state[1] = splitmix64(&sm);
    g_random_state[2] = splitmix64(&sm);
    g_random_state[3] = splitmix64(&sm);
    g_last_seed = seed;
}

uint64_t random_current_seed(void) {
    return g_last_seed;
}

/* if sys.seed was changed, re-seed the PRNG */
static void random_sync_seed(Environment *env) {
    Environment *root = env_root(env);
    int found;
    Qo sys_val = env_get(root, qo_symbol_intern("sys"), &found);
    if (!found || qo_type(sys_val) != QO_DICT) { qo_release(sys_val); return; }
    int64_t n = QO_DICT_COUNT(sys_val);
    Qo seed_sym = qo_symbol_intern("seed");
    for (int64_t i = 0; i < n; i++) {
        if (value_equals(QO_DICT_KEYS(sys_val)[i], seed_sym)) {
            Qo sv = QO_DICT_VALS(sys_val)[i];
            uint64_t new_seed = (uint64_t)value_as_double(sv);
            if (new_seed != g_last_seed) random_init(new_seed);
            break;
        }
    }
    qo_release(seed_sym);
    qo_release(sys_val);
}

/*
 * Random number generation via the ? operator.
 *
 *   n ? 0j  / n ? 0i  / n ? 0h  / n ? 0x  / n ? 0b
 *     → n random values of the given integer type (full range).
 *
 *   n ? m   (m > 0, any numeric type)
 *     → n random values in [0, m) of m's type.
 *
 *   n ? 0   (default long zero)
 *     → n random longs (full range).
 *
 *   n ? 0f  → n zero floats (no special zero handling for float).
 */
Qo eval_random(Qo count_val, Qo rhs, Environment *env) {
    (void)env;
    if (count_val == NULL || !is_numeric_scalar_type(qo_type(count_val)))
        EVAL_ERROR("?: left operand must be a numeric scalar count");
    int64_t n = (int64_t)value_as_double(count_val);
    if (n < 0) EVAL_ERROR("?: count must be non-negative");
    if (rhs == NULL) EVAL_ERROR("?: right operand must be a numeric scalar");
    {
        uint8_t rt_check = qo_type(rhs);
        if (!is_numeric_scalar_type(rt_check) && rt_check != QO_BYTE)
            EVAL_ERROR("?: right operand must be a numeric scalar");
    }

    random_sync_seed(env);

    uint8_t rt = qo_type(rhs);
    double rhs_val = value_as_double(rhs);
    uint8_t result_type;
    int full_range = 0;

    if (rhs_val == 0.0 && rt != QO_FLOAT) {
        /* integer zero → full-range random of that type */
        full_range = 1;
        switch (rt) {
            case QO_SHORT:    result_type = QO_SHORT;    break;
            case QO_INT:      result_type = QO_INT;      break;
            case QO_LONG:     result_type = QO_LONG;     break;
            case QO_BYTE:     result_type = QO_BYTE;     break;
            case QO_BOOL:     result_type = QO_BOOL;     break;
            default:          result_type = QO_LONG;     break;
        }
    } else {
        result_type = rt;
    }

    uint8_t vec_type = type_base_type(result_type);

    if (n == 0) {
        Qo r = alloc_data_vec(vec_type, 0);
        (void)env;        return r;
    }

    if (n == 1) {
        uint64_t bits = xoshiro_next();
        Qo r;
        if (full_range) {
            switch (result_type) {
                case QO_SHORT: r = make_short_value((int16_t)(bits & 0xFFFF)); break;
                case QO_INT:   r = make_int_value((int32_t)(bits & 0xFFFFFFFF)); break;
                case QO_LONG:  r = make_long_value((int64_t)bits); break;
                case QO_BYTE:  r = make_byte_value((uint8_t)(bits & 0xFF)); break;
                case QO_BOOL:  r = make_bool_value(bits & 1); break;
                default:       r = make_long_value((int64_t)bits); break;
            }
        } else {
            switch (result_type) {
                case QO_SHORT: {
                    int16_t m = (int16_t)rhs_val;
                    r = m > 0 ? make_short_value((int16_t)(xoshiro_next() % (uint64_t)m)) : make_short_value(0);
                    break;
                }
                case QO_INT: {
                    int32_t m = (int32_t)rhs_val;
                    r = m > 0 ? make_int_value((int32_t)(xoshiro_next() % (uint64_t)m)) : make_int_value(0);
                    break;
                }
                case QO_LONG: {
                    int64_t m = (int64_t)rhs_val;
                    if (m <= 0) { r = make_long_value(0); }
                    else { r = make_long_value((int64_t)(xoshiro_next() % (uint64_t)m)); }
                    break;
                }
                case QO_FLOAT: {
                    if (rhs_val <= 0.0) { r = make_float_value(0.0); }
                    else {
                        uint64_t b = xoshiro_next();
                        r = make_float_value((double)(b >> 11) * 0x1.0p-53 * rhs_val);
                    }
                    break;
                }
                case QO_BYTE: {
                    uint8_t m = (uint8_t)rhs_val;
                    r = m > 0 ? make_byte_value((uint8_t)(xoshiro_next() % (uint64_t)m)) : make_byte_value(0);
                    break;
                }
                case QO_BOOL: {
                    int64_t m = (int64_t)rhs_val;
                    r = m > 0 ? make_bool_value(xoshiro_next() % (uint64_t)m) : make_bool_value(0);
                    break;
                }
                default: {
                    int64_t m = (int64_t)rhs_val;
                    r = make_long_value(m > 0 ? (int64_t)(xoshiro_next() % (uint64_t)m) : 0);
                    break;
                }
            }
        }
        (void)env;        return r;
    }

    Qo result = alloc_data_vec(vec_type, n);
    for (int64_t i = 0; i < n; i++) {
        if (full_range) {
            uint64_t bits = xoshiro_next();
            switch (result_type) {
                case QO_SHORT: qo_short_data(result)[i] = (int16_t)(bits & 0xFFFF); break;
                case QO_INT:   qo_int_data(result)[i]   = (int32_t)(bits & 0xFFFFFFFF); break;
                case QO_LONG:  qo_long_data(result)[i]  = (int64_t)bits; break;
                case QO_BYTE:  qo_byte_data(result)[i]  = (uint8_t)(bits & 0xFF); break;
                case QO_BOOL:  qo_bool_data(result)[i]  = (uint8_t)(bits & 1); break;
                default:       qo_long_data(result)[i]  = (int64_t)bits; break;
            }
        } else {
            switch (result_type) {
                case QO_SHORT: {
                    int16_t m = (int16_t)rhs_val;
                    qo_short_data(result)[i] = m > 0 ? (int16_t)(xoshiro_next() % (uint64_t)m) : 0;
                    break;
                }
                case QO_INT: {
                    int32_t m = (int32_t)rhs_val;
                    qo_int_data(result)[i] = m > 0 ? (int32_t)(xoshiro_next() % (uint64_t)m) : 0;
                    break;
                }
                case QO_LONG: {
                    int64_t m = (int64_t)rhs_val;
                    if (m <= 0) { qo_long_data(result)[i] = 0; }
                    else { qo_long_data(result)[i] = (int64_t)(xoshiro_next() % (uint64_t)m); }
                    break;
                }
                case QO_FLOAT: {
                    if (rhs_val <= 0.0) { qo_float_data(result)[i] = 0.0; }
                    else {
                        uint64_t b = xoshiro_next();
                        qo_float_data(result)[i] = (double)(b >> 11) * 0x1.0p-53 * rhs_val;
                    }
                    break;
                }
                case QO_BYTE: {
                    uint8_t m = (uint8_t)rhs_val;
                    qo_byte_data(result)[i] = m > 0 ? (uint8_t)(xoshiro_next() % (uint64_t)m) : 0;
                    break;
                }
                case QO_BOOL: {
                    int64_t m = (int64_t)rhs_val;
                    qo_bool_data(result)[i] = m > 0 ? (uint8_t)(xoshiro_next() % (uint64_t)m) : 0;
                    break;
                }
                default: {
                    int64_t m = (int64_t)rhs_val;
                    qo_long_data(result)[i] = m > 0 ? (int64_t)(xoshiro_next() % (uint64_t)m) : 0;
                    break;
                }
            }
        }
    }
    return result;
}
