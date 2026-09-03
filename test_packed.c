#include "ts_core.h"
#include "ts_core_ext.h"
#include "ts_packed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pass = 0, fail = 0;
#define CHECK(name, cond) do { \
    if (cond) { printf("PASS  %s\n", name); pass++; } \
    else      { printf("FAIL  %s\n", name); fail++; } \
} while(0)

static void round_trip_check(const char *label, TSNode *tree) {
    char *ascii = NULL; size_t ascii_len = 0;
    ts_encode(tree, &ascii, &ascii_len);

    uint8_t packed[2048];
    size_t packed_len = ts_pack(ascii, ascii_len, packed);
    size_t expected_packed_len = (ascii_len + 3) / 4;
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: pack size matches formula (%zu glyphs -> %zu bytes)",
                 label, ascii_len, packed_len);
        CHECK(msg, packed_len == expected_packed_len);
    }

    char unpacked[2048];
    int ok = ts_unpack(packed, ascii_len, unpacked);
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: unpack succeeds", label);
        CHECK(msg, ok);
    }
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: unpacked ASCII matches original exactly", label);
        CHECK(msg, strcmp(ascii, unpacked) == 0);
    }

    TSNode *reparsed = NULL;
    TSError e = ts_parse(unpacked, 64, &reparsed);
    {
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: re-parse of unpacked data succeeds", label);
        CHECK(msg, e == TS_OK);
    }
    if (e == TS_OK) {
        char *reencoded = NULL;
        ts_encode(reparsed, &reencoded, NULL);
        char msg[128];
        snprintf(msg, sizeof(msg), "%s: full round trip identical (%zu -> %zu bytes, %.2fx)",
                 label, ascii_len, packed_len, (double)ascii_len / packed_len);
        CHECK(msg, strcmp(ascii, reencoded) == 0);
        free(reencoded);
        ts_free_tree(reparsed); free(reparsed);
    }
    free(ascii);
}

int main(void) {
    ts_ext_srand(2026);

    /* Edge case 1: the smallest possible tree -- a single bare leaf, "_" (1 symbol). */
    {
        TSNode *t = calloc(1, sizeof(TSNode));
        round_trip_check("single leaf (1 symbol, worst-case padding)", t);
        free(t);
    }

    /* Edge case 2: exactly 4 symbols (no padding needed at all) */
    {
        TSNode *leaf1 = calloc(1, sizeof(TSNode));
        TSNode *t = calloc(1, sizeof(TSNode));
        t->children = leaf1; t->child_count = 1; /* "_/_\" = 4 symbols exactly */
        round_trip_check("exactly 4 symbols (zero padding)", t);
        free(t->children); free(t);
    }

    /* Edge case 3: 5 symbols (1 byte + 1 more, maximum padding: 6 wasted bits) */
    {
        TSNode *leaf1 = calloc(1, sizeof(TSNode));
        TSNode *leaf2 = calloc(1, sizeof(TSNode));
        TSNode *t = calloc(1, sizeof(TSNode));
        t->children = calloc(2, sizeof(TSNode));
        t->children[0] = *leaf1; t->children[1] = *leaf2;
        t->child_count = 2; /* "_/__\" = 5 symbols */
        free(leaf1); free(leaf2);
        round_trip_check("5 symbols (maximum padding case)", t);
        ts_free_tree(t); free(t);
    }

    /* Cases across a range of random tree sizes, using the actual verified RNG */
    for (int trial = 0; trial < 10; trial++) {
        TSNode *t = NULL;
        size_t max_nodes = 5 + trial * 15; /* 5, 20, 35, ... up to ~140 nodes */
        TSError e = ts_random_tree(12, max_nodes, &t);
        if (e != TS_OK) continue;
        char label[64];
        snprintf(label, sizeof(label), "random tree #%d (max_nodes=%zu, actual=%zu)",
                 trial, max_nodes, ts_count_nodes(t));
        round_trip_check(label, t);
        ts_free_tree(t); free(t);
    }

    printf("\n%d passed, %d failed\n", pass, fail);
    return fail == 0 ? 0 : 1;
}
