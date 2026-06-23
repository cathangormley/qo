#pragma once

#include "qo_value.h"

typedef struct Environment {
    Qo dict;
    struct Environment *parent;
} Environment;

Environment *env_new(void);
Environment *env_new_with_parent(Environment *parent);
Environment *env_root(Environment *env);
void env_free(Environment *env);
void env_set(Environment *env, Qo key_sym, Qo value);
Qo env_get(Environment *env, Qo key_sym, int *found);
