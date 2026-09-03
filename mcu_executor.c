/* mcu_executor.c -- the single, unified EdgeTG MCU.
 * Consumes the two-file wire artifacts that gateway.lua writes and produces
 * two reply artifacts that gateway.py decodes:
 *   argv[1] = wire_packet.bin   (topology, ASCII or 2-bit packed mode)
 *   argv[2] = wire_values.bin   (the ts_values_encode value blob)
 * Replies with:
 *   reply_packet.bin  (topology, re-encoded in the SAME mode as the request)
 *   reply_values.bin  (the reply value blob, via ts_values_encode)
 *
 * This is the classic battery-powered "muscle" side: it decodes, executes
 * positionally (here a +0.4f calibration drift on the float value), and
 * re-encodes, all without touching the ts_packed grammar module.
 */
#include "ts_core.h"
#include "ts_layers.h"
#include "ts_packed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define WIRE_FORMAT_ASCII  0x00
#define WIRE_FORMAT_PACKED 0x01

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
    printf("MCU: wire packet mode=%s, topology=\"" "%s" "\".\n",
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

    /* "EXECUTE" positionally: apply the +0.4f calibration drift to the float
     * value (battery-backed "muscle" proves it is really processing). */
    for (size_t i = 0; i < val_count; i++) {
        if (values[i].len == sizeof(float)) {
            float f = read_float(values[i].data);
            f += 0.4f;
            memcpy((void *)values[i].data, &f, sizeof(float));
            printf("MCU:   position %zu: float %.3f -> %.3f (offset +0.4)\n", i, f - 0.4f, f);
        }
    }

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

    printf("MCU: wrote reply_packet.bin (%zu bytes, mode=%s) and reply_values.bin (%zu bytes).\n",
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