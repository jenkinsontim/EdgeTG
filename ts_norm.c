#include "ts_norm.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

/* ---- helpers ---- */

static int cmp_tree_ptrs(const void *A, const void *B) {
    const TSNode *a = *(const TSNode *const *)A;
    const TSNode *b = *(const TSNode *const *)B;
    char *sa = NULL, *sb = NULL;
    if (ts_encode(a, &sa, NULL) != TS_OK || ts_encode(b, &sb, NULL) != TS_OK) {
        free(sa); free(sb); return 0;
    }
    int c = strcmp(sa, sb);
    free(sa); free(sb);
    return c;
}

static int cmp_nodes_inline(const void *A, const void *B) {
    const TSNode *a = (const TSNode *)A;
    const TSNode *b = (const TSNode *)B;
    char *sa = NULL, *sb = NULL;
    if (ts_encode(a, &sa, NULL) != TS_OK || ts_encode(b, &sb, NULL) != TS_OK) {
        free(sa); free(sb); return 0;
    }
    int c = strcmp(sa, sb);
    free(sa); free(sb);
    return c;
}

/* Recursively normalize children only at selected depths.
 * cur_depth is the depth of *this* node's children (0 for root). */
static TSError norm_at_rec(const TSNode *src, TSNode *dst,
                           const bool *mask, size_t mask_len,
                           size_t cur_depth) {
    dst->child_count = src->child_count;
    dst->children = NULL;
    if (src->child_count == 0) return TS_OK;

    dst->children = (TSNode *)calloc(src->child_count, sizeof(TSNode));
    if (!dst->children) return TS_ERR_OOM;

    for (size_t i = 0; i < src->child_count; i++) {
        TSError e = norm_at_rec(&src->children[i], &dst->children[i],
                                mask, mask_len, cur_depth + 1);
        if (e != TS_OK) {
            for (size_t j = 0; j < i; j++) ts_free_tree(&dst->children[j]);
            free(dst->children); dst->children = NULL; dst->child_count = 0;
            return e;
        }
    }

    bool sort_here = (cur_depth < mask_len) && mask[cur_depth];
    if (sort_here && dst->child_count > 1)
        qsort(dst->children, dst->child_count, sizeof(TSNode), cmp_nodes_inline);
    return TS_OK;
}

TSError ts_unordered_normalize_at_depths(const TSNode *tree,
                                         const bool *depth_mask,
                                         size_t mask_len,
                                         TSNode **out) {
    if (!tree || !out) return TS_ERR_INVALID_ARG;
    *out = NULL;
    TSNode *n = (TSNode *)calloc(1, sizeof(TSNode));
    if (!n) return TS_ERR_OOM;
    TSError e = norm_at_rec(tree, n, depth_mask, mask_len, 0);
    if (e != TS_OK) { free(n); return e; }
    *out = n;
    return TS_OK;
}

/* Full normalize = all depths true, sized to the tree's actual depth
 * (never a fixed 64-level guess, so arbitrarily deep trees are normalized). */
static TSError full_norm(const TSNode *tree, TSNode **out) {
    size_t depth = ts_depth(tree);
    bool *mask = (bool *)malloc(depth > 0 ? depth : 1);
    if (!mask) return TS_ERR_OOM;
    for (size_t i = 0; i < depth; i++) mask[i] = true;
    TSError e = ts_unordered_normalize_at_depths(tree, mask, depth, out);
    free(mask);
    return e;
}

/* ---- forest normalize ---- */

