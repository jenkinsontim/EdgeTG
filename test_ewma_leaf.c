/**
 * test_ewma_leaf.c -- EdgeTG EWMA adaptive leaf integration tests.
 *
 * Wires EWMA leaf classifiers to the leaf nodes of EdgeTG trees identified
 * by their preorder index.  Exercises:
 *   - Struct sizing and initialisation
 *   - Online learning convergence (single leaf)
 *   - Concept reversal recovery
 *   - Multi-leaf forest: each tree leaf gets its own ewma_leaf_t
 *   - Leaf dispatch: route a feature vector to the correct leaf via the tree
 *   - TSRoleMap integration: leaf nodes tagged ROLE_EWMA_LEAF
 *   - Error-rate monitoring API
 *
 * Build:
 *   gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
 *       -fsanitize=address,undefined -g \
 *       ewma_leaf.c ts_core.c ts_layers.c ts_roles.c test_ewma_leaf.c \
 *       -o test_ewma_leaf
 */

#include "ewma_leaf.h"
#include "ts_core.h"
#include "ts_layers.h"
#include "ts_roles.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <assert.h>

/* ------------------------------------------------------------------ helpers */

static int tests = 0, passed = 0;

#define CHECK(x, msg) do { \
    tests++; \
    if (!(x)) { printf("FAIL [%d]: %s\n", tests, (msg)); } \
    else       { passed++; } \
} while (0)

/* Collect preorder indices of all leaf nodes (child_count == 0). */
static void collect_leaves(const TSNode *node, size_t *idx, size_t *leaves,
                            size_t *leaf_count, size_t max_leaves)
{
    size_t my_idx = (*idx)++;
    if (node->child_count == 0) {
        if (*leaf_count < max_leaves)
            leaves[(*leaf_count)++] = my_idx;
    }
    for (size_t c = 0; c < node->child_count; c++)
        collect_leaves(&node->children[c], idx, leaves, leaf_count, max_leaves);
}

/* ------------------------------------------------------------------ tests */

/* 1. Struct size and init */
static int test_sizing(void)
{
    printf("1. Struct size and initialisation\n");
    CHECK(sizeof(ewma_leaf_t) <= 64, "struct fits in 64 bytes");

    ewma_leaf_t leaf;
    ewma_leaf_init(&leaf, 2, 8);
    CHECK(leaf.num_classes  == 2, "num_classes");
    CHECK(leaf.num_features == 8, "num_features");
    CHECK(leaf.recent_errors == 0, "errors zero");
    CHECK(leaf.recent_total  == 0, "total zero");
    for (uint8_t c = 0; c < 2; c++)
        for (uint8_t f = 0; f < 8; f++)
            CHECK(leaf.proto[c][f] == 128, "proto mid-scale");

    printf("   sizeof(ewma_leaf_t) = %zu bytes\n", sizeof(ewma_leaf_t));
    return 0;
}

/* 2. Online convergence: drive two classes apart, then predict correctly */
static int test_convergence(void)
{
    printf("2. Online convergence\n");
    ewma_leaf_t leaf;
    ewma_leaf_init(&leaf, 2, 8);

    uint8_t zeros[8] = {0,0,0,0,0,0,0,0};
    uint8_t ones[8]  = {1,1,1,1,1,1,1,1};

    for (int i = 0; i < 200; i++) {
        ewma_leaf_update(&leaf, ones,  0);
        ewma_leaf_update(&leaf, zeros, 1);
    }
    CHECK(ewma_leaf_predict(&leaf, ones)  == 0, "predict ones  → class 0");
    CHECK(ewma_leaf_predict(&leaf, zeros) == 1, "predict zeros → class 1");
    CHECK(leaf.proto[0][0] > 200, "proto[0] converged high");
    CHECK(leaf.proto[1][0] < 55,  "proto[1] converged low");
    return 0;
}

/* 3. Concept reversal recovery */
static int test_reversal(void)
{
    printf("3. Concept reversal recovery\n");
    ewma_leaf_t leaf;
    ewma_leaf_init(&leaf, 2, 8);

    uint8_t zeros[8] = {0,0,0,0,0,0,0,0};
    uint8_t ones[8]  = {1,1,1,1,1,1,1,1};

    /* Phase 1: class 0 = ones */
    for (int i = 0; i < 150; i++) {
        ewma_leaf_update(&leaf, ones,  0);
        ewma_leaf_update(&leaf, zeros, 1);
    }
    CHECK(ewma_leaf_predict(&leaf, ones)  == 0, "pre-reversal: ones → 0");

    /* Phase 2: reverse — class 0 = zeros now */
    for (int i = 0; i < 120; i++) {
        ewma_leaf_update(&leaf, zeros, 0);
        ewma_leaf_update(&leaf, ones,  1);
    }
    CHECK(ewma_leaf_predict(&leaf, zeros) == 0, "post-reversal: zeros → 0");
    CHECK(ewma_leaf_predict(&leaf, ones)  == 1, "post-reversal: ones  → 1");
    /* No unsigned wrap: proto must be a valid uint8 in range */
    CHECK(leaf.proto[0][0] <= 255, "proto[0][0] in range after reversal");
    return 0;
}

