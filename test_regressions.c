/* EdgeTG — regression tests for bugs found in the v1.5 hidden-issue sweep.
 * Each group pins one fix; every test here failed (or crashed / leaked
 * under ASan+LSan) on the code before its fix. */
#include "ts_core.h"
#include "ts_layers.h"
#include "ts_roles.h"
#include "ts_norm.h"
#include "ts_enum.h"
#include "ts_packed.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Under ASan, make absurd allocations abort instead of silently succeeding
 * via overcommit, so an unbounded allocation driven by a hostile length
 * field is a hard test failure. Ignored when built without ASan. */
const char *__asan_default_options(void);
const char *__asan_default_options(void) { return "max_allocation_size=268435456"; }

static int g_pass = 0, g_fail = 0;
#define CHECK(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; printf("   FAIL: %s (%s:%d)\n", msg, __FILE__, __LINE__); } \
} while (0)

static void put_u32le(uint8_t *p, uint32_t x) {
    p[0] = (uint8_t)x; p[1] = (uint8_t)(x >> 8);
    p[2] = (uint8_t)(x >> 16); p[3] = (uint8_t)(x >> 24);
}

/* ts_values_decode: malformed blobs must be rejected without leaking the
 * payloads decoded so far and without trusting the header count. */
static void test_values_decode_untrusted(void) {
    printf("1. ts_values_decode rejects truncated / hostile blobs\n");
    TSValue *v = NULL; size_t n = 0;

    /* count=2, value 0 = "abc", value 1 header truncated (leaked "abc"). */
    uint8_t trunc[4 + 4 + 3 + 2];
    put_u32le(trunc, 2); put_u32le(trunc + 4, 3); memcpy(trunc + 8, "abc", 3);
    trunc[11] = 0; trunc[12] = 0;
    CHECK(ts_values_decode(trunc, sizeof trunc, &v, &n) == TS_ERR_INVALID_ARG,
          "truncated second value header rejected");

    /* count=2, value 0 = "abc", value 1 claims 9 bytes but has 1. */
    uint8_t short_body[4 + 4 + 3 + 4 + 1];
    put_u32le(short_body, 2); put_u32le(short_body + 4, 3);
    memcpy(short_body + 8, "abc", 3); put_u32le(short_body + 11, 9);
    short_body[15] = 'x';
    CHECK(ts_values_decode(short_body, sizeof short_body, &v, &n) == TS_ERR_INVALID_ARG,
          "short second value body rejected");

    /* 8-byte packet claiming 0xFFFFFFFF values (remote DoS: ~64 GiB calloc). */
    uint8_t hostile[8];
    put_u32le(hostile, 0xFFFFFFFFu); put_u32le(hostile + 4, 0);
    CHECK(ts_values_decode(hostile, sizeof hostile, &v, &n) == TS_ERR_INVALID_ARG,
          "hostile value count rejected");

    /* A valid blob still decodes. */
    uint8_t ok[4 + 4 + 2 + 4];
    put_u32le(ok, 2); put_u32le(ok + 4, 2); memcpy(ok + 8, "hi", 2); put_u32le(ok + 10, 0);
    CHECK(ts_values_decode(ok, sizeof ok, &v, &n) == TS_OK && n == 2 &&
          v[0].len == 2 && memcmp(v[0].data, "hi", 2) == 0 && v[1].len == 0,
          "valid blob decodes");
    if (v) ts_values_free(v, n);
}

/* ts_roles_decode: same untrusted-count / leak-on-truncation pattern. */
static void test_roles_decode_untrusted(void) {
    printf("2. ts_roles_decode rejects truncated / hostile blobs\n");
    TSRoleMap m;

    /* count=2, max_tag=5; role 0 has 3 bytes metadata; role 1 truncated. */
    uint8_t trunc[8 + 12 + 3 + 4];
    memset(trunc, 0, sizeof trunc);
    put_u32le(trunc, 2); put_u32le(trunc + 4, 5);
    put_u32le(trunc + 8, 0); put_u32le(trunc + 12, 1); put_u32le(trunc + 16, 3);
    memcpy(trunc + 20, "xyz", 3);
    CHECK(ts_roles_decode(trunc, sizeof trunc, &m) == TS_ERR_INVALID_ARG,
          "truncated second role rejected");

    /* role 1 metadata length overruns the buffer. */
    uint8_t overrun[8 + 12 + 3 + 12 + 1];
    memset(overrun, 0, sizeof overrun);
    put_u32le(overrun, 2); put_u32le(overrun + 4, 5);
    put_u32le(overrun + 16, 3); memcpy(overrun + 20, "xyz", 3);
    put_u32le(overrun + 31, 50);
    CHECK(ts_roles_decode(overrun, sizeof overrun, &m) == TS_ERR_INVALID_ARG,
          "overrunning metadata length rejected");

    /* 8-byte packet claiming 0xFFFFFFFF roles (~128 GiB calloc). */
    uint8_t hostile[8];
    put_u32le(hostile, 0xFFFFFFFFu); put_u32le(hostile + 4, 1);
    CHECK(ts_roles_decode(hostile, sizeof hostile, &m) == TS_ERR_INVALID_ARG,
          "hostile role count rejected");

    /* A valid one-role map still decodes. */
    uint8_t ok[8 + 12 + 2];
    put_u32le(ok, 1); put_u32le(ok + 4, 7);
    put_u32le(ok + 8, 0); put_u32le(ok + 12, 7); put_u32le(ok + 16, 2);
    memcpy(ok + 20, "md", 2);
    CHECK(ts_roles_decode(ok, sizeof ok, &m) == TS_OK && m.count == 1 &&
          m.max_tag == 7 && m.roles[0].role_tag == 7 &&
          m.roles[0].metadata.len == 2, "valid role map decodes");
    /* Contract: ts_roles_free does not own metadata; decode-owned buffers
     * are released by the caller (as in test_priority23 / test_ewma_leaf). */
    for (size_t i = 0; i < m.count; i++) free((void *)m.roles[i].metadata.data);
    ts_roles_free(&m);
}

