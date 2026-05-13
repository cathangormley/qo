#pragma once

#include "qo_value.h"

Qo qo_symbol_intern(const char *text);
void qo_symbol_intern_remove(Qo symbol);
