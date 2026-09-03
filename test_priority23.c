#include "ts_core.h"
#include "ts_core_ext.h"
#include "ts_layers.h"
#include "ts_roles.h"
#include "ts_boltzmann.h"
#include "ts_enum.h"
#include "ts_metric.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static int tests = 0, passed = 0;
#define CHECK(x, msg) do { \
    tests++; \
    if (!(x)) { printf("FAIL: %s (test #%d)\n", msg, tests); return 1; } \
    passed++; \
} while(0)

static bool trees_equal(const TSNode *a, const TSNode *b) {
    char *sa = NULL, *sb = NULL;
    if (ts_encode(a, &sa, NULL) != TS_OK || ts_encode(b, &sb, NULL) != TS_OK) {
        free(sa); free(sb); return false;
    }
    bool eq = strcmp(sa, sb) == 0;
    free(sa); free(sb);
    return eq;
}

/* ---- Roles helpers ---- */
static void free_decoded_roles(TSRoleMap *m) {
    if (!m) return;
    for (size_t i = 0; i < m->count; i++)
        free((void *)m->roles[i].metadata.data);
    ts_roles_free(m);
}

int main(void) {
    setbuf(stdout, NULL);
    printf("EdgeTG â€” PRIORITY 2 & 3 (expanded verification)\n\n");

    /* ================================================================
     * ROLES â€” unit + round-trip + edge cases
     * ================================================================ */
    printf("1. Extended role metadata\n");
    {
        TSNode *t = NULL;
        CHECK(ts_parse("_/__\\", 64, &t) == TS_OK, "parse 3-node");
        size_t n = ts_count_nodes(t);
        CHECK(n == 3, "node count");

        /* Happy path */
        TSRole rs[3] = {
            {0, 1, {NULL, 0}},
            {1, 2, {(const uint8_t *)"LEFT", 4}},
            {2, 3, {(const uint8_t *)"RIGHT", 5}}
        };
        TSRoleMap map;
        CHECK(ts_roles_create(rs, 3, 10, &map) == TS_OK, "create");
        CHECK(ts_roles_validate(t, &map), "validate complete");

        /* Missing index */
        map.roles[2].preorder_index = 1; /* duplicate 1, missing 2 */
        CHECK(!ts_roles_validate(t, &map), "duplicate index fails");
        map.roles[2].preorder_index = 2;

        /* Tag out of range */
        map.roles[0].role_tag = 11;
        CHECK(!ts_roles_validate(t, &map), "tag > max fails");
        map.roles[0].role_tag = 1;

        /* Wrong count */
        TSRoleMap shortmap;
        CHECK(ts_roles_create(rs, 2, 10, &shortmap) == TS_OK, "short create");
        CHECK(!ts_roles_validate(t, &shortmap), "wrong count fails");
        ts_roles_free(&shortmap);

        /* Serialization round-trip */
        uint8_t *blob = NULL; size_t blen = 0;
        CHECK(ts_roles_encode(&map, &blob, &blen) == TS_OK && blen > 0, "encode");
        TSRoleMap decoded;
        CHECK(ts_roles_decode(blob, blen, &decoded) == TS_OK, "decode");
        CHECK(decoded.count == 3 && decoded.max_tag == 10, "decoded header");
        CHECK(decoded.roles[1].role_tag == 2, "decoded tag");
        CHECK(decoded.roles[1].metadata.len == 4 &&
              memcmp(decoded.roles[1].metadata.data, "LEFT", 4) == 0, "decoded meta");
        CHECK(ts_roles_validate(t, &decoded), "decoded validates");

        /* Corrupt blob */
        if (blen > 4) blob[4] ^= 0xFF;
        TSRoleMap bad;
        /* may or may not fail depending on which byte; just exercise path */
        (void)ts_roles_decode(blob, blen, &bad);
        free_decoded_roles(&bad);
        free(blob);

        /* Empty tree / empty map */
        TSNode *leaf = NULL;
        CHECK(ts_parse("_", 64, &leaf) == TS_OK, "leaf");
        TSRole leaf_role = {0, 0, {NULL, 0}};
        TSRoleMap lm;
        CHECK(ts_roles_create(&leaf_role, 1, 0, &lm) == TS_OK, "leaf map");
        CHECK(ts_roles_validate(leaf, &lm), "leaf validates");
        uint8_t *lb = NULL; size_t ll = 0;
        CHECK(ts_roles_encode(&lm, &lb, &ll) == TS_OK, "leaf encode");
        TSRoleMap ld;
        CHECK(ts_roles_decode(lb, ll, &ld) == TS_OK && ld.count == 1, "leaf decode");
        free_decoded_roles(&ld); free(lb);
        ts_roles_free(&lm);
        ts_free_tree(leaf); free(leaf);

        free_decoded_roles(&decoded);
        ts_roles_free(&map);
        ts_free_tree(t); free(t);
    }
    printf("   PASS\n\n");

    /* ================================================================
     * BOLTZMANN â€” determinism, bounds, param effects
     * ================================================================ */
    printf("2. Boltzmann sampling\n");
    {
        TSBoltzmannParams def = ts_boltzmann_defaults();
        CHECK(def.leaf_probability > 0 && def.leaf_probability < 1, "default leaf_p");
        CHECK(def.max_children >= 1, "default max_children");

        /* Determinism: same seed + same params â†’ identical tree */
        ts_ext_srand(12345);
        TSNode *a = NULL, *b = NULL;
        CHECK(ts_random_tree_boltzmann(6, 20, &def, &a) == TS_OK, "boltz a");
        ts_ext_srand(12345);
        CHECK(ts_random_tree_boltzmann(6, 20, &def, &b) == TS_OK, "boltz b");
        CHECK(trees_equal(a, b), "determinism");
        CHECK(ts_count_nodes(a) <= 20 && ts_depth(a) <= 6, "bounds");
        CHECK(ts_canonical_roundtrip(a), "canonical");
        ts_free_tree(a); free(a); ts_free_tree(b); free(b);

        /* Different seeds â†’ (very likely) different trees */
        ts_ext_srand(1);
        CHECK(ts_random_tree_boltzmann(5, 15, &def, &a) == TS_OK, "seed1");
        ts_ext_srand(2);
        CHECK(ts_random_tree_boltzmann(5, 15, &def, &b) == TS_OK, "seed2");
        /* not required to differ, but usually will; just check both valid */
        CHECK(ts_canonical_roundtrip(a) && ts_canonical_roundtrip(b), "both canonical");
        ts_free_tree(a); free(a); ts_free_tree(b); free(b);

        /* High leaf probability â†’ shallower / smaller */
        TSBoltzmannParams high_leaf = {0.9, 3, 0.5};
        size_t total_nodes = 0;
        ts_ext_srand(7);
        for (int i = 0; i < 30; i++) {
            TSNode *t = NULL;
            CHECK(ts_random_tree_boltzmann(8, 30, &high_leaf, &t) == TS_OK, "high leaf");
            total_nodes += ts_count_nodes(t);
            CHECK(ts_canonical_roundtrip(t), "high leaf canonical");
            ts_free_tree(t); free(t);
        }
        CHECK(total_nodes < 30 * 15, "high leaf tends smaller");

        /* max_children = 1 â†’ pure spines */
        TSBoltzmannParams spine_p = {0.1, 1, 1.0};
        ts_ext_srand(11);
        for (int i = 0; i < 20; i++) {
            TSNode *t = NULL;
            CHECK(ts_random_tree_boltzmann(10, 15, &spine_p, &t) == TS_OK, "spine param");
            /* every node has at most 1 child */
            /* walk: width <= 1 */
            size_t w = 0;
            const TSNode *p = t;
            while (p) {
                if (p->child_count > w) w = p->child_count;
                p = p->child_count ? &p->children[0] : NULL;
            }
            CHECK(w <= 1, "max_children=1 enforces spine");
            ts_free_tree(t); free(t);
        }

        /* Invalid args */
        TSNode *bad = NULL;
        CHECK(ts_random_tree_boltzmann(0, 10, &def, &bad) == TS_ERR_INVALID_ARG, "depth 0");
        CHECK(ts_random_tree_boltzmann(5, 0, &def, &bad) == TS_ERR_INVALID_ARG, "nodes 0");
        CHECK(ts_random_tree_boltzmann(5, 10, &def, NULL) == TS_ERR_INVALID_ARG, "null out");
    }
    printf("   PASS\n\n");

    /* ================================================================
     * ENUMERATION â€” Catalan counts + real unranking
     * ================================================================ */
    printf("3. Combinatorial enumeration\n");
    {
        /* Known Catalan numbers C_{n-1} for trees with n nodes */
        CHECK(ts_count_trees(1) == 1,   "n=1 â†’ 1");
        CHECK(ts_count_trees(2) == 1,   "n=2 â†’ 1");
        CHECK(ts_count_trees(3) == 2,   "n=3 â†’ 2");
        CHECK(ts_count_trees(4) == 5,   "n=4 â†’ 5");
        CHECK(ts_count_trees(5) == 14,  "n=5 â†’ 14");
        CHECK(ts_count_trees(6) == 42,  "n=6 â†’ 42");
        CHECK(ts_count_trees(7) == 132, "n=7 â†’ 132");
        CHECK(ts_count_trees(0) == 0,   "n=0 â†’ 0");

        /* Unrank every tree of size 1..5 and check:
         * - correct node count
         * - canonical
         * - all distinct
         * - count matches */
        for (size_t n = 1; n <= 5; n++) {
            uint64_t total = ts_count_trees(n);
            char **seen = (char **)calloc(total, sizeof(char *));
            CHECK(seen != NULL, "alloc seen");
            for (uint64_t i = 0; i < total; i++) {
                TSNode *t = NULL;
                CHECK(ts_tree_index(n, i, &t) == TS_OK, "unrank");
                CHECK(ts_count_nodes(t) == n, "exact size");
                CHECK(ts_canonical_roundtrip(t), "canonical");
                char *enc = NULL;
                CHECK(ts_encode(t, &enc, NULL) == TS_OK, "encode");
                /* uniqueness */
                for (uint64_t j = 0; j < i; j++)
                    CHECK(strcmp(seen[j], enc) != 0, "distinct");
                seen[i] = enc;
                ts_free_tree(t); free(t);
            }
            /* out of range */
            TSNode *bad = NULL;
            CHECK(ts_tree_index(n, total, &bad) == TS_ERR_INVALID_ARG, "oob index");
            for (uint64_t i = 0; i < total; i++) free(seen[i]);
            free(seen);
        }

        /* enumerate_trees visits the same count */
        uint64_t walked = ts_enumerate_trees(4, NULL, NULL);
        CHECK(walked == 5, "enumerate n=4 count");
    }
    printf("   PASS\n\n");

    /* ================================================================
     * METRICS â€” known-answer cases
     * ================================================================ */
    printf("4. Topology distance metrics (known answers)\n");
    {
        TSNode *leaf = NULL, *unary = NULL, *binary = NULL, *spine3 = NULL;
        CHECK(ts_parse("_", 64, &leaf) == TS_OK, "leaf");
        CHECK(ts_parse("_/_\\", 64, &unary) == TS_OK, "unary");
        CHECK(ts_parse("_/__\\", 64, &binary) == TS_OK, "binary");
        CHECK(ts_parse("_/_/_\\\\", 64, &spine3) == TS_OK, "spine3");

        /* Identity */
        CHECK(ts_edit_distance(leaf, leaf) == 0, "edit id leaf");
        CHECK(ts_edit_distance(binary, binary) == 0, "edit id binary");
        CHECK(ts_canonical_distance(leaf, leaf) == 0, "canon id");

        /* leaf â†” unary: insert one node â†’ distance 1 */
        CHECK(ts_edit_distance(leaf, unary) == 1, "edit leafâ†’unary = 1");
        CHECK(ts_edit_distance(unary, leaf) == 1, "edit unaryâ†’leaf = 1");

        /* leaf â†” binary: insert two leaves â†’ distance 2 */
        CHECK(ts_edit_distance(leaf, binary) == 2, "edit leafâ†’binary = 2");

        /* unary â†” binary: replace one leaf with two â†’ at least 1 */
        size_t d_ub = ts_edit_distance(unary, binary);
        CHECK(d_ub >= 1 && d_ub <= 3, "edit unaryâ†”binary in [1,3]");

        /* Canonical string distance is a metric (non-neg, symmetric, id) */
        CHECK(ts_canonical_distance(leaf, unary) > 0, "canon differs");
        CHECK(ts_canonical_distance(leaf, unary) ==
              ts_canonical_distance(unary, leaf), "canon symmetric");

        /* Same shape different? no â€” same string â†’ 0 */
        TSNode *leaf2 = NULL;
        CHECK(ts_parse("_", 64, &leaf2) == TS_OK, "leaf2");
        CHECK(ts_canonical_distance(leaf, leaf2) == 0, "same string");
        ts_free_tree(leaf2); free(leaf2);

        ts_free_tree(leaf); free(leaf);
        ts_free_tree(unary); free(unary);
        ts_free_tree(binary); free(binary);
        ts_free_tree(spine3); free(spine3);
    }
    printf("   PASS\n\n");

    /* ================================================================
     * FUZZ cross-module
     * ================================================================ */
    printf("5. Cross-module fuzz (50 trees)\n");
    {
        ts_ext_srand(20260831);
        for (int i = 0; i < 50; i++) {
            TSNode *t = NULL;
            TSBoltzmannParams p = ts_boltzmann_defaults();
            CHECK(ts_random_tree_boltzmann(5, 12, &p, &t) == TS_OK, "fuzz gen");
            CHECK(ts_canonical_roundtrip(t), "fuzz canonical");

            /* roles covering every node */
            size_t n = ts_count_nodes(t);
            TSRole *roles = (TSRole *)calloc(n, sizeof(TSRole));
            CHECK(roles != NULL, "roles alloc");
            for (size_t j = 0; j < n; j++) {
                roles[j].preorder_index = j;
                roles[j].role_tag = (uint32_t)(j % 4);
            }
            TSRoleMap m;
            CHECK(ts_roles_create(roles, n, 10, &m) == TS_OK, "fuzz roles");
            CHECK(ts_roles_validate(t, &m), "fuzz validate");
            uint8_t *blob = NULL; size_t bl = 0;
            CHECK(ts_roles_encode(&m, &blob, &bl) == TS_OK, "fuzz encode");
            TSRoleMap m2;
            CHECK(ts_roles_decode(blob, bl, &m2) == TS_OK, "fuzz decode");
            CHECK(ts_roles_validate(t, &m2), "fuzz decoded ok");
            free_decoded_roles(&m2); free(blob);
            ts_roles_free(&m); free(roles);

            /* self-distance zero */
            CHECK(ts_edit_distance(t, t) == 0, "fuzz edit id");
            CHECK(ts_canonical_distance(t, t) == 0, "fuzz canon id");

            ts_free_tree(t); free(t);
        }
    }
    printf("   PASS\n\n");

    printf("RESULT: %d/%d priority-2/3 tests passed.\n", passed, tests);
    return (passed == tests) ? 0 : 1;
}
