#include <stdlib.h>
#include "environment.h"
#include "qo_value.h"
#include "internal.h"

Environment *env_new(void) {
    return env_new_with_parent(NULL);
}

Environment *env_new_with_parent(Environment *parent) {
    Environment *env = xmalloc(sizeof(Environment));
    env->vars = NULL;
    env->parent = parent;
    return env;
}

Environment *env_root(Environment *env) {
    while (env && env->parent) env = env->parent;
    return env;
}

void env_free(Environment *env) {
    Variable *cur = env->vars;
    while (cur) {
        Variable *next = cur->next;
        qo_release(cur->value);
        free(cur);
        cur = next;
    }
    free(env);
}

void env_set(Environment *env, int64_t symbol_id, Qo value) {
    for (Variable *v = env->vars; v; v = v->next) {
        if (v->symbol_id == symbol_id) {
            qo_release(v->value);
            v->value = qo_retain(value);
            return;
        }
    }

    {
        Variable *var = xmalloc(sizeof(Variable));
        var->symbol_id = symbol_id;
        var->value = qo_retain(value);
        var->next = env->vars;
        env->vars = var;
    }
}

Qo env_get(Environment *env, int64_t symbol_id, int *found) {
    for (Environment *scope = env; scope; scope = scope->parent) {
        for (Variable *v = scope->vars; v; v = v->next) {
            if (v->symbol_id == symbol_id) {
                *found = 1;
                return qo_retain(v->value);
            }
        }
    }

    *found = 0;
    return NULL;
}
