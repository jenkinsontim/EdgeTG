/* mcu_executor.c -- the single, unified EdgeTG MCU.
 * Consumes the two-file wire artifacts that gateway.lua writes and produces
 * two reply artifacts that gateway.py decodes:
 *   argv[1] = wire_packet.bin   (topology, ASCII or 2-bit packed mode)
 *   argv[2] = wire_values.bin   (the ts_values_encode value blob)
 * Replies with:
 *   reply_packet.bin  (topology, re-encoded in the SAME mode as the request)
 *   reply_values.bin  (the reply value blob, via ts_values_encode)
 *
 * Execution model:
 *   1. Calibration pass  — applies +0.4 f32 offset to every float value
 *                          (the classic battery-powered "muscle" side).
 *   2. EWMA leaf pass    — walks the parsed tree; for every leaf node whose
 *                          positional value blob has length == num_features
 *                          (uint8_t binary features), runs ewma_leaf_predict()
 *                          and logs the result.  If a ground-truth label is
 *                          available (value blob length == num_features + 1,
 *                          with the last byte being the label), the leaf is
 *                          also updated via ewma_leaf_update().
 *                          Leaves are keyed by preorder index; one
 *                          ewma_leaf_t is kept for each leaf node.
 */
#include "ts_core.h"
#include "ts_layers.h"
#include "ts_packed.h"
#include "ewma_leaf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WIRE_FORMAT_ASCII  0x00
#define WIRE_FORMAT_PACKED 0x01

/* ------------------------------------------------------------------ helpers */

static float read_float(const uint8_t *b) {
    float f;
    memcpy(&f, b, 4);
    return f;
}

static int read_file(const char *path, uint8_t **out, size_t *out_len) {
    FILE *f = fopen(path, "rb");
    if (!f) return 0;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return 0; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return 0; }
    if (fseek(f, 0, SEEK_SET) != 0) { fclose(f); return 0; }
    uint8_t *buf = malloc((size_t)sz);
    if (!buf) { fclose(f); return 0; }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (got != (size_t)sz) { free(buf); return 0; }
    *out = buf;
    *out_len = (size_t)sz;
    return 1;
}

static int write_file(const char *path, const uint8_t *buf, size_t len) {
    FILE *f = fopen(path, "wb");
    if (!f) return 0;
    size_t got = fwrite(buf, 1, len, f);
    fclose(f);
    return got == len;
}

/* Decode the wire packet into an ASCII topology string (NULL-terminated).
 * Returns NULL (after printing REJECTED) if the format byte is unknown or
 * the payload is corrupt. */
static char *decode_wire_payload(const uint8_t *buf, size_t len,
                                 uint8_t *format_out) {
    if (len < 1) {
        fprintf(stderr, "MCU: REJECTED: empty wire packet.\n");
        return NULL;
    }
    uint8_t fmt = buf[0];
    char *out = NULL;

    if (fmt == WIRE_FORMAT_ASCII) {
        size_t str_len = len - 1;
        out = malloc(str_len + 1);
        if (!out) return NULL;
        memcpy(out, buf + 1, str_len);
        out[str_len] = '\0';
    } else if (fmt == WIRE_FORMAT_PACKED) {
        if (len < 2) {
            fprintf(stderr, "MCU: REJECTED: packed packet missing symbol count.\n");
            return NULL;
        }
        uint8_t symbol_count = buf[1];
        size_t expected = ((size_t)symbol_count + 3) / 4;
        if (len != 2 + expected) {
            fprintf(stderr, "MCU: REJECTED: packed packet length mismatch "
                            "(got %zu, expected %zu).\n", len, 2 + expected);
            return NULL;
        }
        out = malloc((size_t)symbol_count + 1);
        if (!out) return NULL;
        if (!ts_unpack(buf + 2, symbol_count, out)) {
            fprintf(stderr, "MCU: REJECTED: corrupted packed topology.\n");
            free(out);
            return NULL;
        }
    } else {
        fprintf(stderr, "MCU: REJECTED: unknown wire format byte 0x%02x.\n", fmt);
        return NULL;
    }

    *format_out = fmt;
    return out;
}