TSError ts_forest_normalize(const TSNode *forest, size_t count,
                            TSNode **out, size_t *out_count) {
    if (!out || !out_count) return TS_ERR_INVALID_ARG;
    *out = NULL; *out_count = 0;
    if (count == 0) return TS_OK;
    if (!forest) return TS_ERR_INVALID_ARG;

    /* First normalize each tree fully */
    TSNode **tmp = (TSNode **)calloc(count, sizeof(TSNode *));
    if (!tmp) return TS_ERR_OOM;
    for (size_t i = 0; i < count; i++) {
        TSError e = full_norm(&forest[i], &tmp[i]);
        if (e != TS_OK) {
            for (size_t j = 0; j < i; j++) {
                ts_free_tree(tmp[j]); free(tmp[j]);
            }
            free(tmp); return e;
        }
    }

    /* Sort the array of tree pointers by canonical string */
    qsort(tmp, count, sizeof(TSNode *), cmp_tree_ptrs);

    /* Flatten into contiguous array */
    TSNode *f = (TSNode *)calloc(count, sizeof(TSNode));
    if (!f) {
        for (size_t i = 0; i < count; i++) {
            ts_free_tree(tmp[i]); free(tmp[i]);
        }
        free(tmp); return TS_ERR_OOM;
    }
    for (size_t i = 0; i < count; i++) {
        f[i] = *tmp[i];
        free(tmp[i]);   /* free the wrapper, keep children */
    }
    free(tmp);
    *out = f;
    *out_count = count;
    return TS_OK;
}

/* Paired forest normalize — values are concatenated preorder blocks of each tree.
 * After sorting trees we reorder the value blocks accordingly. */
TSError ts_forest_normalize_paired(const TSNode *forest, size_t count,
                                   const TSValue *values, size_t value_count,
                                   TSNode **out_forest, size_t *out_count,
                                   TSValue **out_values) {
    if (!out_forest || !out_count || !out_values) return TS_ERR_INVALID_ARG;
    *out_forest = NULL; *out_count = 0; *out_values = NULL;
    if (count == 0) {
        if (value_count != 0) return TS_ERR_INVALID_ARG;
        return TS_OK;
    }
    if (!forest || !values) return TS_ERR_INVALID_ARG;

    /* Verify total node count matches value_count */
    size_t total = 0;
    for (size_t i = 0; i < count; i++) total += ts_count_nodes(&forest[i]);
    if (total != value_count) return TS_ERR_INVALID_ARG;

    /* Normalize each tree + its value block */
    typedef struct {
        TSNode *tree;
        TSValue *vals;
        size_t n;
        char *key;
    } Item;
    Item *items = (Item *)calloc(count, sizeof(Item));
    if (!items) return TS_ERR_OOM;

    size_t off = 0;
    for (size_t i = 0; i < count; i++) {
        items[i].n = ts_count_nodes(&forest[i]);
        TSError e = full_norm(&forest[i], &items[i].tree);
        if (e != TS_OK) goto fail;
        /* For simplicity we keep original value order within each tree;
         * a full paired recursive normalize would re-permute inside, but
         * forest-level only reorders whole trees. */
        items[i].vals = (TSValue *)malloc(items[i].n * sizeof(TSValue));
        if (!items[i].vals) { e = TS_ERR_OOM; goto fail; }
        memcpy(items[i].vals, values + off, items[i].n * sizeof(TSValue));
        off += items[i].n;
        e = ts_encode(items[i].tree, &items[i].key, NULL);
        if (e != TS_OK) goto fail;
        continue;
    fail:
        for (size_t j = 0; j <= i; j++) {
            if (items[j].tree) { ts_free_tree(items[j].tree); free(items[j].tree); }
            free(items[j].vals);
            free(items[j].key);
        }
        free(items);
        return e != TS_OK ? e : TS_ERR_OOM;
    }

    /* Sort items by key */
    for (size_t i = 0; i < count; i++) {
        for (size_t j = i + 1; j < count; j++) {
            if (strcmp(items[i].key, items[j].key) > 0) {
                Item tmp = items[i]; items[i] = items[j]; items[j] = tmp;
            }
        }
    }

    TSNode *f = (TSNode *)calloc(count, sizeof(TSNode));
    TSValue *v = (TSValue *)malloc(value_count * sizeof(TSValue));
    if (!f || !v) {
        free(f); free(v);
        for (size_t i = 0; i < count; i++) {
            ts_free_tree(items[i].tree); free(items[i].tree);
            free(items[i].vals); free(items[i].key);
        }
        free(items);
        return TS_ERR_OOM;
    }
    off = 0;
    for (size_t i = 0; i < count; i++) {
        f[i] = *items[i].tree;
        free(items[i].tree);
        memcpy(v + off, items[i].vals, items[i].n * sizeof(TSValue));
        off += items[i].n;
        free(items[i].vals);
        free(items[i].key);
    }
    free(items);
    *out_forest = f;
    *out_count = count;
    *out_values = v;
    return TS_OK;
}

