#include "ts_core.h"
#include "ts_core_ext.h"
#include "ts_layers.h"
#include "ts_intern.h"
#include "ts_norm.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int tests = 0, passed = 0;
#define CHECK(x, msg) do { tests++; if (!(x)) { printf("FAIL: %s\n", msg); return 1; } passed++; } while(0)

static bool is_canonical(const TSNode *t) {
    return ts_canonical_roundtrip(t);
}

int main(void) {
    setbuf(stdout, NULL);
    printf("EdgeTG â€” PRIORITY 1 MODULES\n\n");

    /* ========== Intern table ========== */
    printf("1. Hash-cons interning table\n");
    TSInternTable *tab = ts_intern_create(32);
    CHECK(tab != NULL, "create");
    CHECK(ts_intern_count(tab) == 0, "empty count");

    TSNode *a = NULL, *b = NULL, *c = NULL;
    CHECK(ts_parse("_", 64, &a) == TS_OK, "parse leaf");
    CHECK(ts_parse("_/__\\", 64, &b) == TS_OK, "parse binary");
    CHECK(ts_parse("_", 64, &c) == TS_OK, "parse leaf2");

    int64_t id_a = ts_intern_add(tab, a);
    int64_t id_b = ts_intern_add(tab, b);
    int64_t id_c = ts_intern_add(tab, c);   /* same shape as a */
    CHECK(id_a >= 0 && id_b >= 0 && id_c >= 0, "add ok");
    CHECK(id_a == id_c, "identical shapes share ID");
    CHECK(id_a != id_b, "different shapes different IDs");
    CHECK(ts_intern_count(tab) == 2, "count after inserts");

    CHECK(ts_intern_lookup(tab, a) == id_a, "lookup by tree");
    CHECK(ts_intern_lookup(tab, b) == id_b, "lookup b");
    const char *sa = ts_intern_get(tab, id_a);
    const char *sb = ts_intern_get(tab, id_b);
    CHECK(sa && strcmp(sa, "_") == 0, "get string a");
    CHECK(sb && strcmp(sb, "_/__\\") == 0, "get string b");
    CHECK(ts_intern_get_tree(tab, id_a) != NULL, "get tree");
    CHECK(ts_intern_get(tab, 999) == NULL, "bad id");

    /* Collision / many inserts */
    ts_ext_srand(42);
    for (int i = 0; i < 200; i++) {
        TSNode *r = NULL;
        CHECK(ts_random_tree(5, 12, &r) == TS_OK, "rand");
        int64_t id = ts_intern_add(tab, r);
        CHECK(id >= 0, "intern random");
        CHECK(ts_intern_lookup(tab, r) == id, "lookup random");
        ts_free_tree(r); free(r);
    }
    CHECK(ts_intern_count(tab) > 2, "grew");
    ts_intern_free(tab);
    ts_free_tree(a); free(a); ts_free_tree(b); free(b); ts_free_tree(c); free(c);
    printf("   PASS\n\n");

    /* ========== Forest normalize ========== */
    printf("2. Canonical forest ordering\n");
    TSNode *f1 = NULL, *f2 = NULL, *f3 = NULL;
    CHECK(ts_parse("_/_\\", 64, &f1) == TS_OK, "f1");
    CHECK(ts_parse("_", 64, &f2) == TS_OK, "f2");
    CHECK(ts_parse("_/___\\", 64, &f3) == TS_OK, "f3");
    TSNode forest_in[3] = {*f1, *f2, *f3};
    /* free wrappers later */

    TSNode *fn = NULL; size_t fn_n = 0;
    CHECK(ts_forest_normalize(forest_in, 3, &fn, &fn_n) == TS_OK && fn_n == 3, "forest norm");
    char *e0 = NULL, *e1 = NULL, *e2 = NULL;
    ts_encode(&fn[0], &e0, NULL);
    ts_encode(&fn[1], &e1, NULL);
    ts_encode(&fn[2], &e2, NULL);
    CHECK(strcmp(e0, e1) <= 0 && strcmp(e1, e2) <= 0, "sorted order");
    CHECK(is_canonical(&fn[0]) && is_canonical(&fn[1]) && is_canonical(&fn[2]), "each canonical");

    /* Idempotence */
    TSNode *fn2 = NULL; size_t fn2_n = 0;
    CHECK(ts_forest_normalize(fn, fn_n, &fn2, &fn2_n) == TS_OK, "norm again");
    char *f0 = NULL, *g0 = NULL;
    ts_encode_forest(fn, fn_n, &f0, NULL);
    ts_encode_forest(fn2, fn2_n, &g0, NULL);
    CHECK(strcmp(f0, g0) == 0, "idempotent");
    free(e0); free(e1); free(e2); free(f0); free(g0);
    ts_free_forest(fn, fn_n); ts_free_forest(fn2, fn2_n);
    ts_free_tree(f1); free(f1); ts_free_tree(f2); free(f2); ts_free_tree(f3); free(f3);
    printf("   PASS\n\n");

    /* ========== Depth-selective normalize ========== */
    printf("3. Depth-selective normalization\n");
    TSNode *u = NULL;
    CHECK(ts_parse("_/_/_\\_\\", 64, &u) == TS_OK, "input");
    /* Sort only depth 0 (root children) */
    bool mask0[] = { true, false, false };
    TSNode *n0 = NULL;
    CHECK(ts_unordered_normalize_at_depths(u, mask0, 3, &n0) == TS_OK, "depth0");
    CHECK(is_canonical(n0), "canonical depth0");

    /* Sort all depths â€” should match full unordered */
    bool mask_all[] = { true, true, true, true };
    TSNode *nall = NULL, *nfull = NULL;
    CHECK(ts_unordered_normalize_at_depths(u, mask_all, 4, &nall) == TS_OK, "all depths");
    CHECK(ts_unordered_normalize(u, &nfull) == TS_OK, "full");
    char *sall = NULL, *sfull = NULL;
    ts_encode(nall, &sall, NULL); ts_encode(nfull, &sfull, NULL);
    CHECK(strcmp(sall, sfull) == 0, "all-depths == full unordered");
    free(sall); free(sfull);

    /* No depths sorted â€” identity (order preserved) */
    bool mask_none[] = { false, false, false };
    TSNode *nnone = NULL;
    CHECK(ts_unordered_normalize_at_depths(u, mask_none, 3, &nnone) == TS_OK, "none");
    char *su = NULL, *sn = NULL;
    ts_encode(u, &su, NULL); ts_encode(nnone, &sn, NULL);
    CHECK(strcmp(su, sn) == 0, "no-sort preserves order");
    free(su); free(sn);

    /* Idempotence of depth-selective */
    TSNode *n0b = NULL;
    CHECK(ts_unordered_normalize_at_depths(n0, mask0, 3, &n0b) == TS_OK, "idem");
    char *s1 = NULL, *s2 = NULL;
    ts_encode(n0, &s1, NULL); ts_encode(n0b, &s2, NULL);
    CHECK(strcmp(s1, s2) == 0, "depth-selective idempotent");
    free(s1); free(s2);

    ts_free_tree(u); free(u);
    ts_free_tree(n0); free(n0);
    ts_free_tree(nall); free(nall);
    ts_free_tree(nfull); free(nfull);
    ts_free_tree(nnone); free(nnone);
    ts_free_tree(n0b); free(n0b);
    printf("   PASS\n\n");

    /* ========== Fuzz invariants ========== */
    printf("4. Fuzz invariants (100 random trees)\n");
    ts_ext_srand(2026);
    for (int i = 0; i < 100; i++) {
        TSNode *t = NULL;
        CHECK(ts_random_tree(6, 20, &t) == TS_OK, "fuzz gen");
        CHECK(is_canonical(t), "fuzz base");

        /* intern */
        TSInternTable *tt = ts_intern_create(8);
        int64_t id = ts_intern_add(tt, t);
        CHECK(id >= 0 && ts_intern_lookup(tt, t) == id, "fuzz intern");
        ts_intern_free(tt);

        /* depth-selective */
        bool m[8]; for (int d = 0; d < 8; d++) m[d] = (i + d) % 2 == 0;
        TSNode *nd = NULL;
        CHECK(ts_unordered_normalize_at_depths(t, m, 8, &nd) == TS_OK, "fuzz depth");
        CHECK(is_canonical(nd), "fuzz depth canonical");
        ts_free_tree(nd); free(nd);

        /* forest of one */
        TSNode *ff = NULL; size_t ffn = 0;
        CHECK(ts_forest_normalize(t, 1, &ff, &ffn) == TS_OK && ffn == 1, "fuzz forest");
        CHECK(is_canonical(&ff[0]), "fuzz forest canonical");
        ts_free_forest(ff, ffn);

        ts_free_tree(t); free(t);
    }
    printf("   PASS\n\n");

    printf("RESULT: %d/%d priority-1 tests passed.\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
