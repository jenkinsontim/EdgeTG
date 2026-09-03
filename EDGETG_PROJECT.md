# EdgeTG Project

**Version 1.1**

A minimal, canonical tree topology language and the layers, mutations,
firmwares, and research tooling built on top of it.

## 1. Purpose

The EdgeTG treats ordered trees as first-class genomes. The core
language uses exactly three glyphs so that every higher-level operator can be
proven to preserve canonicity and to fail explicitly on illegal states.

## 2. Core grammar

Alphabet: `_` `/` `\`

- `_` â€” a node
- `/` â€¦ `\` â€” an ordered, non-empty list of child trees

Empty child lists (`_/\`) are illegal. The only legal encodings are those
produced by the canonical encoder; parse â†’ encode is an identity.

## 3. Invariants (non-negotiable)

1. Exactly three glyphs. No new symbols, ever.
2. Strict canonicity: one ordered tree â†” one string.
3. Meaning stays external (values, roles, scores, IDs).
4. Identity is positional (preorder index).
5. Empty child lists remain illegal.
6. Operations work on parsed trees, never raw strings.
7. Invalid states fail loudly (`TSError` / false / NULL).

## 4. Module map

| Module | Files | Role |
|--------|-------|------|
| **Core** | `ts_core.h/.c` | Parse, encode, clone, free, count, depth, forests |
| **Extension** | `ts_core_ext.h/.c` | Seeded RNG, random trees, Grow/Shrink/Swap/Retarget |
| **Layers** | `ts_layers.h/.c` | Paired data, schema, forest ops, unordered normalize |
| **Intern** | `ts_intern.h/.c` | Hash-cons table keyed by canonical string |
| **Norm** | `ts_norm.h/.c` | Forest normalize + depth-selective unordered normalize |
| **Roles** | `ts_roles.h/.c` | Per-position role tags + metadata (external artifact) |
| **Boltzmann** | `ts_boltzmann.h/.c` | Parameterized random tree generation |
| **Enum** | `ts_enum.h/.c` | Catalan counts + plane-tree unranking / enumeration |
| **Metric** | `ts_metric.h/.c` | Tree edit distance + canonical-string Levenshtein |
| **Device** | `device.h/.c` | Firmwares A/B/C + dedup persistence store |

## 5. Device firmwares

- **A â€” Ordered pipeline**: prefers deep narrow trees (depth â‰¥ 2, width â‰¤ 3).
- **B â€” Task tree**: width vs latency; legal when nodes â‰¤ 20 and depth â‰¤ 8.
- **C â€” Modular robot**: power = 1.5Â·nodes + 2Â·width; legal when power â‰¤ 40.

## 6. Build

```bash
make          # builds all test binaries + device_bench + integration
make test     # runs every suite under ASan/UBSan/LeakSanitizer
make clean
```

Individual suites:

```bash
gcc -std=c11 -Wall -Wextra -Wpedantic -Werror -O2 \
    -fsanitize=address,undefined -g \
    ts_core.c ts_layers.c test_layers.c -o test_layers
# â€¦ same pattern for test_extended, test_priority1, test_priority23,
#    device_bench, test_integration
```

## 7. Verified assertion counts

| Suite | Assertions | Coverage |
|-------|------------|----------|
| `test_layers` | 1531 | Four layers + 250-tree fuzz |
| `test_extended` | 2343 | RNG, mutations, 500-run fuzz |
| `test_priority1` | 1334 | Intern, forest-norm, depth-norm + fuzz |
| `test_priority23` | 861 | Roles, Boltzmann, enum, metrics + fuzz |
| `test_integration` | (cross-module) | Evolve â†’ values â†’ norm â†’ schema â†’ roles â†’ intern â†’ firmwares |
| **Total** | **â‰ˆ 6069+** | All under ASan/UBSan/LeakSanitizer, `-Werror` |

## 8. Repository layout

```
ts_core.h / ts_core.c
ts_core_ext.h / ts_core_ext.c
ts_layers.h / ts_layers.c
ts_intern.h / ts_intern.c
ts_norm.h / ts_norm.c
ts_roles.h / ts_roles.c
ts_boltzmann.h / ts_boltzmann.c
ts_enum.h / ts_enum.c
ts_metric.h / ts_metric.c
device.h / device.c
device_bench.c
test_layers.c
test_extended.c
test_priority1.c
test_priority23.c
test_integration.c
Makefile
TOPOLOGY_GENOME_PROJECT.md
README.md
```

## 9. Design notes

- Shrink clones before cascade and aborts with `TS_EXT_ERR_EMPTY_ROOT` if the
  root would empty â€” the original tree is never corrupted.
- Interning is an internal optimization; the wire format remains pure `_/\`.
- Roles and values are separate binary artifacts, never embedded in the string.
- Plane-tree unranking uses the standard first-child + rest Catalan decomposition.
