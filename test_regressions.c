/* EdgeTG — regression tests for bugs found in the v1.5 hidden-issue sweep.
 * Each group pins one fix; every test here failed (or crashed / leaked
 * under ASan+LSan) on the code before its fix. */
#include "ts_core.h"
#include "ts_layers.h"
#include "ts_roles.h"
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

int main(void) {
    printf("EdgeTG — REGRESSION TESTS\n\n");
    test_values_decode_untrusted();
    test_roles_decode_untrusted();
    test_encode_forest_null();
    printf("\nRESULT: %d/%d regression assertions passed.\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
