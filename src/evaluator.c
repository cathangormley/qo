#include "evaluator.h"

static int g_eval_error = 0;
static int g_eval_exit = 0;
static int64_t g_eval_exit_code = 0;

void evaluator_request_error(void) {
    g_eval_error = 1;
}

int evaluator_error_requested(void) {
    return g_eval_error;
}

void evaluator_reset_error(void) {
    g_eval_error = 0;
}

void evaluator_request_exit(int64_t code) {
    g_eval_exit = 1;
    g_eval_exit_code = code;
}

int evaluator_exit_requested(void) {
    return g_eval_exit;
}

int64_t evaluator_exit_code(void) {
    return g_eval_exit_code;
}

void evaluator_reset_exit(void) {
    g_eval_exit = 0;
    g_eval_exit_code = 0;
}
