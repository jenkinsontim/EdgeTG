#ifndef TS_CORE_H
#define TS_CORE_H
#include <stddef.h>
#include <stdint.h>

typedef struct TSNode TSNode;
struct TSNode { TSNode *children; size_t child_count; };

typedef enum {
    TS_OK = 0,
    TS_ERR_EXPECTED_NODE,
    TS_ERR_UNCLOSED_CHILDREN,
    TS_ERR_EMPTY_CHILDREN,
    TS_ERR_TRAILING,
    TS_ERR_DEPTH,
    TS_ERR_OOM,
    TS_ERR_INVALID_ARG
} TSError;

TSError ts_parse(const char *s, size_t max_depth, TSNode **out);
TSError ts_parse_forest(const char *s, size_t max_depth, TSNode **out, size_t *out_count);
void ts_free_tree(TSNode *tree);
void ts_free_forest(TSNode *forest, size_t count);
TSError ts_clone(const TSNode *src, TSNode **out);
TSError ts_encode(const TSNode *tree, char **out, size_t *len);
TSError ts_encode_forest(const TSNode *forest, size_t count, char **out, size_t *len);
size_t ts_count_nodes(const TSNode *tree);
size_t ts_depth(const TSNode *tree);

#endif
