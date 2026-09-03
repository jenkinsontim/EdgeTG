#include "ts_core.h"
#include "ts_core_ext.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
static int tests = 0, passed = 0;
#define CHECK(x, msg) do { tests++; if (!(x)) { printf("FAIL: %s\n", msg); return 1; } passed++; } while(0)
static bool is_canonical(const TSNode* tree) {
    char *s1 = NULL, *s2 = NULL;
    size_t l1 = 0, l2 = 0;
    if (ts_encode(tree, &s1, &l1) != TS_OK) return false;
    TSNode *p = NULL;
    bool ok = (ts_parse(s1, 64, &p) == TS_OK);
    if (ok) ok = (ts_encode(p, &s2, &l2) == TS_OK && l1 == l2 && memcmp(s1, s2, l1) == 0);
    free(s1); free(s2);
    if (p) { ts_free_tree(p); free(p); }
    return ok;
}
int main(void) {
    setbuf(stdout, NULL);
    printf("TS_CORE EXTENSION — seeded RNG + structural mutations\n");
    /* 1. RNG Determinism */
    ts_ext_srand(42); uint32_t a = ts_ext_rand();
    ts_ext_srand(42); uint32_t b = ts_ext_rand();
    CHECK(a == b && a != 0, "RNG determinism");
    printf("1. Seeded RNG determinism            PASS\n");
    /* 2. Random Tree */
    TSNode *rt = NULL;
    ts_ext_srand(123);
    CHECK(ts_random_tree(5, 20, &rt) == TS_OK && rt != NULL, "random tree gen");
    CHECK(ts_count_nodes(rt) <= 20 && ts_depth(rt) <= 5, "random tree bounds");
    CHECK(is_canonical(rt), "random tree canonical");
    ts_free_tree(rt); free(rt);
    printf("2. Random tree budget & depth        PASS\n");
    /* 3. Grow */
    TSNode *g = NULL; ts_parse("_", 64, &g);
    size_t old_count = ts_count_nodes(g);
    CHECK(ts_mut_grow(g, 10) == TS_OK, "grow ok");
    CHECK(ts_count_nodes(g) == old_count + 1, "grow count");
    CHECK(is_canonical(g), "grow canonical");
    ts_free_tree(g); free(g);
    printf("3. Grow                              PASS\n");
    /* 4. Shrink + DeleteEmptiedTree */
    TSNode *s1 = NULL; ts_parse("_", 64, &s1);
    CHECK(ts_mut_shrink(s1) == TS_EXT_ERR_EMPTY_ROOT, "shrink empty root fails");
    
    TSNode *s2 = NULL; ts_parse("_/_\\", 64, &s2);
    CHECK(ts_mut_shrink(s2) == TS_EXT_ERR_EMPTY_ROOT, "shrink cascade to empty root fails");
    
    TSNode *s3 = NULL; ts_parse("_/___\\", 64, &s3);
    CHECK(ts_mut_shrink(s3) == TS_OK, "shrink ok");
    CHECK(ts_count_nodes(s3) == 3, "shrink count");
    CHECK(is_canonical(s3), "shrink canonical");
    ts_free_tree(s1); free(s1); ts_free_tree(s2); free(s2); ts_free_tree(s3); free(s3);
    printf("4. Shrink (+ DeleteEmptiedTree)      PASS\n");
    /* 5. Swap */
    TSNode *sw = NULL; ts_parse("_", 64, &sw);
    CHECK(ts_mut_swap(sw) == TS_ERR_INVALID_ARG, "swap leaf fails");
    ts_free_tree(sw); free(sw);
    ts_parse("_/___\\", 64, &sw);
    CHECK(ts_mut_swap(sw) == TS_OK, "swap ok");
    CHECK(is_canonical(sw), "swap canonical");
    ts_free_tree(sw); free(sw);
    printf("5. Swap                              PASS\n");
    /* 6. Retarget */
    TSNode *r = NULL; ts_parse("_", 64, &r);
    CHECK(ts_mut_retarget(r, 10) == TS_ERR_INVALID_ARG, "retarget root fails");
    ts_free_tree(r); free(r);
    ts_parse("_/_\\", 64, &r);
    CHECK(ts_mut_retarget(r, 10) == TS_OK, "retarget ok");
    CHECK(is_canonical(r), "retarget canonical");
    ts_free_tree(r); free(r);
    printf("6. Retarget                          PASS\n");
    /* 7. Fuzz Invariants */
    printf("7. Mutation invariants (500 runs)    ");
    ts_ext_srand(2026);
    for (int i = 0; i < 500; i++) {
        TSNode *t = NULL;
        CHECK(ts_random_tree(6, 30, &t) == TS_OK, "fuzz gen");
        size_t c1 = ts_count_nodes(t);
        
        TSError e1 = ts_mut_grow(t, 8);
        if (e1 == TS_OK) CHECK(is_canonical(t) && ts_count_nodes(t) == c1 + 1, "fuzz grow");
        
        size_t before_swap = ts_count_nodes(t);
        TSError e2 = ts_mut_swap(t);
        if (e2 == TS_OK) {
            size_t after_swap = ts_count_nodes(t);
            CHECK(is_canonical(t) && before_swap == after_swap,
                  "fuzz swap preserves node count");
        }
        
        TSError e3 = ts_mut_shrink(t);
        if (e3 == TS_OK) CHECK(is_canonical(t), "fuzz shrink");
        else CHECK(e3 == TS_EXT_ERR_EMPTY_ROOT, "fuzz shrink error");
        
        TSError e4 = ts_mut_retarget(t, 8);
        if (e4 == TS_OK) CHECK(is_canonical(t), "fuzz retarget");
        
        ts_free_tree(t); free(t);
    }
    printf("PASS\n");
    printf("\nRESULT: %d/%d extension tests passed.\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
