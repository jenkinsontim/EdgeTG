#include "ts_metric.h"
#include <stdlib.h>
#include <string.h>

/* Simple ordered tree edit distance via recursive DP on subtrees.
 * Cost: insert=1, delete=1, relabel=0 (unlabeled). */
static size_t ted(const TSNode *a, const TSNode *b);

static size_t forest_dist(const TSNode *a, size_t ai, size_t an,
                          const TSNode *b, size_t bi, size_t bn) {
    /* DP for forests a[ai..an) vs b[bi..bn) */
    size_t rows = an - ai + 1, cols = bn - bi + 1;
    size_t *dp = (size_t *)calloc(rows * cols, sizeof(size_t));
    if (!dp) return (size_t)-1;
    for (size_t i = 0; i < rows; i++) dp[i * cols] = i; /* delete */
    for (size_t j = 0; j < cols; j++) dp[j] = j;         /* insert */
    for (size_t i = 1; i < rows; i++) {
        for (size_t j = 1; j < cols; j++) {
            size_t del = dp[(i - 1) * cols + j] + 1;
            size_t ins = dp[i * cols + (j - 1)] + 1;
            size_t sub = dp[(i - 1) * cols + (j - 1)] +
                         ted(&a[ai + i - 1], &b[bi + j - 1]);
            size_t m = del < ins ? del : ins;
            if (sub < m) m = sub;
            dp[i * cols + j] = m;
        }
    }
    size_t r = dp[(rows - 1) * cols + (cols - 1)];
    free(dp);
    return r;
}

static size_t ted(const TSNode *a, const TSNode *b) {
    if (!a && !b) return 0;
    if (!a) return ts_count_nodes(b);
    if (!b) return ts_count_nodes(a);
    /* cost of matching root (0) + forest distance of children */
    return forest_dist(a->children, 0, a->child_count,
                       b->children, 0, b->child_count);
}

size_t ts_edit_distance(const TSNode *a, const TSNode *b) {
    if (!a && !b) return 0;
    if (!a) return b ? ts_count_nodes(b) : 0;
    if (!b) return ts_count_nodes(a);
    return ted(a, b);
}

static size_t levenshtein(const char *s, const char *t) {
    size_t n = strlen(s), m = strlen(t);
    size_t *dp = (size_t *)calloc((n + 1) * (m + 1), sizeof(size_t));
    if (!dp) return (size_t)-1;
    for (size_t i = 0; i <= n; i++) dp[i * (m + 1)] = i;
    for (size_t j = 0; j <= m; j++) dp[j] = j;
    for (size_t i = 1; i <= n; i++) {
        for (size_t j = 1; j <= m; j++) {
            size_t cost = (s[i - 1] == t[j - 1]) ? 0 : 1;
            size_t del = dp[(i - 1) * (m + 1) + j] + 1;
            size_t ins = dp[i * (m + 1) + (j - 1)] + 1;
            size_t sub = dp[(i - 1) * (m + 1) + (j - 1)] + cost;
            size_t v = del < ins ? del : ins;
            if (sub < v) v = sub;
            dp[i * (m + 1) + j] = v;
        }
    }
    size_t r = dp[n * (m + 1) + m];
    free(dp);
    return r;
}

size_t ts_canonical_distance(const TSNode *a, const TSNode *b) {
    if (!a && !b) return 0;
    char *sa = NULL, *sb = NULL;
    if (a && ts_encode(a, &sa, NULL) != TS_OK) return (size_t)-1;
    if (b && ts_encode(b, &sb, NULL) != TS_OK) { free(sa); return (size_t)-1; }
    size_t d = levenshtein(sa ? sa : "", sb ? sb : "");
    free(sa); free(sb);
    return d;
}
