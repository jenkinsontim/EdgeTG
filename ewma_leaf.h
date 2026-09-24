/**
 * EdgeTG Adaptive Leaf — EWMA Prototype Classifier
 *
 * Pure integer, single-pass online, Cortex-M0+ friendly.
 * Prototypes stored as uint8_t [0..255].
 * Binary features are scaled 0 → 0, 1 → 255.
 *
 * Memory (with current MAX settings):
 *   sizeof(ewma_leaf_t) == 30 bytes  (3 classes × 8 features + counters)
 * Logical minimum for a 2-class leaf (if MAX reduced / counters stripped): ~16–22 B
 */

#ifndef EWMA_LEAF_H
#define EWMA_LEAF_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EWMA_MAX_CLASSES
#define EWMA_MAX_CLASSES  3
#endif

#ifndef EWMA_MAX_FEATURES
#define EWMA_MAX_FEATURES 8
#endif

/* α as Q8 fixed-point: α_real = EWMA_ALPHA / 256
 * Default 51 ≈ 0.199.  Change at compile time or pass at runtime if desired.
 */
#ifndef EWMA_ALPHA
#define EWMA_ALPHA  51
#endif

typedef struct {
    uint8_t  proto[EWMA_MAX_CLASSES][EWMA_MAX_FEATURES];
    uint8_t  num_classes;
    uint8_t  num_features;
    uint16_t recent_errors;   /* for optional higher-level monitoring */
    uint16_t recent_total;
} ewma_leaf_t;

/* Compile-time size check (optional but useful) */
_Static_assert(sizeof(ewma_leaf_t) <= 64, "ewma_leaf_t unexpectedly large");

void     ewma_leaf_init(ewma_leaf_t *leaf, uint8_t num_classes, uint8_t num_features);
uint8_t  ewma_leaf_predict(const ewma_leaf_t *leaf, const uint8_t *features);
void     ewma_leaf_update(ewma_leaf_t *leaf, const uint8_t *features, uint8_t true_class);
void     ewma_leaf_update_alpha(ewma_leaf_t *leaf, const uint8_t *features,
                                uint8_t true_class, uint8_t alpha_q8);
uint8_t  ewma_leaf_error_rate(const ewma_leaf_t *leaf);

#ifdef __cplusplus
}
#endif

#endif /* EWMA_LEAF_H */
