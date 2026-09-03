#ifndef TS_BOLTZMANN_H
#define TS_BOLTZMANN_H

#include "ts_core.h"
#include "ts_core_ext.h"
#include <stddef.h>

typedef struct {
    double leaf_probability;  /* 0..1, probability of stopping (no children) */
    size_t max_children;      /* upper bound on children per node (>=1) */
    double depth_decay;       /* multiply branching chance by this each level deeper (0..1) */
} TSBoltzmannParams;

/* Default params that approximate the original ts_random_tree behaviour. */
TSBoltzmannParams ts_boltzmann_defaults(void);

TSError ts_random_tree_boltzmann(size_t max_depth, size_t max_nodes,
                                 const TSBoltzmannParams *params,
                                 TSNode **out);
#endif
