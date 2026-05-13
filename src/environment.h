#pragma once

#include <stdint.h>
#include "qo_value.h"

typedef struct Variable {
    int64_t symbol_id;
    Qo value;
    struct Variable *next;
} Variable;

typedef struct Environment {
    Variable *vars;
    struct Environment *parent;
} Environment;

Environment *env_new(void);
Environment *env_new_with_parent(Environment *parent);
Environment *env_root(Environment *env);
void env_free(Environment *env);
void env_set(Environment *env, int64_t symbol_id, Qo value);
Qo env_get(Environment *env, int64_t symbol_id, int *found);