/* ------------------------------------------------------------------ EWMA leaf dispatch
 *
 * Walk the tree in preorder.  For each leaf node (child_count == 0):
 *   - If its positional value (values[preorder_index]) has length >= 1 and
 *     <= EWMA_MAX_FEATURES, treat those bytes as binary features (0 = off,
 *     non-zero = on) and call ewma_leaf_predict().
 *   - If value length == num_features + 1, the extra trailing byte is used
 *     as the ground-truth label and ewma_leaf_update() is called too.
 *
 * ewma_leaves is a parallel array indexed by the leaf's slot in the
 * leaf-order (not preorder); leaf_map[slot] = preorder index.
 *
 * For simplicity the executor allocates one EWMA leaf per leaf node and
 * keeps them in a flat array.  In a real deployment these would be
 * persisted across invocations (e.g. in FRAM).
 */

typedef struct {
    size_t     preorder_index;
    ewma_leaf_t clf;
} EWMALeafSlot;

/* Recursive preorder walk; fills slots[], returns number of leaf nodes. */
static size_t collect_ewma_leaves(const TSNode *node, size_t *preorder_idx,
                                  EWMALeafSlot *slots, size_t slot_cap,
                                  size_t slot_count)
{
    size_t my_idx = (*preorder_idx)++;
    if (node->child_count == 0) {
        /* Leaf node */
        if (slot_count < slot_cap) {
            slots[slot_count].preorder_index = my_idx;
            /* Initialise with 2 classes, EWMA_MAX_FEATURES features.
             * In production the configuration would come from a stored
             * manifest or role-map metadata. */
            ewma_leaf_init(&slots[slot_count].clf, 2, EWMA_MAX_FEATURES);
            slot_count++;
        }
    }
    for (size_t c = 0; c < node->child_count; c++)
        slot_count = collect_ewma_leaves(&node->children[c], preorder_idx,
                                         slots, slot_cap, slot_count);
    return slot_count;
}

static void run_ewma_dispatch(const TSNode *tree,
                              const TSValue *values, size_t val_count)
{
    /* Upper bound: a tree with N nodes has at most N leaves. */
    size_t node_count = ts_count_nodes(tree);
    EWMALeafSlot *slots = calloc(node_count, sizeof(EWMALeafSlot));
    if (!slots) {
        fprintf(stderr, "MCU: EWMA dispatch: OOM allocating leaf slots.\n");
        return;
    }

    size_t preorder_idx = 0;
    size_t leaf_count = collect_ewma_leaves(tree, &preorder_idx,
                                             slots, node_count, 0);

    printf("MCU: EWMA dispatch: %zu leaf node(s) found.\n", leaf_count);

    for (size_t s = 0; s < leaf_count; s++) {
        size_t pos = slots[s].preorder_index;
        if (pos >= val_count) {
            printf("MCU:   leaf[%zu] (preorder %zu): no value blob, skipping.\n",
                   s, pos);
            continue;
        }

        const TSValue *v = &values[pos];
        uint8_t nf = slots[s].clf.num_features;  /* EWMA_MAX_FEATURES */

        /* Determine mode from value length:
         *   len == nf        → predict only
         *   len == nf + 1    → predict then update with trailing label byte
         *   otherwise        → not a feature vector; skip
         */
        if (v->len == 0 || v->len > (size_t)(nf + 1)) {
            printf("MCU:   leaf[%zu] (preorder %zu): value len %zu not a "
                   "feature vector (expected %u or %u), skipping.\n",
                   s, pos, v->len, nf, (unsigned)(nf + 1));
            continue;
        }

        /* Build a zero-padded feature array of exactly num_features bytes. */
        uint8_t features[EWMA_MAX_FEATURES] = {0};
        size_t feat_len = (v->len == (size_t)(nf + 1)) ? (size_t)nf : v->len;
        for (size_t f = 0; f < feat_len; f++)
            features[f] = v->data[f] ? 1u : 0u;  /* binarise */

        uint8_t pred = ewma_leaf_predict(&slots[s].clf, features);

        if (v->len == (size_t)(nf + 1)) {
            /* Ground-truth label is the last byte. */
            uint8_t label = v->data[nf];
            ewma_leaf_update(&slots[s].clf, features, label);
            uint8_t err_rate = ewma_leaf_error_rate(&slots[s].clf);
            printf("MCU:   leaf[%zu] (preorder %zu): pred=%u label=%u "
                   "err_rate=%u/255\n", s, pos, pred, label, err_rate);
        } else {
            printf("MCU:   leaf[%zu] (preorder %zu): pred=%u (predict-only)\n",
                   s, pos, pred);
        }
    }

    free(slots);
}

