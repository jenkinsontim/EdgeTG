# EWMA Prototype Leaf Classifier

**Status:** Recommended default adaptive leaf for EdgeTG  
**Memory:** 16–30 bytes (logical state)  
**Target:** Cortex-M0+ and similar ultra-low-power MCUs  
**Learning:** Single-pass online, supervised

## Overview

The EWMA Prototype is a tiny online classifier designed for the leaf nodes of an EdgeTG tree. It maintains one prototype vector per class and updates them with an exponentially weighted moving average (EWMA). Prediction is nearest-prototype classification using L1 distance.

It was selected after systematic comparison against Nearest Centroid, Winnow, Naive Bayes, Minimal Tsetlin, and integer Softmax under controlled synthetic conditions that match the intended EdgeTG leaf constraints (≤ 60 bytes, binary features, online single-pass learning, sensor and concept drift).

### Why EWMA

| Property              | EWMA Prototype                          | EdgeTG Leaf Requirement      |
|-----------------------|-----------------------------------------|------------------------------|
| Memory                | 16 B (2-class) / 24 B (3-class) logical | ✓ 20–60 B target             |
| Arithmetic            | Integer only (shifts + adds)            | ✓ No FPU required            |
| Online / single-pass  | Yes                                     | ✓ No batch / no epochs       |
| Gradual sensor drift  | Built-in via α decay                    | ✓ Tracks shifting baseline   |
| Concept reversal      | Fast recovery (~40–50 samples)          | ✓ Adapts without cloud       |
| Explainability        | Prototype vectors are human-inspectable | ✓ Desirable                  |
| Code complexity       | Very low                                | ✓ Easy to audit & port       |

The exponential forgetting (controlled by α) gives natural drift adaptation without explicit “move toward / move away” logic or multiplicative weight updates.

## Architecture Fit

```
EdgeTG decision tree
        │
   internal nodes (routing)
        │
   ┌────┴────┐
   │         │
 leaf       leaf
   │         │
EWMA      EWMA
prototype prototype
   │         │
prediction prediction
```

The tree supplies context separation and hierarchical structure. Each leaf only needs to solve a small local classification problem and adapt to local sensor or concept drift.

## Memory Footprint

| Configuration              | Logical state | `sizeof(ewma_leaf_t)` (current C) |
|----------------------------|---------------|-----------------------------------|
| 2 classes × 8 features     | 16 B          | 30 B (with max-array + counters)  |
| 3 classes × 8 features     | 24 B          | 30 B                              |

The C implementation uses fixed compile-time maxima (`EWMA_MAX_CLASSES`, `EWMA_MAX_FEATURES`) for simplicity. Reducing the maxima or stripping the optional error counters brings the struct size closer to the logical minimum.

## API (C)

```c
#include "ewma_leaf.h"

ewma_leaf_t leaf;
ewma_leaf_init(&leaf, num_classes, num_features);   // e.g. 2, 8

// Online loop
uint8_t features[8];          // 0/1 binary features from thermometer encoding
uint8_t pred = ewma_leaf_predict(&leaf, features);
ewma_leaf_update(&leaf, features, true_label);      // after label is known

// Optional monitoring for higher-level drift detection
uint8_t err = ewma_leaf_error_rate(&leaf);          // 0–255
```

### Key functions

| Function | Description |
|----------|-------------|
| `ewma_leaf_init` | Initialise prototypes to mid-scale (128) |
| `ewma_leaf_predict` | Nearest prototype (L1) |
| `ewma_leaf_update` | EWMA update with compile-time α |
| `ewma_leaf_update_alpha` | Same, but α supplied at runtime (Q8) |
| `ewma_leaf_error_rate` | Recent error rate for tree-level monitoring |

### Fixed-point α

α is represented as a Q8 value:

```
α_real = EWMA_ALPHA / 256
```

Default: `EWMA_ALPHA = 51` (≈ 0.20).  
Recommended operating range from controlled sweeps: **0.20 – 0.25**.

## Controlled Benchmark Results

Protocol (identical for all classifiers):

- Synthetic well-separated binary prototypes
- 8 features, bit-flip noise ≈ 8 %
- Correct online order: **predict → score → update**
- 1500 steps, 8 random seeds
- Scenarios: stationary, gradual sensor drift, abrupt concept reversal

