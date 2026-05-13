#pragma once

#include "qo_value.h"

Qo qo_symbol_intern(const char *text);
Qo qo_symbol_by_id(int64_t id);
void qo_symbol_intern_remove(Qo symbol);