/* ------------------------------------------------------------------ main */

int main(int argc, char **argv) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <wire_packet.bin> <wire_values.bin>\n", argv[0]);
        return 1;
    }

    uint8_t *wire = NULL, *vals = NULL, *reply_wire = NULL, *reply_vals = NULL;
    size_t wire_len = 0, vlen = 0, reply_wire_len = 0, reply_vals_len = 0;
    char *topo = NULL;
    TSNode *tree = NULL;
    TSValue *values = NULL;
    size_t val_count = 0;
    int rc = 1;

    if (!read_file(argv[1], &wire, &wire_len)) {
        fprintf(stderr, "MCU: could not read %s.\n", argv[1]);
        goto done;
    }
    if (!read_file(argv[2], &vals, &vlen)) {
        fprintf(stderr, "MCU: could not read %s.\n", argv[2]);
        goto done;
    }

    uint8_t incoming_mode = 0;
    topo = decode_wire_payload(wire, wire_len, &incoming_mode);
    if (!topo) { rc = 2; goto done; }
    printf("MCU: wire packet mode=%s, topology=\"%s\".\n",
           incoming_mode == WIRE_FORMAT_PACKED ? "packed" : "ascii", topo);

    if (ts_parse(topo, 16, &tree) != TS_OK) {
        fprintf(stderr, "MCU: REJECTED: malformed topology.\n");
        rc = 3;
        goto done;
    }

    if (ts_values_decode(vals, vlen, &values, &val_count) != TS_OK) {
        fprintf(stderr, "MCU: REJECTED: could not decode value blob.\n");
        rc = 4;
        goto done;
    }
    printf("MCU: decoded %zu value(s).\n", val_count);

    /* --- Pass 1: calibration (classic +0.4 f32 drift). --- */
    for (size_t i = 0; i < val_count; i++) {
        if (values[i].len == sizeof(float)) {
            float f = read_float(values[i].data);
            f += 0.4f;
            memcpy((void *)values[i].data, &f, sizeof(float));
            printf("MCU:   position %zu: float %.3f -> %.3f (offset +0.4)\n",
                   i, f - 0.4f, f);
        }
    }

    /* --- Pass 2: EWMA leaf dispatch. --- */
    run_ewma_dispatch(tree, values, val_count);

    /* Encode the reply value blob with ts_values_encode. */
    if (ts_values_encode(values, val_count, &reply_vals, &reply_vals_len) != TS_OK) {
        fprintf(stderr, "MCU: REJECTED: could not encode reply values.\n");
        rc = 5;
        goto done;
    }

    /* Build reply packet: [format_byte][...] exactly matching the incoming mode. */
    {
        size_t n = strlen(topo);
        if (incoming_mode == WIRE_FORMAT_PACKED && n > 255) {
            fprintf(stderr, "MCU: REJECTED: reply topology too large.\n");
            rc = 5; goto done;
        }
        if (incoming_mode == WIRE_FORMAT_PACKED) {
            reply_wire = malloc(2 + ((n + 3) / 4));
            if (!reply_wire) { rc = 5; goto done; }
            reply_wire[0] = WIRE_FORMAT_PACKED;
            reply_wire[1] = (uint8_t)n;
            size_t packed = ts_pack(topo, n, reply_wire + 2);
            reply_wire_len = 2 + packed;
        } else {
            reply_wire = malloc(1 + n);
            if (!reply_wire) { rc = 5; goto done; }
            reply_wire[0] = WIRE_FORMAT_ASCII;
            memcpy(reply_wire + 1, topo, n);
            reply_wire_len = 1 + n;
        }
    }

    if (!write_file("reply_packet.bin", reply_wire, reply_wire_len) ||
        !write_file("reply_values.bin", reply_vals, reply_vals_len)) {
        fprintf(stderr, "MCU: could not write reply artifacts.\n");
        rc = 6;
        goto done;
    }

    printf("MCU: wrote reply_packet.bin (%zu bytes, mode=%s) and "
           "reply_values.bin (%zu bytes).\n",
           reply_wire_len,
           incoming_mode == WIRE_FORMAT_PACKED ? "packed" : "ascii",
           reply_vals_len);
    rc = 0;

done:
    free(reply_wire);
    free(reply_vals);
    ts_values_free(values, val_count);
    ts_free_tree(tree); free(tree);
    free(topo);
    free(vals);
    free(wire);
    return rc;
}