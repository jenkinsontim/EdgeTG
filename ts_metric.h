#ifndef TS_METRIC_H
#define TS_METRIC_H

#include "ts_core.h"
#include <stddef.h>

/* Tree edit distance (insert / delete only; nodes unlabeled).
 * Classic Zhang-Shasha simplified for unordered? We use ordered TED. */
size_t ts_edit_distance(const TSNode *a, const TSNode *b);

/* Fast approximation: Levenshtein distance of the two canonical strings. */
size_t ts_canonical_distance(const TSNode *a, const TSNode *b);
#endif
