/**
 * EWMA Prototype — hardened integer implementation
 * - Explicit signed arithmetic for (x - p)
 * - Portable handling of the α scaling
 * - Boundary-safe clamping
 */

#include "ewma_leaf.h"

void ewma_leaf_init(ewma_leaf_t *leaf, uint8_t num_classes, uint8_t num_features)
{
    if (num_classes > EWMA_MAX_CLASSES) num_classes = EWMA_MAX_CLASSES;
    if (num_features > EWMA_MAX_FEATURES) num_features = EWMA_MAX_FEATURES;

    leaf->num_classes  = num_classes;
    leaf->num_features = num_features;
    leaf->recent_errors = 0;
    leaf->recent_total  = 0;

    for (uint8_t c = 0; c < num_classes; c++) {
        for (uint8_t f = 0; f < num_features; f++) {
            leaf->proto[c][f] = 128;          /* mid-scale start */
        }
    }
}

uint8_t ewma_leaf_predict(const ewma_leaf_t *leaf, const uint8_t *features)
{
    uint8_t  best = 0;
    uint16_t best_dist = 0xFFFF;

    for (uint8_t c = 0; c < leaf->num_classes; c++) {
        uint16_t dist = 0;
        for (uint8_t f = 0; f < leaf->num_features; f++) {
            const uint8_t x = features[f] ? 255u : 0u;
            int16_t d = (int16_t)leaf->proto[c][f] - (int16_t)x;
            if (d < 0) d = (int16_t)(-d);
            dist = (uint16_t)(dist + (uint16_t)d);
        }
        if (dist < best_dist) {
            best_dist = dist;
            best = c;
        }
    }
    return best;
}

/* Core update with explicit alpha (Q8).  Safe for the full 0↔255 range. */
void ewma_leaf_update_alpha(ewma_leaf_t *leaf, const uint8_t *features,
                            uint8_t true_class, uint8_t alpha_q8)
{
    if (true_class >= leaf->num_classes) return;

    /* error tracking (optional, for higher-level monitors) */
    const uint8_t pred = ewma_leaf_predict(leaf, features);
    leaf->recent_total++;
    if (pred != true_class) leaf->recent_errors++;
    if (leaf->recent_total >= 256u) {
        leaf->recent_errors = (uint16_t)(leaf->recent_errors >> 1);
        leaf->recent_total  = (uint16_t)(leaf->recent_total  >> 1);
    }

    for (uint8_t f = 0; f < leaf->num_features; f++) {
        const uint8_t x = features[f] ? 255u : 0u;
        /* signed difference — critical for 255→0 and 0→255 */
        const int16_t diff = (int16_t)x - (int16_t)leaf->proto[true_class][f];

        /* delta = (diff * alpha_q8) / 256
         * Use 32-bit intermediate then arithmetic-style divide.
         */
        const int32_t prod = (int32_t)diff * (int32_t)alpha_q8;
        int16_t delta;
        if (prod >= 0) {
            delta = (int16_t)(prod / 256);
        } else {
            /* toward-zero truncation for negative */
            delta = (int16_t)(- ((-prod) / 256));
        }

        int16_t np = (int16_t)leaf->proto[true_class][f] + delta;
        if (np < 0)   np = 0;
        if (np > 255) np = 255;
        leaf->proto[true_class][f] = (uint8_t)np;
    }
}

void ewma_leaf_update(ewma_leaf_t *leaf, const uint8_t *features, uint8_t true_class)
{
    ewma_leaf_update_alpha(leaf, features, true_class, (uint8_t)EWMA_ALPHA);
}

uint8_t ewma_leaf_error_rate(const ewma_leaf_t *leaf)
{
    if (leaf->recent_total == 0) return 0;
    return (uint8_t)((leaf->recent_errors * 255u) / leaf->recent_total);
}
