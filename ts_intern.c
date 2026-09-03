#include "ts_intern.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

typedef struct {
    char   *key;        /* canonical string, owned */
    TSNode *tree;       /* cached clone, owned */
    int64_t id;
    int     used;
} Slot;

struct TSInternTable {
    Slot   *slots;
    size_t  capacity;   /* power of two */
    size_t  count;
    size_t  id_counter;
    /* id -> slot index map for O(1) get by id */
    size_t *id_to_slot;
    size_t  id_cap;
};

static uint64_t hash_str(const char *s) {
    uint64_t h = 14695981039346656037ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

static size_t next_pow2(size_t n) {
    size_t p = 16;
    while (p < n) p *= 2;
    return p;
}

TSInternTable *ts_intern_create(size_t capacity_hint) {
    TSInternTable *t = (TSInternTable *)calloc(1, sizeof(*t));
    if (!t) return NULL;
    t->capacity = next_pow2(capacity_hint ? capacity_hint : 64);
    t->slots = (Slot *)calloc(t->capacity, sizeof(Slot));
    if (!t->slots) { free(t); return NULL; }
    t->id_cap = t->capacity;
    t->id_to_slot = (size_t *)calloc(t->id_cap, sizeof(size_t));
    if (!t->id_to_slot) { free(t->slots); free(t); return NULL; }
    return t;
}

static int resize(TSInternTable *t) {
    size_t nc = t->capacity * 2;
    Slot *ns = (Slot *)calloc(nc, sizeof(Slot));
    if (!ns) return -1;
    for (size_t i = 0; i < t->capacity; i++) {
        if (!t->slots[i].used) continue;
        uint64_t h = hash_str(t->slots[i].key);
        size_t j = h & (nc - 1);
        while (ns[j].used) j = (j + 1) & (nc - 1);
        ns[j] = t->slots[i];
        if (t->slots[i].id >= 0 && (size_t)t->slots[i].id < t->id_cap)
            t->id_to_slot[t->slots[i].id] = j;
    }
    free(t->slots);
    t->slots = ns;
    t->capacity = nc;
    return 0;
}

static int ensure_id_cap(TSInternTable *t, size_t need) {
    if (need <= t->id_cap) return 0;
    size_t nc = t->id_cap ? t->id_cap * 2 : 64;
    while (nc < need) nc *= 2;
    size_t *p = (size_t *)realloc(t->id_to_slot, nc * sizeof(size_t));
    if (!p) return -1;
    memset(p + t->id_cap, 0, (nc - t->id_cap) * sizeof(size_t));
    t->id_to_slot = p;
    t->id_cap = nc;
    return 0;
}

int64_t ts_intern_add(TSInternTable *t, const TSNode *tree) {
    if (!t || !tree) return -1;
    char *canon = NULL;
    if (ts_encode(tree, &canon, NULL) != TS_OK) return -1;

    if (t->count * 2 >= t->capacity) {
        if (resize(t) != 0) { free(canon); return -1; }
    }

    uint64_t h = hash_str(canon);
    size_t i = h & (t->capacity - 1);
    while (t->slots[i].used) {
        if (strcmp(t->slots[i].key, canon) == 0) {
            free(canon);
            return t->slots[i].id;   /* already present */
        }
        i = (i + 1) & (t->capacity - 1);
    }

    /* insert new */
    TSNode *clone = NULL;
    if (ts_clone(tree, &clone) != TS_OK) { free(canon); return -1; }
    int64_t id = (int64_t)t->id_counter++;
    if (ensure_id_cap(t, (size_t)id + 1) != 0) {
        ts_free_tree(clone); free(clone); free(canon); return -1;
    }
    t->slots[i].key = canon;
    t->slots[i].tree = clone;
    t->slots[i].id = id;
    t->slots[i].used = 1;
    t->id_to_slot[id] = i;
    t->count++;
    return id;
}

int64_t ts_intern_lookup(const TSInternTable *t, const TSNode *tree) {
    if (!t || !tree) return -1;
    char *canon = NULL;
    if (ts_encode(tree, &canon, NULL) != TS_OK) return -1;
    uint64_t h = hash_str(canon);
    size_t i = h & (t->capacity - 1);
    while (t->slots[i].used) {
        if (strcmp(t->slots[i].key, canon) == 0) {
            free(canon);
            return t->slots[i].id;
        }
        i = (i + 1) & (t->capacity - 1);
    }
    free(canon);
    return -1;
}

const char *ts_intern_get(const TSInternTable *t, int64_t id) {
    if (!t || id < 0 || (size_t)id >= t->id_counter) return NULL;
    size_t slot = t->id_to_slot[id];
    if (slot >= t->capacity || !t->slots[slot].used || t->slots[slot].id != id)
        return NULL;
    return t->slots[slot].key;
}

const TSNode *ts_intern_get_tree(const TSInternTable *t, int64_t id) {
    if (!t || id < 0 || (size_t)id >= t->id_counter) return NULL;
    size_t slot = t->id_to_slot[id];
    if (slot >= t->capacity || !t->slots[slot].used || t->slots[slot].id != id)
        return NULL;
    return t->slots[slot].tree;
}

size_t ts_intern_count(const TSInternTable *t) {
    return t ? t->count : 0;
}

void ts_intern_free(TSInternTable *t) {
    if (!t) return;
    for (size_t i = 0; i < t->capacity; i++) {
        if (t->slots[i].used) {
            free(t->slots[i].key);
            if (t->slots[i].tree) {
                ts_free_tree(t->slots[i].tree);
                free(t->slots[i].tree);
            }
        }
    }
    free(t->slots);
    free(t->id_to_slot);
    free(t);
}
