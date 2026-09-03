#include "ts_core.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdbool.h>
#include <stdint.h>

/* Recursion depth is strictly bounded by the parser's `max_depth` limit (default 1024), preventing stack overflow on valid trees. */
size_t ts_count_nodes(const TSNode *t) {
    if (!t) return 0;
    size_t c = 1;
    for (size_t i = 0; i < t->child_count; i++) c += ts_count_nodes(&t->children[i]);
    return c;
}

/* Recursion depth is strictly bounded by the parser's `max_depth` limit (default 1024), preventing stack overflow on valid trees. */
size_t ts_depth(const TSNode *t) {
    if (!t || t->child_count == 0) return 1;
    size_t m = 0;
    for (size_t i = 0; i < t->child_count; i++) {
        size_t d = ts_depth(&t->children[i]);
        if (d > m) m = d;
    }
    return 1 + m;
}
void ts_free_tree(TSNode *t) {
    if (!t) return;
    for (size_t i = 0; i < t->child_count; i++) ts_free_tree(&t->children[i]);
    free(t->children);
    t->children = NULL; t->child_count = 0;
}
void ts_free_forest(TSNode *f, size_t n) {
    if (!f) return;
    for (size_t i = 0; i < n; i++) ts_free_tree(&f[i]);
    free(f);
}

/* FIX 1: was `ts_clone(&src->children[i], &n->children[i])`, passing a TSNode*
 * where TSNode** is required (a genuine type error that fails to compile
 * under -Werror). Clone into a temporary heap node, then shallow-copy its
 * contents into the array slot and free the temporary shell. */
TSError ts_clone(const TSNode *src, TSNode **out) {
    if (!src || !out) return TS_ERR_INVALID_ARG;
    TSNode *n = calloc(1, sizeof(*n));
    if (!n) return TS_ERR_OOM;
    n->child_count = src->child_count;
    if (n->child_count) {
        n->children = calloc(n->child_count, sizeof(TSNode));
        if (!n->children) { free(n); return TS_ERR_OOM; }
        for (size_t i = 0; i < n->child_count; i++) {
            TSNode *tmp = NULL;
            TSError e = ts_clone(&src->children[i], &tmp);
            if (e != TS_OK) { ts_free_tree(n); free(n); return e; }
            n->children[i] = *tmp;
            free(tmp);
        }
    }
    *out = n; return TS_OK;
}

static TSError parse_rec(const char *s, size_t *pos, size_t len, size_t depth, size_t max_depth, TSNode *out) {
    if (depth > max_depth) return TS_ERR_DEPTH;
    if (*pos >= len || s[*pos] != '_') return TS_ERR_EXPECTED_NODE;
    (*pos)++;
    if (*pos < len && s[*pos] == '/') {
        (*pos)++;
        size_t cap = 4, count = 0;
        TSNode *ch = malloc(cap * sizeof(TSNode));
        if (!ch) return TS_ERR_OOM;
        while (*pos < len && s[*pos] != '\\') {
            if (count == cap) {
                /* Sane real-world bound instead of SIZE_MAX-adjacent clamping --
                 * no legitimate config needs anywhere near this many children on
                 * one node, and this avoids GCC's -Walloc-size-larger-than
                 * flagging a theoretical multi-exabyte allocation path. */
                if (cap >= (1u << 20)) { for (size_t i = 0; i < count; i++) ts_free_tree(&ch[i]); free(ch); return TS_ERR_OOM; }
                size_t newcap = cap * 2;
                TSNode *tmp = realloc(ch, newcap * sizeof(TSNode));
                if (!tmp) { for (size_t i = 0; i < count; i++) ts_free_tree(&ch[i]); free(ch); return TS_ERR_OOM; }
                ch = tmp; cap = newcap;
            }
            memset(&ch[count], 0, sizeof(TSNode));
            TSError e = parse_rec(s, pos, len, depth + 1, max_depth, &ch[count]);
            if (e != TS_OK) { for(size_t i=0; i<count; i++) ts_free_tree(&ch[i]); free(ch); return e; }
            count++;
        }
        if (*pos >= len || s[*pos] != '\\') { for(size_t i=0; i<count; i++) ts_free_tree(&ch[i]); free(ch); return TS_ERR_UNCLOSED_CHILDREN; }
        if (count == 0) { free(ch); return TS_ERR_EMPTY_CHILDREN; }
        (*pos)++;
        out->children = ch; out->child_count = count;
    }
    return TS_OK;
}
TSError ts_parse(const char *s, size_t max_depth, TSNode **out) {
    if (!s || !out) return TS_ERR_INVALID_ARG;
    TSNode *n = calloc(1, sizeof(TSNode));
    if (!n) return TS_ERR_OOM;
    size_t pos = 0, len = strlen(s);
    TSError e = parse_rec(s, &pos, len, 1, max_depth, n);
    if (e != TS_OK) { free(n); return e; }
    if (pos != len) { ts_free_tree(n); free(n); return TS_ERR_TRAILING; }
    *out = n; return TS_OK;
}
TSError ts_parse_forest(const char *s, size_t max_depth, TSNode **out, size_t *out_count) {
    if (!s || !out || !out_count) return TS_ERR_INVALID_ARG;
    size_t len = strlen(s), pos = 0, cap = 4, count = 0;
    TSNode *f = malloc(cap * sizeof(TSNode));
    if (!f) return TS_ERR_OOM;
    while (pos < len) {
        if (count == cap) {
            if (cap >= (1u << 20)) { for (size_t i = 0; i < count; i++) ts_free_tree(&f[i]); free(f); return TS_ERR_OOM; }
            size_t newcap = cap * 2;
            /* FIX 2b: check realloc before overwriting the pointer. */
            TSNode *tmp = realloc(f, newcap * sizeof(TSNode));
            if (!tmp) { for (size_t i = 0; i < count; i++) ts_free_tree(&f[i]); free(f); return TS_ERR_OOM; }
            f = tmp; cap = newcap;
        }
        memset(&f[count], 0, sizeof(TSNode));
        TSError e = parse_rec(s, &pos, len, 1, max_depth, &f[count]);
        if (e != TS_OK) { for(size_t i=0; i<count; i++) ts_free_tree(&f[i]); free(f); return e; }
        count++;
    }
    *out = f; *out_count = count; return TS_OK;
}