/* Paired depth-selective: only sort at selected depths; values follow. */
typedef struct {
    TSNode tree;
    TSValue *values;
    size_t count;
    char *key;
} PChild;

static void free_pchild(PChild *p) {
    if (!p) return;
    ts_free_tree(&p->tree);
    free(p->values);
    free(p->key);
}

static int cmp_pchild(const void *A, const void *B) {
    return strcmp(((const PChild *)A)->key, ((const PChild *)B)->key);
}

static TSError norm_pair_at_rec(const TSNode *src, const TSValue *vals,
                                TSNode *out, TSValue *outvals,
                                const bool *mask, size_t mask_len,
                                size_t cur_depth) {
    out->child_count = src->child_count;
    out->children = NULL;
    outvals[0] = vals[0];
    if (!src->child_count) return TS_OK;

    out->children = (TSNode *)calloc(src->child_count, sizeof(TSNode));
    if (!out->children) return TS_ERR_OOM;

    PChild *kids = (PChild *)calloc(src->child_count, sizeof(PChild));
    if (!kids) { free(out->children); out->children = NULL; return TS_ERR_OOM; }

    size_t off = 1;
    for (size_t i = 0; i < src->child_count; i++) {
        kids[i].count = ts_count_nodes(&src->children[i]);
        TSValue *invals = (TSValue *)malloc(kids[i].count * sizeof(TSValue));
        kids[i].values = (TSValue *)malloc(kids[i].count * sizeof(TSValue));
        if (!invals || !kids[i].values) {
            free(invals); free(kids[i].values);
            for (size_t j = 0; j < i; j++) free_pchild(&kids[j]);
            free(kids); ts_free_tree(out); return TS_ERR_OOM;
        }
        memcpy(invals, vals + off, kids[i].count * sizeof(TSValue));
        TSError e = norm_pair_at_rec(&src->children[i], invals,
                                     &kids[i].tree, kids[i].values,
                                     mask, mask_len, cur_depth + 1);
        free(invals);
        if (e != TS_OK) {
            for (size_t j = 0; j <= i; j++) free_pchild(&kids[j]);
            free(kids); ts_free_tree(out); return e;
        }
        off += kids[i].count;
        e = ts_encode(&kids[i].tree, &kids[i].key, NULL);
        if (e != TS_OK) {
            for (size_t j = 0; j <= i; j++) free_pchild(&kids[j]);
            free(kids); ts_free_tree(out); return e;
        }
    }

    bool sort_here = (cur_depth < mask_len) && mask[cur_depth];
    if (sort_here && src->child_count > 1)
        qsort(kids, src->child_count, sizeof(PChild), cmp_pchild);

    off = 1;
    for (size_t i = 0; i < src->child_count; i++) {
        out->children[i] = kids[i].tree;
        kids[i].tree.children = NULL;
        kids[i].tree.child_count = 0;
        memcpy(outvals + off, kids[i].values, kids[i].count * sizeof(TSValue));
        off += kids[i].count;
        free(kids[i].values);
        free(kids[i].key);
    }
    free(kids);
    return TS_OK;
}

TSError ts_unordered_normalize_at_depths_paired(const TSNode *tree,
                                                const TSValue *values,
                                                size_t value_count,
                                                const bool *depth_mask,
                                                size_t mask_len,
                                                TSNode **out_tree,
                                                TSValue **out_values) {
    if (!tree || !values || !out_tree || !out_values) return TS_ERR_INVALID_ARG;
    *out_tree = NULL; *out_values = NULL;
    size_t n = ts_count_nodes(tree);
    if (value_count != n) return TS_ERR_INVALID_ARG;
    TSNode *t = (TSNode *)calloc(1, sizeof(TSNode));
    TSValue *v = (TSValue *)malloc(n * sizeof(TSValue));
    if (!t || !v) { free(t); free(v); return TS_ERR_OOM; }
    TSError e = norm_pair_at_rec(tree, values, t, v, depth_mask, mask_len, 0);
    if (e != TS_OK) { ts_free_tree(t); free(t); free(v); return e; }
    *out_tree = t;
    *out_values = v;
    return TS_OK;
}
