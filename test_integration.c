/* Cross-module integration: evolve â†’ attach values â†’ normalize â†’ schema â†’
 * roles â†’ intern â†’ score under firmwares. Proves the pieces compose. */
#include "ts_core.h"
#include "ts_core_ext.h"
#include "ts_layers.h"
#include "ts_intern.h"
#include "ts_norm.h"
#include "ts_roles.h"
#include "ts_boltzmann.h"
#include "ts_enum.h"
#include "ts_metric.h"
#include "device.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int tests = 0, passed = 0;
#define CHECK(x, msg) do { \
    tests++; if (!(x)) { printf("FAIL: %s\n", msg); return 1; } passed++; \
} while(0)

int main(void) {
    setbuf(stdout, NULL);
    printf("EdgeTG â€” CROSS-MODULE INTEGRATION\n\n");

    ts_ext_srand(2026);

    /* 1. Generate via Boltzmann */
    TSBoltzmannParams bp = ts_boltzmann_defaults();
    TSNode *tree = NULL;
    CHECK(ts_random_tree_boltzmann(6, 16, &bp, &tree) == TS_OK, "boltzmann gen");
    CHECK(ts_canonical_roundtrip(tree), "canonical after gen");
    size_t n = ts_count_nodes(tree);
    CHECK(n >= 1 && n <= 16, "node bounds");

    /* 2. Mutate a few times */
    for (int i = 0; i < 5; i++) {
        TSError e = ts_mut_grow(tree, 8);
        if (e == TS_OK) CHECK(ts_canonical_roundtrip(tree), "grow canonical");
        e = ts_mut_swap(tree);
        if (e == TS_OK) CHECK(ts_canonical_roundtrip(tree), "swap canonical");
    }
    n = ts_count_nodes(tree);

    /* 3. Attach synthetic values (Layer 1) */
    TSValue *vals = (TSValue *)calloc(n, sizeof(TSValue));
    CHECK(vals != NULL, "vals alloc");
    for (size_t i = 0; i < n; i++) {
        char buf[16];
        int len = snprintf(buf, sizeof(buf), "v%zu", i);
        uint8_t *d = (uint8_t *)malloc((size_t)len);
        CHECK(d != NULL, "val data");
        memcpy(d, buf, (size_t)len);
        vals[i].data = d;
        vals[i].len = (size_t)len;
    }
    TSPaired paired;
    CHECK(ts_pair_attach(tree, vals, n, &paired) == TS_OK, "pair attach");
    CHECK(strcmp(paired.topology, "") != 0, "topology non-empty");

    /* 4. Depth-selective normalize */
    bool mask[8] = {true, true, false, false, true, true, true, true};
    TSNode *normed = NULL;
    CHECK(ts_unordered_normalize_at_depths(tree, mask, 8, &normed) == TS_OK, "depth norm");
    CHECK(ts_canonical_roundtrip(normed), "normed canonical");

    /* 5. Schema validate (Layer 2) */
    TSSchema schema = { .max_depth = 16, .min_children = -1, .max_children = 5,
                        .rules = NULL, .rule_count = 0 };
    TSSchemaResult sr;
    CHECK(ts_schema_validate(normed, &schema, &sr), "schema pass");

    /* 6. Roles for every node */
    size_t nn = ts_count_nodes(normed);
    TSRole *roles = (TSRole *)calloc(nn, sizeof(TSRole));
    CHECK(roles != NULL, "roles alloc");
    for (size_t i = 0; i < nn; i++) {
        roles[i].preorder_index = i;
        roles[i].role_tag = (uint32_t)(i % 3);
    }
    TSRoleMap rmap;
    CHECK(ts_roles_create(roles, nn, 5, &rmap) == TS_OK, "roles create");
    CHECK(ts_roles_validate(normed, &rmap), "roles validate");
    uint8_t *rblob = NULL; size_t rlen = 0;
    CHECK(ts_roles_encode(&rmap, &rblob, &rlen) == TS_OK, "roles encode");

    /* 7. Intern */
    TSInternTable *tab = ts_intern_create(64);
    CHECK(tab != NULL, "intern create");
    int64_t id1 = ts_intern_add(tab, normed);
    int64_t id2 = ts_intern_add(tab, normed);
    CHECK(id1 >= 0 && id1 == id2, "intern dedup");
    CHECK(ts_intern_lookup(tab, normed) == id1, "intern lookup");
    const char *canon = ts_intern_get(tab, id1);
    CHECK(canon != NULL && canon[0] == '_', "intern get string");

    /* 8. Forest normalize of a small forest */
    TSNode *forest = NULL; size_t fn = 0;
    CHECK(ts_forest_normalize(normed, 1, &forest, &fn) == TS_OK && fn == 1, "forest norm");
    CHECK(ts_canonical_roundtrip(&forest[0]), "forest member canonical");

    /* 9. Score under all three firmwares */
    DeviceScore sa = device_evaluate(normed, FW_A_ORDERED_PIPELINE);
    DeviceScore sb = device_evaluate(normed, FW_B_TASK_TREE);
    DeviceScore sc = device_evaluate(normed, FW_C_MODULAR_ROBOT);
    CHECK(sa.nodes == nn, "firmware A nodes");
    (void)sb; (void)sc;

    /* 10. Metrics self-distance */
    CHECK(ts_edit_distance(normed, normed) == 0, "edit self");
    CHECK(ts_canonical_distance(normed, normed) == 0, "canon self");

    /* 11. Enumerate small n and intern each */
    uint64_t count3 = ts_count_trees(3);
    CHECK(count3 == 2, "catalan 3");
    for (uint64_t i = 0; i < count3; i++) {
        TSNode *et = NULL;
        CHECK(ts_tree_index(3, i, &et) == TS_OK, "unrank");
        CHECK(ts_count_nodes(et) == 3, "unrank size");
        int64_t eid = ts_intern_add(tab, et);
        CHECK(eid >= 0, "intern enumerated");
        ts_free_tree(et); free(et);
    }

    /* 12. Persistence store */
    DeviceStore store;
    device_store_init(&store);
    CHECK(device_store_insert(&store, normed, 0), "store insert");
    CHECK(!device_store_insert(&store, normed, 1), "store dedup");
    CHECK(device_store_count(&store) == 1, "store count");

    /* cleanup */
    device_store_free(&store);
    ts_free_forest(forest, fn);
    ts_intern_free(tab);
    free(rblob);
    ts_roles_free(&rmap);
    free(roles);
    ts_free_tree(normed); free(normed);
    ts_pair_free(&paired);
    for (size_t i = 0; i < n; i++) free((void *)vals[i].data);
    free(vals);
    ts_free_tree(tree); free(tree);

    printf("RESULT: %d/%d integration assertions passed.\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
