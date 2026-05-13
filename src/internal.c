#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "internal.h"
#include "lexer.h"

void *xmalloc(size_t size) {
    if (size == 0) {
        size = 1;
    }
    void *ptr = malloc(size);
    if (!ptr) {
        fprintf(stderr, "Error: out of memory\n");
        exit(1);
    }
    return ptr;
}

void *xrealloc(void *ptr, size_t size) {
    if (size == 0) {
        size = 1;
    }
    void *resized = realloc(ptr, size);
    if (!resized) {
        fprintf(stderr, "Error: out of memory\n");
        exit(1);
    }
    return resized;
}

char *xstrdup(const char *text) {
    char *copy = strdup(text);
    if (!copy) {
        fprintf(stderr, "Error: out of memory\n");
        exit(1);
    }
    return copy;
}



char *read_file_contents(const char *path) {
    FILE *file = fopen(path, "r");
    if (!file) {
        fprintf(stderr, "Error: cannot open file '%s'\n", path);
        return NULL;
    }

    if (fseek(file, 0, SEEK_END) != 0) {
        fclose(file);
        fprintf(stderr, "Error: failed to read file '%s'\n", path);
        return NULL;
    }

    long size = ftell(file);
    if (size < 0) {
        fclose(file);
        fprintf(stderr, "Error: failed to read file '%s'\n", path);
        return NULL;
    }

    rewind(file);

    char *contents = xmalloc((size_t)size + 1);
    size_t read_size = fread(contents, 1, (size_t)size, file);
    fclose(file);

    if (read_size != (size_t)size) {
        free(contents);
        fprintf(stderr, "Error: failed to read file '%s'\n", path);
        return NULL;
    }

    contents[size] = '\0';
    return contents;
}