/* ts_encode_forest: a NULL forest with a non-zero count must be rejected
 * (ts_encode already rejects a NULL tree); count 0 stays valid. */
static void test_encode_forest_null(void) {
    printf("3. ts_encode_forest argument validation\n");
    char *s = NULL; size_t len = 99;
    CHECK(ts_encode_forest(NULL, 3, &s, &len) == TS_ERR_INVALID_ARG && s == NULL,
          "NULL forest with count 3 rejected");
    CHECK(ts_encode_forest(NULL, 0, &s, &len) == TS_OK && s && s[0] == '\0' && len == 0,
          "empty forest encodes to empty string");
    free(s);
}

/* ts_forest_normalize_paired: each value must stay bound to its node when
 * the normalizer reorders children inside a tree (not just whole trees). */
static void test_forest_normalize_paired_binding(void) {
    printf("4. ts_forest_normalize_paired keeps values bound to their nodes\n");
    /* Tree 0: root R -> [A -> [a], B]; normalizing sorts leaf B before A.
     * Tree 1: single node T, which sorts before tree 0 ("_" < "_/...").  */
    TSNode *t0 = NULL, *t1 = NULL;
    if (ts_parse("_/_/_\\_\\", 8, &t0) != TS_OK || ts_parse("_", 8, &t1) != TS_OK) {
        CHECK(0, "fixture parse"); return;
    }
    TSNode forest[2] = { *t0, *t1 };
    free(t0); free(t1);
    const char *tags[5] = { "R", "A", "a", "B", "T" };   /* preorder, tree 0 then 1 */
    TSValue vals[5];
    for (int i = 0; i < 5; i++) { vals[i].data = (const uint8_t *)tags[i]; vals[i].len = 1; }

    TSNode *out = NULL; size_t out_n = 0; TSValue *ov = NULL;
    TSError e = ts_forest_normalize_paired(forest, 2, vals, 5, &out, &out_n, &ov);
    CHECK(e == TS_OK && out_n == 2, "normalize succeeds");
    if (e == TS_OK) {
        char *s0 = NULL, *s1 = NULL;
        ts_encode(&out[0], &s0, NULL); ts_encode(&out[1], &s1, NULL);
        CHECK(s0 && strcmp(s0, "_") == 0, "tree order: single node first");
        CHECK(s1 && strcmp(s1, "_/__/_\\\\") == 0, "tree 0 children sorted");
        const char *want = "TRBAa";   /* T | R, B, A, a */
        for (int i = 0; i < 5; i++) {
            char msg[64];
            snprintf(msg, sizeof msg, "output value %d is '%c'", i, want[i]);
            CHECK(ov[i].len == 1 && ov[i].data[0] == (uint8_t)want[i], msg);
        }
        free(s0); free(s1);
        ts_free_forest(out, out_n); free(ov);
    }
    ts_free_tree(&forest[0]); ts_free_tree(&forest[1]);
}

/* ts_count_trees: C_{n-1} stops fitting in uint64_t at n = 38. The DP
 * checked each product for overflow but not the running sum, so n = 38/39
 * returned the count mod 2^64. Unrepresentable counts must return 0. */
static void test_count_trees_overflow(void) {
    printf("5. ts_count_trees reports overflow instead of wrapping\n");
    CHECK(ts_count_trees(36) == 3116285494907301262ull, "n=36 exact");
    CHECK(ts_count_trees(37) == 11959798385860453492ull, "n=37 exact (largest that fits)");
    CHECK(ts_count_trees(38) == 0, "n=38 overflows -> 0");
    CHECK(ts_count_trees(39) == 0, "n=39 overflows -> 0");
    CHECK(ts_count_trees(40) == 0, "n=40 overflows -> 0");
}

/* ts_unpack: padding bits after the last symbol must be zero, so each
 * topology has exactly one packed encoding (ts_pack always zeroes them). */
static void test_unpack_padding(void) {
    printf("6. ts_unpack rejects non-zero padding bits\n");
    char out[8];
    const uint8_t clean[2] = { 0x04, 0x02 };        /* _/__\ (5 symbols) */
    CHECK(ts_unpack(clean, 5, out) == 1 && strcmp(out, "_/__\\") == 0,
          "clean packet unpacks");
    for (int bit = 2; bit < 8; bit++) {             /* symbols 5..7 live in bits 2..7 */
        uint8_t dirty[2] = { 0x04, (uint8_t)(0x02 | (1u << bit)) };
        char msg[48];
        snprintf(msg, sizeof msg, "padding bit %d set rejected", bit);
        CHECK(ts_unpack(dirty, 5, out) == 0, msg);
    }
    const uint8_t full[1] = { 0x94 };               /* 4 symbols: _ / / \ -> no padding */
    CHECK(ts_unpack(full, 4, out) == 1, "exact multiple of 4 has no padding");
}

int main(void) {
    printf("EdgeTG — REGRESSION TESTS\n\n");
    test_values_decode_untrusted();
    test_roles_decode_untrusted();
    test_encode_forest_null();
    test_forest_normalize_paired_binding();
    test_count_trees_overflow();
    test_unpack_padding();
    printf("\nRESULT: %d/%d regression assertions passed.\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