/* FIX 2c: encode_rec now reports failure instead of silently continuing
 * with a NULL buffer after a failed realloc. */
static bool encode_rec(const TSNode *t, char **buf, size_t *pos, size_t *cap) {
    if (*pos + 2 >= *cap) {
        size_t newcap = (*cap > SIZE_MAX / 2) ? SIZE_MAX : *cap * 2;
        char *tmp = realloc(*buf, newcap);
        if (!tmp) return false;
        *buf = tmp; *cap = newcap;
    }
    (*buf)[(*pos)++] = '_';
    if (t->child_count > 0) {
        (*buf)[(*pos)++] = '/';
        for (size_t i = 0; i < t->child_count; i++) {
            if (!encode_rec(&t->children[i], buf, pos, cap)) return false;
        }
        if (*pos + 1 >= *cap) {
            size_t newcap = (*cap > SIZE_MAX / 2) ? SIZE_MAX : *cap * 2;
            char *tmp = realloc(*buf, newcap);
            if (!tmp) return false;
            *buf = tmp; *cap = newcap;
        }
        (*buf)[(*pos)++] = '\\';
    }
    return true;
}
TSError ts_encode(const TSNode *tree, char **out, size_t *out_len) {
    if (!tree || !out) return TS_ERR_INVALID_ARG;
    size_t cap = 32, pos = 0;
    char *buf = malloc(cap);
    if (!buf) return TS_ERR_OOM;
    if (!encode_rec(tree, &buf, &pos, &cap)) { free(buf); return TS_ERR_OOM; }
    if (pos >= cap) { char *tmp = realloc(buf, pos + 1); if (!tmp) { free(buf); return TS_ERR_OOM; } buf = tmp; }
    buf[pos] = '\0';
    *out = buf; if (out_len) *out_len = pos;
    return TS_OK;
}
TSError ts_encode_forest(const TSNode *forest, size_t count, char **out, size_t *out_len) {
    if (!out) return TS_ERR_INVALID_ARG;
    size_t cap = 64, pos = 0;
    char *buf = malloc(cap);
    if (!buf) return TS_ERR_OOM;
    for (size_t i = 0; i < count; i++) {
        if (!encode_rec(&forest[i], &buf, &pos, &cap)) { free(buf); return TS_ERR_OOM; }
    }
    if (pos >= cap) { char *tmp = realloc(buf, pos + 1); if (!tmp) { free(buf); return TS_ERR_OOM; } buf = tmp; }
    buf[pos] = '\0';
    *out = buf; if (out_len) *out_len = pos;
    return TS_OK;
}
