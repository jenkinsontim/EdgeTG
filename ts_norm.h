#ifndef TS_NORM_H
#define TS_NORM_H

#include "ts_core.h"
#include "ts_layers.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Canonical forest ordering (opt-in).
 * Top-level trees sorted by strcmp of their canonical encodings;
 * each tree is also recursively unordered-normalized. */
TSError ts_forest_normalize(const TSNode *forest, size_t count,
                            TSNode **out, size_t *out_count);

/* Paired forest normalize: permutes value blocks consistently with
 * the tree permutation (preorder blocks follow the sorted trees). */
TSError ts_forest_normalize_paired(const TSNode *forest, size_t count,
                                   const TSValue *values, size_t value_count,
                                   TSNode **out_forest, size_t *out_count,
                                   TSValue **out_values);

/* Depth-selective unordered normalization.
 * depth_mask[d] == true  => sort children at depth d (0 = root's children).
 * Depths beyond mask_len or false retain original order.
 * Existing ts_unordered_normalize ≡ all depths true. */
TSError ts_unordered_normalize_at_depths(const TSNode *tree,
                                         const bool *depth_mask,
                                         size_t mask_len,
                                         TSNode **out);

TSError ts_unordered_normalize_at_depths_paired(const TSNode *tree,
                                                const TSValue *values,
                                                size_t value_count,
                                                const bool *depth_mask,
                                                size_t mask_len,
                                                TSNode **out_tree,
                                                TSValue **out_values);

#endif
