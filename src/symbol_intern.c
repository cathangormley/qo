#include <stdlib.h>
#include <string.h>
#include "evaluator_internal.h"
#include "internal.h"
#include "symbol_intern.h"

typedef struct SymbolInternEntry {
    int64_t id;
    int64_t len;
    char *name;
    Qo live_symbol;
    struct SymbolInternEntry *next;
} SymbolInternEntry;

#define SYMBOL_INTERN_BUCKETS 256

static SymbolInternEntry *symbol_intern_table[SYMBOL_INTERN_BUCKETS];
static SymbolInternEntry **symbol_entries_by_id;
static int64_t symbol_entry_count;
static int64_t symbol_entry_capacity;

static size_t symbol_hash_text(const char *text, int64_t len) {
    size_t hash = (size_t)2166136261u;
    for (int64_t i = 0; i < len; i++) {
        hash ^= (unsigned char)text[i];
        hash *= (size_t)16777619u;
    }
    return hash;
}

static size_t symbol_bucket_index(const char *text, int64_t len) {
    return symbol_hash_text(text, len) % SYMBOL_INTERN_BUCKETS;
}

static SymbolInternEntry *symbol_entry_lookup(const char *text, int64_t len) {
    size_t bucket = symbol_bucket_index(text, len);
    for (SymbolInternEntry *entry = symbol_intern_table[bucket]; entry != NULL; entry = entry->next) {
        if (entry->len == len && memcmp(entry->name, text, (size_t)len) == 0) {
            return entry;
        }
    }
    return NULL;
}

static void ensure_symbol_entry_capacity(void) {
    if (symbol_entry_count < symbol_entry_capacity) return;
    symbol_entry_capacity = (symbol_entry_capacity == 0) ? 64 : symbol_entry_capacity * 2;
    symbol_entries_by_id = xrealloc(symbol_entries_by_id, (size_t)symbol_entry_capacity * sizeof(SymbolInternEntry *));
}

static Qo make_symbol_object(int64_t id) {
    Qo symbol = qo_alloc_raw(sizeof(int64_t));
    symbol->type_tag = QO_SYMBOL;
    symbol->attribute = 0;
    QO_SET_SYMBOL_ID(symbol, id);
    return symbol;
}

static SymbolInternEntry *symbol_intern_insert(const char *text, int64_t len) {
    size_t bucket = symbol_bucket_index(text, len);
    SymbolInternEntry *entry = xmalloc(sizeof(SymbolInternEntry));
    entry->id = symbol_entry_count;
    entry->len = len;
    entry->name = xstrdup(text);
    entry->live_symbol = NULL;
    entry->next = symbol_intern_table[bucket];
    symbol_intern_table[bucket] = entry;
    ensure_symbol_entry_capacity();
    symbol_entries_by_id[symbol_entry_count++] = entry;
    return entry;
}

Qo qo_symbol_intern(const char *text) {
    int64_t len = (int64_t)strlen(text);
    SymbolInternEntry *entry = symbol_entry_lookup(text, len);

    if (entry != NULL) {
        if (entry->live_symbol != NULL) {
            return qo_retain(entry->live_symbol);
        }
        entry->live_symbol = make_symbol_object(entry->id);
        return entry->live_symbol;
    }

    entry = symbol_intern_insert(text, len);
    entry->live_symbol = make_symbol_object(entry->id);
    return entry->live_symbol;
}

Qo qo_symbol_by_id(int64_t id) {
    if (id < 0 || id >= symbol_entry_count) return NULL;
    SymbolInternEntry *entry = symbol_entries_by_id[id];
    if (entry->live_symbol != NULL) return qo_retain(entry->live_symbol);
    entry->live_symbol = make_symbol_object(id);
    return entry->live_symbol;
}

const char *qo_symbol_name(Qo symbol) {
    int64_t id;
    if (symbol == NULL || qo_type(symbol) != QO_SYMBOL) return NULL;
    id = qo_symbol_id(symbol);
    if (id < 0 || id >= symbol_entry_count) return NULL;
    return symbol_entries_by_id[id]->name;
}

void qo_symbol_intern_remove(Qo symbol) {
    int64_t id;
    SymbolInternEntry *entry;
    if (symbol == NULL || qo_type(symbol) != QO_SYMBOL) return;
    id = qo_symbol_id(symbol);
    if (id < 0 || id >= symbol_entry_count) return;
    entry = symbol_entries_by_id[id];
    if (entry != NULL && entry->live_symbol == symbol) {
        entry->live_symbol = NULL;
    }
}