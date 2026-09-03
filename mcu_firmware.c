/* mcu_firmware.c -- the "battery-powered muscle" side.
 * Reads the real dual-format wire packet ([format_byte][...]) that
 * gateway_encode.lua actually writes, using ts_packed.c to decode when
 * packed mode is used. Replies in packed format too -- genuinely
 * packed both directions, not just outbound. */
#include "ts_core.h"
#include "ts_packed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define FIRMWARE_MANIFEST_VERSION 2
#define FIRMWARE_EXPECTED_ARITY 2
#define WIRE_FORMAT_ASCII  0x00
#define WIRE_FORMAT_PACKED 0x01

static float read_float(const uint8_t *b) {
    float f;
    memcpy(&f, b, 4);
    return f;
}

static char* decode_wire_payload(const uint8_t *buf, size_t len) {
    if (len < 1) return NULL;
    uint8_t fmt = buf[0];
    if (fmt == WIRE_FORMAT_ASCII) {
        size_t str_len = len - 1;
        char *out = malloc(str_len + 1);
        if (!out) return NULL;
        memcpy(out, buf + 1, str_len);
        out[str_len] = '\0';
        return out;
    }
    if (fmt == WIRE_FORMAT_PACKED) {
        if (len < 2) return NULL;
        uint8_t symbol_count = buf[1];
        size_t expected_packed_bytes = ((size_t)symbol_count + 3) / 4;
        if (len != 2 + expected_packed_bytes) return NULL;
        char *out = malloc((size_t)symbol_count + 1);
        if (!out) return NULL;
        if (!ts_unpack(buf + 2, symbol_count, out)) { free(out); return NULL; }
        return out;
    }
    return NULL;
}

static size_t encode_wire_packed(const char *topo, uint8_t *out) {
    size_t len = strlen(topo);
    if (len > 255) return 0;
    out[0] = WIRE_FORMAT_PACKED;
    out[1] = (uint8_t)len;
    return 2 + ts_pack(topo, len, out + 2);
}

int main(int argc, char **argv) {
    if (argc < 4) {
        fprintf(stderr, "usage: %s <wire_packet_file> <values_file> <manifest_version>\n", argv[0]);
        return 1;
    }

    FILE *tf = fopen(argv[1], "rb");
    uint8_t wire_buf[256];
    size_t wire_len = fread(wire_buf, 1, sizeof(wire_buf), tf);
    fclose(tf);

    FILE *vf = fopen(argv[2], "rb");
    uint8_t vals[256];
    size_t vlen = fread(vals, 1, sizeof(vals), vf);
    fclose(vf);

    int incoming_manifest_version = atoi(argv[3]);

    printf("[MCU] Firmware built for manifest v%d, expects arity %d\n",
           FIRMWARE_MANIFEST_VERSION, FIRMWARE_EXPECTED_ARITY);
    printf("[MCU] Received %zu-byte wire packet (mode byte 0x%02x), manifest v%d claimed\n",
           wire_len, wire_buf[0], incoming_manifest_version);

    if (incoming_manifest_version != FIRMWARE_MANIFEST_VERSION) {
        printf("[MCU] REJECTED: manifest version mismatch (got v%d, firmware speaks v%d).\n",
               incoming_manifest_version, FIRMWARE_MANIFEST_VERSION);
        return 2;
    }

    char *topo = decode_wire_payload(wire_buf, wire_len);
    if (!topo) {
        printf("[MCU] REJECTED: could not decode wire packet (bad format or corrupt data)\n");
        return 3;
    }
    printf("[MCU] Decoded topology: %s\n", topo);

    TSNode *tree = NULL;
    TSError e = ts_parse(topo, 64, &tree);
    if (e != TS_OK) {
        printf("[MCU] REJECTED: malformed topology, parse error %d\n", e);
        free(topo);
        return 3;
    }

    size_t arity = tree->child_count;
    if (arity != FIRMWARE_EXPECTED_ARITY || vlen != 5) {
        printf("[MCU] REJECTED: shape/value-size mismatch. Expected arity %d + 5 value bytes, got arity %zu + %zu bytes.\n",
               FIRMWARE_EXPECTED_ARITY, arity, vlen);
        free(topo); ts_free_tree(tree); free(tree);
        return 4;
    }

    printf("[MCU] Shape and manifest version accepted. Executing positionally:\n");

    int valve_cmd = vals[0];
    float temp_reading = read_float(&vals[1]);
    printf("[MCU]   values[0] (positional) = %d -> setting valve GPIO\n", valve_cmd);
    printf("[MCU]   values[1] (positional) = %.2f -> read as temperature setpoint\n", temp_reading);

    int valve_actual_state = valve_cmd;
    float sensor_actual_temp = temp_reading + 0.4f;

    TSNode reply;
    reply.child_count = 2;
    reply.children = calloc(2, sizeof(TSNode));
    reply.children[0].child_count = 0; reply.children[0].children = NULL;
    reply.children[1].child_count = 0; reply.children[1].children = NULL;

    char *reply_topo = NULL; size_t reply_topo_len = 0;
    ts_encode(&reply, &reply_topo, &reply_topo_len);

    uint8_t reply_vals[5];
    reply_vals[0] = (uint8_t)valve_actual_state;
    memcpy(&reply_vals[1], &sensor_actual_temp, 4);

    uint8_t reply_wire[64];
    size_t reply_wire_len = encode_wire_packed(reply_topo, reply_wire);

    printf("[MCU] Reply topology: %s -> packed reply wire packet: %zu bytes\n", reply_topo, reply_wire_len);
    printf("[MCU] Sleeping. Total awake compute: microseconds, not milliseconds.\n");

    FILE *rt = fopen("/tmp/reply_wire_packet.bin", "wb"); fwrite(reply_wire, 1, reply_wire_len, rt); fclose(rt);
    FILE *rv = fopen("/tmp/reply_values.bin", "wb"); fwrite(reply_vals, 1, 5, rv); fclose(rv);

    free(topo);
    free(reply.children);
    free(reply_topo);
    ts_free_tree(tree); free(tree);
    return 0;
}
