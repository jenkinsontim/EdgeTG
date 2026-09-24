#include "ewma_leaf.h"
#include <stdio.h>
#include <string.h>
#include <assert.h>

static void print_proto(const ewma_leaf_t *l, uint8_t c) {
    printf("  proto[%u]:", c);
    for (uint8_t f = 0; f < l->num_features; f++) printf(" %3u", l->proto[c][f]);
    printf("\n");
}

int main(void) {
    printf("sizeof(ewma_leaf_t) = %zu bytes\n\n", sizeof(ewma_leaf_t));

    ewma_leaf_t leaf;
    ewma_leaf_init(&leaf, 2, 8);

    uint8_t zeros[8] = {0};
    uint8_t ones[8]  = {1,1,1,1,1,1,1,1};

    /* 1. Drive class 0 → all-1, class 1 → all-0 */
    for (int i = 0; i < 250; i++) {
        ewma_leaf_update(&leaf, ones,  0);
        ewma_leaf_update(&leaf, zeros, 1);
    }
    printf("After 250 updates (class0←ones, class1←zeros):\n");
    print_proto(&leaf, 0);
    print_proto(&leaf, 1);
    assert(ewma_leaf_predict(&leaf, ones)  == 0);
    assert(ewma_leaf_predict(&leaf, zeros) == 1);
    printf("  predictions OK\n\n");

    /* 2. Dangerous reversal: force class 0 from ~255 down to 0 */
    for (int i = 0; i < 400; i++) ewma_leaf_update(&leaf, zeros, 0);
    printf("After forcing class 0 → zeros:\n");
    print_proto(&leaf, 0);
    assert(leaf.proto[0][0] < 10);
    printf("  reversal OK (no unsigned wrap)\n\n");

    /* 3. Rapid alternation */
    ewma_leaf_init(&leaf, 2, 8);
    for (int i = 0; i < 100; i++) {
        ewma_leaf_update(&leaf, ones,  0);
        ewma_leaf_update(&leaf, zeros, 0);
    }
    printf("After rapid 0/1 alternation on class 0:\n");
    print_proto(&leaf, 0);
    assert(leaf.proto[0][0] > 80 && leaf.proto[0][0] < 180);
    printf("  alternation OK (stable mid-scale)\n\n");

    /* 4. Constant extremes */
    ewma_leaf_init(&leaf, 2, 8);
    for (int i = 0; i < 300; i++) ewma_leaf_update(&leaf, ones, 0);
    assert(leaf.proto[0][0] >= 250);
    for (int i = 0; i < 300; i++) ewma_leaf_update(&leaf, zeros, 0);
    assert(leaf.proto[0][0] <= 5);
    printf("Constant extreme tracking OK\n");

    printf("\nAll boundary tests passed.\n");
    return 0;
}
