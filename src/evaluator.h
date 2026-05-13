#pragma once

#include <stdint.h>
#include "environment.h"
#include "qo_value.h"

Qo eval_value(Qo tree, Environment *env);

void evaluator_request_error(void);
int evaluator_error_requested(void);
void evaluator_reset_error(void);

void evaluator_request_exit(int64_t code);
int evaluator_exit_requested(void);
int64_t evaluator_exit_code(void);
void evaluator_reset_exit(void);
