#include "ts_boltzmann.h"
#include <stdlib.h>
#include <string.h>

TSBoltzmannParams ts_boltzmann_defaults(void) {
    TSBoltzmannParams p = { 0.33, 3, 0.9 };
    return p;
}

static double urand(void) {
    return (double)(ts_ext_rand() & 0xFFFFFF) / (double)0x1000000;
}

static TSError build(TSNode *n, size_t depth, size_t max_depth,
                     size_t *nodes_left, const TSBoltzmannParams *p) {
    n->child_count = 0;
    n->children = NULL;
    if (depth >= max_depth || *nodes_left == 0) return TS_OK;

    double stop = p->leaf_probability;
    for (size_t d = 1; d < depth; d++) stop = 1.0 - (1.0 - stop) * p->depth_decay;
    if (stop > 1.0) stop = 1.0;
    if (urand() < stop && depth > 0) return TS_OK;

    size_t kmax = p->max_children;
    if (kmax > *nodes_left) kmax = *nodes_left;
    if (kmax == 0) return TS_OK;
    size_t k = 1 + (size_t)(urand() * (double)kmax);
    if (k > kmax) k = kmax;
    if (k == 0) return TS_OK;

    n->children = (TSNode *)calloc(k, sizeof(TSNode));
    if (!n->children) return TS_ERR_OOM;
    n->child_count = k;
    *nodes_left -= k;
    for (size_t i = 0; i < k; i++) {
        TSError e = build(&n->children[i], depth + 1, max_depth, nodes_left, p);
        if (e != TS_OK) return e;
    }
    return TS_OK;
}

TSError ts_random_tree_boltzmann(size_t max_depth, size_t max_nodes,
                                 const TSBoltzmannParams *params,
                                 TSNode **out) {
    if (!out || max_depth == 0 || max_nodes == 0) return TS_ERR_INVALID_ARG;
    TSBoltzmannParams def = ts_boltzmann_defaults();
    if (!params) params = &def;
    TSNode *root = (TSNode *)calloc(1, sizeof(TSNode));
    if (!root) return TS_ERR_OOM;
    size_t left = max_nodes - 1;
    TSError e = build(root, 1, max_depth, &left, params);
    if (e != TS_OK) { ts_free_tree(root); free(root); return e; }
    *out = root;
    return TS_OK;
}
