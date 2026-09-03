#ifndef TS_LAYERS_H
#define TS_LAYERS_H

#include "ts_core.h"
#include <stdbool.h>
#include <stdint.h>

/* Layer 1: topology and values are separate artifacts.
 * Value i belongs to preorder node i: root first, then each node's children
 * recursively from left to right. The topology string is never modified. */
typedef struct {
    const uint8_t *data;
    size_t len;
} TSValue;

typedef struct {
    char *topology;
    size_t node_count;
    TSValue *values;
} TSPaired;

TSError ts_pair_attach(const TSNode *tree, const TSValue *values, size_t value_count, TSPaired *out);
TSError ts_pair_detach(const TSNode *tree, const TSPaired *paired, TSValue *out_values, size_t value_count);
TSError ts_pair_encode(const TSNode *tree, const TSValue *values, size_t value_count,
                       char **topology_out, TSValue **values_out, size_t *count_out);
TSError ts_pair_decode(const char *topology, const TSValue *values, size_t value_count,
                       size_t max_depth, TSNode **tree_out, TSPaired *paired_out);
void ts_pair_free(TSPaired *p);

/* Optional separate serialization of the value array.
 * This is a binary side artifact; it is not accepted by ts_parse(). */
TSError ts_values_encode(const TSValue *values, size_t count, uint8_t **out, size_t *out_len);
TSError ts_values_decode(const uint8_t *buf, size_t len, TSValue **out, size_t *out_count);
void ts_values_free(TSValue *values, size_t count);

/* Layer 2: schema / arity constraints. -1 means unconstrained. */
typedef struct {
    size_t preorder_index;
    int min_children;
    int max_children;
} TSArityRule;

typedef struct {
    size_t max_depth;
    int min_children;
    int max_children;
    const TSArityRule *rules;
    size_t rule_count;
} TSSchema;

typedef enum {
    TS_SCHEMA_OK = 0,
    TS_SCHEMA_MAX_DEPTH,
    TS_SCHEMA_MIN_CHILDREN,
    TS_SCHEMA_MAX_CHILDREN,
    TS_SCHEMA_ROLE_MIN_CHILDREN,
    TS_SCHEMA_ROLE_MAX_CHILDREN
} TSSchemaCode;

typedef struct {
    TSSchemaCode code;
    size_t preorder_index;
    size_t depth;
    size_t actual_children;
    int limit;
} TSSchemaResult;

bool ts_schema_validate(const TSNode *tree, const TSSchema *schema, TSSchemaResult *result);

/* Layer 3: forest operators.
 * Pairing: equal-length forests become roots with A_i then B_i as ordered children. */
TSError ts_forest_concat(const TSNode *a, size_t an, const TSNode *b, size_t bn,
                         TSNode **out, size_t *out_n);
TSError ts_forest_graft(const TSNode *forest, size_t forest_n, size_t dst_root,
                        const size_t *path, size_t path_len, const TSNode *source,
                        size_t max_depth, TSNode **out_forest);
TSError ts_forest_pair(const TSNode *a, size_t an, const TSNode *b, size_t bn,
                       size_t max_depth, TSNode **out, size_t *out_n);

/* Layer 4: explicit opt-in unordered normalization. */
TSError ts_unordered_normalize(const TSNode *tree, TSNode **out);
TSError ts_unordered_normalize_paired(const TSNode *tree, const TSValue *values, size_t value_count,
                                      TSNode **out_tree, TSValue **out_values);

/* Mechanical canonicity invariant for topology-producing results. */
bool ts_canonical_roundtrip(const TSNode *tree);

#endif
