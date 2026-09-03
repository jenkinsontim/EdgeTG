#ifndef TS_ENUM_H
#define TS_ENUM_H

#include "ts_core.h"
#include <stddef.h>
#include <stdint.h>

/* Number of ordered rooted trees with exactly n nodes (plane trees / Catalan).
 * C_{n-1} where C_k is the k-th Catalan number. Implemented via DP. */
uint64_t ts_count_trees(size_t n);

/* Call callback for every tree with exactly n nodes (canonical generation order).
 * Callback must not free the tree; the enumerator frees it after the call.
 * Returns number of trees visited, or (uint64_t)-1 on error. */
typedef void (*TSTreeCallback)(const TSNode *tree, void *ctx);
uint64_t ts_enumerate_trees(size_t n, TSTreeCallback cb, void *ctx);

/* Generate the index-th tree (0-based) with n nodes in the same order
 * as ts_enumerate_trees, without enumerating predecessors. */
TSError ts_tree_index(size_t n, uint64_t index, TSNode **out);
#endif
