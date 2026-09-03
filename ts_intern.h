#ifndef TS_INTERN_H
#define TS_INTERN_H

#include "ts_core.h"
#include <stddef.h>
#include <stdint.h>

/* External hash-cons table keyed by canonical _/\ string.
 * Wire and store formats remain pure strings; this table is never serialized. */

typedef struct TSInternTable TSInternTable;

/* Create table with approximate capacity hint (grows as needed). */
TSInternTable *ts_intern_create(size_t capacity_hint);

/* Intern a tree: encode canonically, look up or insert, return stable ID (>=0).
 * Returns -1 on OOM or invalid argument. */
int64_t ts_intern_add(TSInternTable *t, const TSNode *tree);

/* Lookup by tree: return ID if previously interned, else -1. */
int64_t ts_intern_lookup(const TSInternTable *t, const TSNode *tree);

/* Lookup by ID: return owned? No — returns pointer to internal canonical string
 * (valid until table is freed or the entry is overwritten; do not free it).
 * Returns NULL if id is out of range. */
const char *ts_intern_get(const TSInternTable *t, int64_t id);

/* Optional: return a cached clone of the tree for this ID, or NULL. */
const TSNode *ts_intern_get_tree(const TSInternTable *t, int64_t id);

size_t ts_intern_count(const TSInternTable *t);

void ts_intern_free(TSInternTable *t);

#endif
