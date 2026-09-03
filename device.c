#include "device.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <math.h>

static size_t max_width(const TSNode *n) {
    size_t w = n->child_count;
    for (size_t i = 0; i < n->child_count; i++) {
        size_t cw = max_width(&n->children[i]);
        if (cw > w) w = cw;
    }
    return w;
}

/* Firmware A — Ordered pipeline: prefers deep, narrow pipelines (sequential stages).
 * Score = depth * 10 - width*2 - nodes*0.1. Legal if depth >= 2 and width <= 3. */
static DeviceScore eval_A(const TSNode *t) {
    DeviceScore s = {0};
    s.nodes = ts_count_nodes(t);
    s.depth = ts_depth(t);
    s.width = max_width(t);
    s.legal = (s.depth >= 2 && s.width <= 3);
    s.score = (double)s.depth * 10.0 - (double)s.width * 2.0 - (double)s.nodes * 0.1;
    if (!s.legal) s.score -= 100.0;
    return s;
}

/* Firmware B — Task tree: prefers balanced width for parallelism, penalizes deep latency.
 * latency ≈ depth; score = width*5 - latency*3. Legal if nodes <= 20 and depth <= 8. */
static DeviceScore eval_B(const TSNode *t) {
    DeviceScore s = {0};
    s.nodes = ts_count_nodes(t);
    s.depth = ts_depth(t);
    s.width = max_width(t);
    s.latency = (double)s.depth;
    s.legal = (s.nodes <= 20 && s.depth <= 8);
    s.score = (double)s.width * 5.0 - s.latency * 3.0;
    if (!s.legal) s.score -= 50.0;
    return s;
}

/* Firmware C — Modular robot: each node is a module; power = nodes * 1.5 + width*2.
 * Prefers moderate size under a power budget of 40. Legal if power <= 40. */
static DeviceScore eval_C(const TSNode *t) {
    DeviceScore s = {0};
    s.nodes = ts_count_nodes(t);
    s.depth = ts_depth(t);
    s.width = max_width(t);
    s.power = (double)s.nodes * 1.5 + (double)s.width * 2.0;
    s.legal = (s.power <= 40.0);
    s.score = 40.0 - s.power;   /* remaining budget is the score */
    if (!s.legal) s.score -= 30.0;
    return s;
}

DeviceScore device_evaluate(const TSNode *tree, FirmwareId fw) {
    if (!tree) {
        DeviceScore z = {0};
        return z;
    }
    switch (fw) {
        case FW_A_ORDERED_PIPELINE: return eval_A(tree);
        case FW_B_TASK_TREE:        return eval_B(tree);
        case FW_C_MODULAR_ROBOT:    return eval_C(tree);
        default: {
            DeviceScore z = {0};
            return z;
        }
    }
}

void device_store_init(DeviceStore *s) {
    if (!s) return;
    memset(s, 0, sizeof(*s));
}

void device_store_free(DeviceStore *s) {
    if (!s) return;
    for (size_t i = 0; i < s->count; i++)
        free(s->entries[i].canonical);
    s->count = 0;
}

bool device_store_insert(DeviceStore *s, const TSNode *tree, size_t generation) {
    if (!s || !tree || s->count >= DEVICE_STORE_CAP) return false;
    char *canon = NULL;
    if (ts_encode(tree, &canon, NULL) != TS_OK) return false;
    for (size_t i = 0; i < s->count; i++) {
        if (strcmp(s->entries[i].canonical, canon) == 0) {
            free(canon);
            return false;   /* already present */
        }
    }
    s->entries[s->count].canonical = canon;
    s->entries[s->count].gen = generation;
    s->count++;
    return true;
}

size_t device_store_count(const DeviceStore *s) {
    return s ? s->count : 0;
}