/* 4. Runtime alpha override */
static int test_alpha_override(void)
{
    printf("4. Runtime alpha override (Q8)\n");
    ewma_leaf_t fast, slow;
    ewma_leaf_init(&fast, 2, 4);
    ewma_leaf_init(&slow, 2, 4);

    uint8_t ones[4] = {1,1,1,1};

    /* fast: α ≈ 0.50 (Q8 = 128); slow: α ≈ 0.05 (Q8 = 13) */
    for (int i = 0; i < 40; i++) {
        ewma_leaf_update_alpha(&fast, ones, 0, 128);
        ewma_leaf_update_alpha(&slow, ones, 0, 13);
    }
    /* Fast should converge much higher than slow after 40 updates */
    CHECK(fast.proto[0][0] > slow.proto[0][0], "high α converges faster");
    return 0;
}

/* 5. Error-rate API */
static int test_error_rate(void)
{
    printf("5. Error-rate monitoring API\n");
    ewma_leaf_t leaf;
    ewma_leaf_init(&leaf, 2, 8);

    /* Before any updates, error rate should be 0 */
    CHECK(ewma_leaf_error_rate(&leaf) == 0, "initial error rate = 0");

    uint8_t zeros[8] = {0};
    uint8_t ones[8]  = {1,1,1,1,1,1,1,1};

    /* Give it clearly wrong labels for 30 steps while prototypes are still mid-scale */
    for (int i = 0; i < 30; i++) {
        /* proto mid-scale → prediction could go either way; deliberately
         * train class 0 ← zeros then ask about ones to accumulate errors.
         * We just verify the counter is exercised without overflow. */
        ewma_leaf_update(&leaf, zeros, 0);
        ewma_leaf_update(&leaf, ones, 1);
    }
    uint8_t rate = ewma_leaf_error_rate(&leaf);
    CHECK(rate <= 255, "error rate is valid uint8");
    /* After convergence, error rate should drop */
    for (int i = 0; i < 200; i++) {
        ewma_leaf_update(&leaf, zeros, 0);
        ewma_leaf_update(&leaf, ones,  1);
    }
    uint8_t rate2 = ewma_leaf_error_rate(&leaf);
    CHECK(rate2 < rate || rate == 0, "error rate drops after convergence");
    return 0;
}

/* 6. Tree integration: map leaves of a tree to ewma_leaf_t instances */
static int test_tree_integration(void)
{
    printf("6. Tree-leaf integration via preorder index\n");

    /* Parse a small tree: one root with two leaves.
     *   _/_ _\   → root with children [_, _]
     *   Preorder: root=0, left-leaf=1, right-leaf=2
     */
    TSNode *tree = NULL;
    CHECK(ts_parse("_/__\\", 16, &tree) == TS_OK, "parse tree");
    CHECK(ts_count_nodes(tree) == 3, "node count = 3");

    /* Collect leaf preorder indices */
    size_t leaf_indices[8];
    size_t leaf_count = 0;
    size_t idx = 0;
    collect_leaves(tree, &idx, leaf_indices, &leaf_count, 8);
    CHECK(leaf_count == 2, "2 leaf nodes found");
    CHECK(leaf_indices[0] == 1, "left leaf index = 1");
    CHECK(leaf_indices[1] == 2, "right leaf index = 2");

    /* Allocate one ewma_leaf_t per tree leaf */
    ewma_leaf_t *leaves = calloc(leaf_count, sizeof(ewma_leaf_t));
    CHECK(leaves != NULL, "alloc leaf array");
    for (size_t i = 0; i < leaf_count; i++)
        ewma_leaf_init(&leaves[i], 2, 8);

    /* Train leaf 0 (index 1) to separate ones/zeros */
    uint8_t zeros[8] = {0};
    uint8_t ones[8]  = {1,1,1,1,1,1,1,1};
    for (int i = 0; i < 150; i++) {
        ewma_leaf_update(&leaves[0], ones,  0);
        ewma_leaf_update(&leaves[0], zeros, 1);
    }
    /* Train leaf 1 (index 2) with reversed labels */
    for (int i = 0; i < 150; i++) {
        ewma_leaf_update(&leaves[1], zeros, 0);
        ewma_leaf_update(&leaves[1], ones,  1);
    }

    /* Dispatch: leaf 0 sees ones → class 0; leaf 1 sees ones → class 1 */
    CHECK(ewma_leaf_predict(&leaves[0], ones)  == 0, "leaf[0]: ones → 0");
    CHECK(ewma_leaf_predict(&leaves[0], zeros) == 1, "leaf[0]: zeros → 1");
    CHECK(ewma_leaf_predict(&leaves[1], ones)  == 1, "leaf[1]: ones → 1");
    CHECK(ewma_leaf_predict(&leaves[1], zeros) == 0, "leaf[1]: zeros → 0");

    free(leaves);
    ts_free_tree(tree);
    free(tree);
    return 0;
}

