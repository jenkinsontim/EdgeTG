#include "ts_core_ext.h"
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static uint32_t rng_state = 1;
void ts_ext_srand(uint32_t seed) { rng_state = seed ? seed : 1; }
uint32_t ts_ext_rand(void) { uint32_t x = rng_state; x ^= x << 13; x ^= x >> 17; x ^= x << 5; rng_state = x; return x; }

static TSError build_random(TSNode* n, size_t depth, size_t max_depth, size_t* nodes_left) {
    if (depth >= max_depth || *nodes_left == 0 || (ts_ext_rand() % 3 == 0 && depth > 0)) { n->child_count = 0; n->children = NULL; return TS_OK; }
    size_t k = 1 + (ts_ext_rand() % 3); if (k > *nodes_left) k = *nodes_left;
    if (k == 0) { n->child_count = 0; n->children = NULL; return TS_OK; }
    n->children = calloc(k, sizeof(TSNode)); if (!n->children) return TS_ERR_OOM;
    n->child_count = k; *nodes_left -= k;
    for (size_t i = 0; i < k; i++) { TSError e = build_random(&n->children[i], depth + 1, max_depth, nodes_left); if (e != TS_OK) return e; }
    return TS_OK;
}
TSError ts_random_tree(size_t max_depth, size_t max_nodes, TSNode **out) {
    if (!out || max_depth == 0 || max_nodes == 0) return TS_ERR_INVALID_ARG;
    TSNode* root = calloc(1, sizeof(TSNode)); if (!root) return TS_ERR_OOM;
    size_t left = max_nodes - 1;
    TSError e = build_random(root, 1, max_depth, &left);
    if (e != TS_OK) { ts_free_tree(root); free(root); return e; }
    *out = root; return TS_OK;
}

/* FIX 3: get_path/get_path_rec used to write into a caller-supplied
 * fixed-size stack array `path[64]` with NO bounds check -- any tree
 * deeper than 64 levels caused a stack-buffer-overflow (confirmed via
 * AddressSanitizer). Path length can never exceed the tree's actual
 * depth, so we size the buffer to that, dynamically, every time. */
static void get_path_rec(TSNode* tree, size_t idx, size_t* path, size_t* path_len) {
    if (idx == 0) { return; }
    size_t i = 1;
    for (size_t c = 0; c < tree->child_count; c++) {
        size_t sub = ts_count_nodes(&tree->children[c]);
        if (idx < i + sub) { path[(*path_len)++] = c; get_path_rec(&tree->children[c], idx - i, path, path_len); return; }
        i += sub;
    }
}
/* Returns a heap-allocated path buffer (caller must free) sized safely
 * to the tree's actual depth -- never a fixed guess. */
static size_t* get_path_alloc(TSNode* tree, size_t idx, size_t* path_len) {
    size_t bound = ts_depth(tree) + 1;
    size_t* path = calloc(bound, sizeof(size_t));
    if (!path) return NULL;
    *path_len = 0;
    get_path_rec(tree, idx, path, path_len);
    return path;
}

TSError ts_mut_grow(TSNode *tree, size_t max_depth) {
    if (!tree) { return TS_ERR_INVALID_ARG; }
    size_t total = ts_count_nodes(tree);
    size_t idx = ts_ext_rand() % total;
    size_t path_len = 0;
    size_t* path = get_path_alloc(tree, idx, &path_len);
    if (!path) { return TS_ERR_OOM; }
    if (path_len + 1 >= max_depth) { free(path); return TS_ERR_DEPTH; }
    TSNode* n = tree; for(size_t i = 0; i < path_len; i++) n = &n->children[path[i]];
    free(path);
    TSNode* nc = realloc(n->children, (n->child_count + 1) * sizeof(TSNode)); if (!nc) return TS_ERR_OOM;
    n->children = nc; memset(&n->children[n->child_count], 0, sizeof(TSNode)); n->child_count++; return TS_OK;
}
static bool cascade_shrink(TSNode* node, const size_t* path, size_t path_len, size_t depth) {
    if (depth == path_len - 1) {
        size_t c_idx = path[depth]; ts_free_tree(&node->children[c_idx]);
        for (size_t i = c_idx; i < node->child_count - 1; i++) node->children[i] = node->children[i+1];
        node->child_count--; return (node->child_count == 0);
    }
    size_t c_idx = path[depth]; bool child_empty = cascade_shrink(&node->children[c_idx], path, path_len, depth + 1);
    if (child_empty) { ts_free_tree(&node->children[c_idx]); for (size_t i = c_idx; i < node->child_count - 1; i++) node->children[i] = node->children[i+1]; node->child_count--; return (node->child_count == 0); }
    return false;
}
TSError ts_mut_shrink(TSNode *tree) {
    if (!tree) { return TS_ERR_INVALID_ARG; }
    size_t total = ts_count_nodes(tree);
    if (total <= 1) { return TS_EXT_ERR_EMPTY_ROOT; }
    size_t idx = 1 + (ts_ext_rand() % (total - 1));
    size_t path_len = 0;
    size_t* path = get_path_alloc(tree, idx, &path_len);
    if (!path) { return TS_ERR_OOM; }
    TSNode* clone = NULL; TSError e = ts_clone(tree, &clone);
    if (e != TS_OK) { free(path); return e; }
    bool emptied = cascade_shrink(clone, path, path_len, 0);
    free(path);
    if (emptied) { ts_free_tree(clone); free(clone); return TS_EXT_ERR_EMPTY_ROOT; }
    ts_free_tree(tree); *tree = *clone; free(clone); return TS_OK;
}
TSError ts_mut_swap(TSNode *tree) {
    if (!tree) { return TS_ERR_INVALID_ARG; }
    size_t total = ts_count_nodes(tree);
    for (int tries = 0; tries < 100; tries++) {
        size_t idx = ts_ext_rand() % total;
        size_t path_len = 0;
        size_t* path = get_path_alloc(tree, idx, &path_len);
        if (!path) { return TS_ERR_OOM; }
        TSNode* n = tree; for(size_t i = 0; i < path_len; i++) n = &n->children[path[i]];
        free(path);
        if (n->child_count >= 2) {
            size_t a = ts_ext_rand() % n->child_count, b = ts_ext_rand() % n->child_count;
            while (b == a) b = ts_ext_rand() % n->child_count;
            TSNode tmp = n->children[a]; n->children[a] = n->children[b]; n->children[b] = tmp; return TS_OK;
        }
    }
    return TS_ERR_INVALID_ARG;
}
TSError ts_mut_retarget(TSNode *tree, size_t max_depth) {
    if (!tree) { return TS_ERR_INVALID_ARG; }
    size_t total = ts_count_nodes(tree);
    if (total <= 1) { return TS_ERR_INVALID_ARG; }
    size_t idx = 1 + (ts_ext_rand() % (total - 1));
    size_t path_len = 0;
    size_t* path = get_path_alloc(tree, idx, &path_len);
    if (!path) { return TS_ERR_OOM; }
    if (path_len + 1 >= max_depth) { free(path); return TS_ERR_DEPTH; }
    TSNode* parent = tree; for(size_t i = 0; i < path_len - 1; i++) parent = &parent->children[path[i]];
    size_t c_idx = path[path_len - 1];
    free(path);
    TSNode* new_sub = NULL; TSError e = ts_random_tree(max_depth - (path_len + 1), 5, &new_sub); if (e != TS_OK) return e;
    ts_free_tree(&parent->children[c_idx]); parent->children[c_idx] = *new_sub; free(new_sub); return TS_OK;
}
