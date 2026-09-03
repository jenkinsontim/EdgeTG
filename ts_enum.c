#include "ts_enum.h"
#include <stdlib.h>
#include <string.h>

/* Catalan DP: C[0]=1; number of ordered rooted trees with n nodes = C[n-1]. */
static uint64_t *catalan_up_to(size_t k) {
    uint64_t *C = (uint64_t *)calloc(k + 1, sizeof(uint64_t));
    if (!C) return NULL;
    C[0] = 1;
    for (size_t n = 1; n <= k; n++) {
        for (size_t i = 0; i < n; i++) {
            uint64_t a = C[i], b = C[n - 1 - i];
            if (a && b > UINT64_MAX / a) { free(C); return NULL; }
            C[n] += a * b;
        }
    }
    return C;
}

uint64_t ts_count_trees(size_t n) {
    if (n == 0) return 0;
    if (n == 1) return 1;
    uint64_t *C = catalan_up_to(n - 1);
    if (!C) return 0;
    uint64_t r = C[n - 1];
    free(C);
    return r;
}

/* Unrank the index-th plane tree with n nodes.
 *
 * Ordering: trees are ordered by the size of the *first child*, then by the
 * recursive shape of that first child, then by the shape of the remaining
 * forest treated as a plane tree under a virtual root (standard recursive
 * decomposition of plane trees / Catalan structure).
 *
 * A plane tree of size n is a root plus an ordered sequence of plane trees
 * whose sizes sum to n-1.  We unrank by finding the unique first-child size
 * s such that the cumulative product of Catalan numbers covers `index`.
 */
static TSError unrank_rec(size_t n, uint64_t index, TSNode *out, const uint64_t *C) {
    memset(out, 0, sizeof(*out));
    if (n == 0) return TS_ERR_INVALID_ARG;
    if (n == 1) {
        return (index == 0) ? TS_OK : TS_ERR_INVALID_ARG;
    }

    /* Find first-child size s (1..n-1) and the split of the index. */
    uint64_t acc = 0;
    size_t s = 0;
    uint64_t local = 0;
    for (s = 1; s <= n - 1; s++) {
        /* Number of trees with first child of size s and remaining forest
         * of size n-1-s, where the remaining forest is itself encoded as
         * the children after the first (i.e. a sequence).
         * Count = C[s-1] * C[(n-1-s)]   but only if remaining is ONE tree.
         * For a general sequence we need the generating function.
         *
         * Practical correct approach used by most combinatorial libraries:
         * decompose as (first subtree of size s) + (tree of size n-s) where
         * the second tree's children become the rest of the sequence.
         * This is the standard "first child + rest" decomposition and
         * enumerates each plane tree exactly once.
         * Count for fixed s: C[s-1] * C[n-s-1]  (rest has n-s nodes including
         * a virtual root that is later flattened).
         */
        uint64_t left = C[s - 1];
        uint64_t right = C[n - s - 1];
        uint64_t block;
        if (left == 0 || right == 0) block = 0;
        else if (right > UINT64_MAX / left) return TS_ERR_INVALID_ARG;
        else block = left * right;

        if (acc + block > index) {
            local = index - acc;
            break;
        }
        acc += block;
    }
    if (s > n - 1) return TS_ERR_INVALID_ARG;

    uint64_t left_count = C[s - 1];
    uint64_t right_index = local / (left_count ? left_count : 1);
    uint64_t left_index  = left_count ? (local % left_count) : 0;

    /* Build first child of size s */
    TSNode first;
    TSError e = unrank_rec(s, left_index, &first, C);
    if (e != TS_OK) return e;

    /* Build "rest" tree of size n-s; its children become siblings of first */
    TSNode rest;
    e = unrank_rec(n - s, right_index, &rest, C);
    if (e != TS_OK) { ts_free_tree(&first); return e; }

    /* Assemble: children = [first] ++ rest.children */
    size_t total_children = 1 + rest.child_count;
    out->children = (TSNode *)calloc(total_children, sizeof(TSNode));
    if (!out->children) {
        ts_free_tree(&first); ts_free_tree(&rest); return TS_ERR_OOM;
    }
    out->children[0] = first;
    for (size_t i = 0; i < rest.child_count; i++)
        out->children[1 + i] = rest.children[i];
    out->child_count = total_children;
    free(rest.children); /* ownership moved */
    return TS_OK;
}

TSError ts_tree_index(size_t n, uint64_t index, TSNode **out) {
    if (!out || n == 0) return TS_ERR_INVALID_ARG;
    *out = NULL;
    uint64_t total = ts_count_trees(n);
    if (index >= total) return TS_ERR_INVALID_ARG;

    uint64_t *C = catalan_up_to(n > 0 ? n - 1 : 0);
    if (!C) return TS_ERR_OOM;

    TSNode *t = (TSNode *)calloc(1, sizeof(TSNode));
    if (!t) { free(C); return TS_ERR_OOM; }
    TSError e = unrank_rec(n, index, t, C);
    free(C);
    if (e != TS_OK) { ts_free_tree(t); free(t); return e; }
    *out = t;
    return TS_OK;
}

/* Enumerate by walking every index. */
uint64_t ts_enumerate_trees(size_t n, TSTreeCallback cb, void *ctx) {
    if (n == 0 || n > 14) return 0; /* safety: C_13 ~ 742900 */
    uint64_t total = ts_count_trees(n);
    for (uint64_t i = 0; i < total; i++) {
        TSNode *t = NULL;
        if (ts_tree_index(n, i, &t) != TS_OK) return (uint64_t)-1;
        if (cb) cb(t, ctx);
        ts_free_tree(t);
        free(t);
    }
    return total;
}