### 2-class

| Classifier        | Mem  | Stationary | Gradual | Abrupt (acc / recovery) | Avg   |
|-------------------|------|------------|---------|-------------------------|-------|
| **EWMA (α=0.20)** | 16 B | 0.956      | 0.563   | 0.951 (43)              | **0.823** |
| EWMA (α=0.25)     | 16 B | 0.955      | 0.561   | 0.951 (43)              | 0.822 |
| Winnow            | 32 B | 0.933      | 0.561   | 0.916 (71)              | 0.803 |
| Nearest Centroid  | 20 B | 0.956      | 0.568   | 0.507 (never recovered) | 0.677 |
| Naive Bayes       | 20 B | 0.755      | 0.538   | 0.505                   | 0.599 |
| Minimal Tsetlin   | 64 B | ~0.50      | ~0.50   | ~0.50                   | 0.501 |

### 3-class

| Classifier        | Mem  | Stationary | Gradual | Abrupt (acc / recovery) | Avg   |
|-------------------|------|------------|---------|-------------------------|-------|
| **EWMA (α=0.20)** | 24 B | 0.928      | 0.406   | 0.921 (45)              | **0.752** |
| EWMA (α=0.25)     | 24 B | 0.925      | 0.407   | 0.919 (43)              | 0.750 |
| Winnow            | 48 B | 0.797      | 0.383   | 0.751 (144)             | 0.644 |
| Nearest Centroid  | 30 B | 0.933      | 0.414   | 0.559 (never recovered) | 0.636 |
| Naive Bayes       | 30 B | 0.699      | 0.360   | 0.476                   | 0.512 |
| Minimal Tsetlin   | 96 B | ~0.33      | ~0.33   | ~0.33                   | 0.329 |

EWMA provides the best accuracy / memory / recovery trade-off under these conditions.

## α Characterisation

| α (Q8) | α real | Stationary | Slow drift | Medium drift | Abrupt recovery |
|--------|--------|------------|------------|--------------|-----------------|
| 8      | 0.031  | highest    | weaker     | weaker       | good            |
| 32     | 0.125  | high      | good       | good         | good            |
| **51** | **0.20** | high     | **best region** | **best region** | excellent   |
| 64     | 0.25   | high       | **best region** | **best region** | excellent   |
| 128    | 0.50   | slightly lower | good   | good         | good            |

Fixed α in the 0.20–0.25 range is currently recommended. Adaptive-α remains an optional future experiment.

## Implementation Notes

- Prototypes are `uint8_t` in [0, 255]. Binary features are scaled to 0 / 255.
- All intermediate arithmetic for the update uses signed types to handle the full 0 ↔ 255 range safely.
- The optional error counters support higher-level drift detection by the EdgeTG tree; they can be removed if absolute minimum RAM is required.
- No floating-point, no dynamic allocation, no libraries beyond `<stdint.h>`.

## Limitations

- Supervised: a true label is required for the update step.
- Gradual drift performance depends on α and noise level; very fast continuous drift may need a higher α or an adaptive scheme.
- The one-clause Minimal Tsetlin implementation tested here is not representative of a full Tsetlin Machine; stronger Tsetlin variants would require more memory.
- Results on highly lossy real-world feature subsets (e.g. arbitrary 8-bit slices of image data) can be much lower than the controlled synthetic numbers. Always validate on the target sensor representation.

## Files

| File                    | Description                              |
|-------------------------|------------------------------------------|
| `ewma_leaf.h`           | Public API and configuration             |
| `ewma_leaf.c`           | Integer implementation                   |
| `ewma_boundary_test.c`  | Signed-arithmetic and extreme-value tests|
| `synthetic_leaf_battle.py` | Controlled head-to-head benchmark     |

## Recommended Usage in EdgeTG

1. Use EWMA Prototype as the default leaf classifier.
2. Keep Winnow available as a higher-capacity alternative when a leaf can afford ~2× memory.
3. Expose the leaf’s error rate (or confidence) to the tree so that routing or leaf replacement decisions can be made when a leaf becomes unreliable.
4. Validate α and feature encoding on real sensor traces before freezing the production configuration.

## License

Same as the parent EdgeTG project.
