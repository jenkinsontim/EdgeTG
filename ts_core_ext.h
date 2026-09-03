#ifndef TS_CORE_EXT_H
#define TS_CORE_EXT_H
#include "ts_core.h"
/* Distinct error code for lethal mutation collapse */
#define TS_EXT_ERR_EMPTY_ROOT 100
/* Seeded deterministic RNG (xorshift32) */
void ts_ext_srand(uint32_t seed);
uint32_t ts_ext_rand(void);
/* Random tree generation */
TSError ts_random_tree(size_t max_depth, size_t max_nodes, TSNode **out);
/* Structural mutations (operate on parsed trees, never strings) */
TSError ts_mut_grow(TSNode *tree, size_t max_depth);
TSError ts_mut_shrink(TSNode *tree);
TSError ts_mut_swap(TSNode *tree);
TSError ts_mut_retarget(TSNode *tree, size_t max_depth);
#endif