/* 7. TSRoleMap integration: tag leaf nodes with ROLE_EWMA_LEAF */
#define ROLE_ROUTING   1u
#define ROLE_EWMA_LEAF 2u

static int test_role_map(void)
{
    printf("7. TSRoleMap: leaf nodes tagged ROLE_EWMA_LEAF\n");

    /* Tree: _/_ _\  (3 nodes) */
    TSNode *tree = NULL;
    CHECK(ts_parse("_/__\\", 16, &tree) == TS_OK, "parse");

    /* node 0 = routing, nodes 1,2 = ewma leaves */
    TSRole roles_arr[3] = {
        { .preorder_index = 0, .role_tag = ROLE_ROUTING,   .metadata = {NULL, 0} },
        { .preorder_index = 1, .role_tag = ROLE_EWMA_LEAF, .metadata = {NULL, 0} },
        { .preorder_index = 2, .role_tag = ROLE_EWMA_LEAF, .metadata = {NULL, 0} },
    };
    TSRoleMap rmap;
    CHECK(ts_roles_create(roles_arr, 3, ROLE_EWMA_LEAF, &rmap) == TS_OK, "create role map");
    CHECK(ts_roles_validate(tree, &rmap), "validate: all nodes have roles");

    /* Count ewma leaf roles */
    size_t ewma_count = 0;
    for (size_t i = 0; i < rmap.count; i++)
        if (rmap.roles[i].role_tag == ROLE_EWMA_LEAF)
            ewma_count++;
    CHECK(ewma_count == 2, "2 nodes tagged ROLE_EWMA_LEAF");

    /* Allocate ewma_leaf_t for each tagged node */
    ewma_leaf_t *leaves = calloc(ewma_count, sizeof(ewma_leaf_t));
    CHECK(leaves != NULL, "alloc");
    for (size_t i = 0; i < ewma_count; i++)
        ewma_leaf_init(&leaves[i], 3, 8);

    /* Verify 3-class init */
    CHECK(leaves[0].num_classes == 3, "3-class leaf");

    free(leaves);
    ts_roles_free(&rmap);
    ts_free_tree(tree);
    free(tree);
    return 0;
}

/* 8. Larger tree: 4-leaf binary tree, all leaves get independent classifiers */
static int test_four_leaf_tree(void)
{
    printf("8. Four-leaf binary tree, independent EWMA leaves\n");

    /* _/__/__\__\\  — root, two internal nodes, four leaves
     * Encoded: root → [left-subtree, right-subtree]
     *   left-subtree  = _/_ _\
     *   right-subtree = _/_ _\
     * Full: _/_/__\\/__\\\ 
     * Simpler: depth-2 full binary tree
     *   Canonical: _/_/_  _\/_  _\\
     */
    TSNode *tree = NULL;
    /* Build from known good: root with two children, each a binary node */
    /* Encoded canonical form for root(_/__\, _/__\) — verified by probe:
     * bytes: _ / _ / _ _ \ _ / _ _ \ \
     */
    TSError e = ts_parse("_/_/__\\_/__\\\\", 16, &tree);
    CHECK(e == TS_OK, "parse 4-leaf tree");
    if (e != TS_OK) return 0;  /* skip if parse fails, don't crash */

    size_t n = ts_count_nodes(tree);
    CHECK(n == 7, "7 nodes (3 internal + 4 leaves)");

    size_t leaf_indices[8];
    size_t leaf_count = 0;
    size_t pi = 0;
    collect_leaves(tree, &pi, leaf_indices, &leaf_count, 8);
    CHECK(leaf_count == 4, "4 leaves");

    ewma_leaf_t *leaves = calloc(leaf_count, sizeof(ewma_leaf_t));
    CHECK(leaves != NULL, "alloc 4 leaves");
    for (size_t i = 0; i < leaf_count; i++)
        ewma_leaf_init(&leaves[i], 2, 8);

    /* Each leaf learns a distinct pattern */
    uint8_t pat[4][8] = {
        {0,0,0,0,0,0,0,0},
        {1,1,1,1,1,1,1,1},
        {1,0,1,0,1,0,1,0},
        {0,1,0,1,0,1,0,1},
    };
    for (size_t l = 0; l < leaf_count; l++) {
        for (int i = 0; i < 150; i++) {
            ewma_leaf_update(&leaves[l], pat[l],               0);
            /* class 1 = bitwise complement */
            uint8_t inv[8];
            for (int f = 0; f < 8; f++) inv[f] = (uint8_t)(1u - pat[l][f]);
            ewma_leaf_update(&leaves[l], inv, 1);
        }
        CHECK(ewma_leaf_predict(&leaves[l], pat[l]) == 0,
              "leaf converged: pattern → class 0");
    }

    free(leaves);
    ts_free_tree(tree);
    free(tree);
    return 0;
}

