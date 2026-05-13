#pragma once

#include <stddef.h>

void *xmalloc(size_t size);
void *xrealloc(void *ptr, size_t size);
char *xstrdup(const char *text);
char *read_file_contents(const char *path);