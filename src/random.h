#pragma once

#include <stdint.h>
#include "evaluator.h"

void random_init(uint64_t seed);
uint64_t random_current_seed(void);
Qo eval_random(Qo count_val, Qo rhs, Environment *env);
