/* EdgeTG — regression tests for bugs found in the v1.5 hidden-issue sweep.
 * Each group pins one fix; every test here failed (or crashed / leaked
 * under ASan+LSan) on the code before its fix. */
#include "ts_core.h"
#include "ts_layers.h"
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

int main(void) {
    printf("EdgeTG — REGRESSION TESTS\n\n");
    test_values_decode_untrusted();
    printf("\nRESULT: %d/%d regression assertions passed.\n", g_pass, g_pass + g_fail);
    return g_fail ? 1 : 0;
}
