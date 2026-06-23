#include <stdlib.h>
#include "environment.h"
#include "qo_value.h"
#include "qo_value_internal.h"
#include "evaluator_internal.h"
#include "internal.h"

Environment *env_new(void) {
    return env_new_with_parent(NULL);
}

Environment *env_new_with_parent(Environment *parent) {
    Environment *env = xmalloc(sizeof(Environment));
    env->dict = alloc_dict_block(0);
    QO_DICT_KTYPE(env->dict) = QO_SYMBOL;
    QO_DICT_VTYPE(env->dict) = 0;
    env->parent = parent;
    return env;
}

Environment *env_root(Environment *env) {
    while (env && env->parent) env = env->parent;
    return env;
}

void env_free(Environment *env) {
    if (!env) return;
    qo_release(env->dict);
    free(env);
}

void env_set(Environment *env, Qo key_sym, Qo value) {
    int64_t n = QO_DICT_COUNT(env->dict);
    int64_t key_id = qo_symbol_id(key_sym);

    for (int64_t i = 0; i < n; i++) {
        if (qo_symbol_id(QO_DICT_KEYS(env->dict)[i]) == key_id) {
            qo_release(QO_DICT_VALS(env->dict)[i]);
            QO_DICT_VALS(env->dict)[i] = qo_retain(value);
            return;
        }
    }

    Qo new_dict = alloc_dict_block(n + 1);
    QO_DICT_KTYPE(new_dict) = QO_SYMBOL;
    QO_DICT_VTYPE(new_dict) = QO_DICT_VTYPE(env->dict);
    for (int64_t i = 0; i < n; i++) {
        QO_DICT_KEYS(new_dict)[i] = qo_retain(QO_DICT_KEYS(env->dict)[i]);
        QO_DICT_VALS(new_dict)[i] = qo_retain(QO_DICT_VALS(env->dict)[i]);
    }
    QO_DICT_KEYS(new_dict)[n] = qo_retain(key_sym);
    QO_DICT_VALS(new_dict)[n] = qo_retain(value);
    qo_release(env->dict);
    env->dict = new_dict;
}

Qo env_get(Environment *env, Qo key_sym, int *found) {
    int64_t key_id = qo_symbol_id(key_sym);

    for (Environment *scope = env; scope; scope = scope->parent) {
        int64_t n = QO_DICT_COUNT(scope->dict);
        for (int64_t i = 0; i < n; i++) {
            if (qo_symbol_id(QO_DICT_KEYS(scope->dict)[i]) == key_id) {
                *found = 1;
                return qo_retain(QO_DICT_VALS(scope->dict)[i]);
            }
        }
    }

    *found = 0;
    return make_null_value();
}