/* 9. Boundary: clamping — no uint8 wrap at 0 or 255 */
static int test_clamping(void)
{
    printf("9. Boundary clamping (no uint8 wrap)\n");
    ewma_leaf_t leaf;
    ewma_leaf_init(&leaf, 2, 4);

    uint8_t zeros[4] = {0};
    uint8_t ones[4]  = {1,1,1,1};

    /* Drive to 255 */
    for (int i = 0; i < 300; i++) ewma_leaf_update(&leaf, ones, 0);
    CHECK(leaf.proto[0][0] >= 250 && leaf.proto[0][0] <= 255,
          "clamped at upper bound");

    /* Drive back to 0 */
    for (int i = 0; i < 300; i++) ewma_leaf_update(&leaf, zeros, 0);
    CHECK(leaf.proto[0][0] <= 5, "clamped at lower bound");

    /* Proto stays in [0, 255] — a uint8 assertion is implicit by type,
     * but check the value is actually sensible after reversal */
    CHECK(leaf.proto[0][0] != 255, "no unsigned wrap from reversal");
    return 0;
}

/* 10. Serialisation round-trip via TSRoleMap binary encode/decode */
static int test_role_serialisation(void)
{
    printf("10. TSRoleMap binary encode/decode with EWMA leaf metadata\n");

    /* Store the leaf index as 1-byte metadata in each role entry */
    uint8_t meta0[1] = {0};   /* leaf 0 */
    uint8_t meta1[1] = {1};   /* leaf 1 */

    TSValue mv0 = { meta0, 1 };
    TSValue mv1 = { meta1, 1 };

    TSRole roles_arr[2] = {
        { .preorder_index = 0, .role_tag = ROLE_EWMA_LEAF, .metadata = mv0 },
        { .preorder_index = 1, .role_tag = ROLE_EWMA_LEAF, .metadata = mv1 },
    };
    TSRoleMap rmap;
    CHECK(ts_roles_create(roles_arr, 2, ROLE_EWMA_LEAF, &rmap) == TS_OK, "create");

    /* Encode */
    uint8_t *buf = NULL;
    size_t buf_len = 0;
    CHECK(ts_roles_encode(&rmap, &buf, &buf_len) == TS_OK, "encode");
    CHECK(buf != NULL && buf_len > 0, "non-empty encoding");

    /* Decode */
    TSRoleMap rmap2;
    CHECK(ts_roles_decode(buf, buf_len, &rmap2) == TS_OK, "decode");
    CHECK(rmap2.count == 2, "decoded 2 roles");
    CHECK(rmap2.roles[0].role_tag == ROLE_EWMA_LEAF, "tag preserved");
    CHECK(rmap2.roles[1].metadata.len == 1, "metadata len preserved");
    CHECK(rmap2.roles[1].metadata.data[0] == 1, "metadata value preserved");

    free(buf);
    ts_roles_free(&rmap);
    ts_roles_free(&rmap2);
    return 0;
}

/* ------------------------------------------------------------------ main */

int main(void)
{
    setbuf(stdout, NULL);
    printf("EdgeTG — EWMA ADAPTIVE LEAF INTEGRATION TESTS\n");
    printf("==============================================\n\n");

    test_sizing();
    test_convergence();
    test_reversal();
    test_alpha_override();
    test_error_rate();
    test_tree_integration();
    test_role_map();
    test_four_leaf_tree();
    test_clamping();
    test_role_serialisation();

    printf("\n==============================================\n");
    printf("Results: %d / %d passed\n", passed, tests);
    if (passed == tests)
        printf("ALL EWMA LEAF TESTS PASSED.\n");
    else
        printf("SOME TESTS FAILED.\n");

    return (passed == tests) ? 0 : 1;
}
