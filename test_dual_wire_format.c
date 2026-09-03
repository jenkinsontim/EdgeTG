#include "ts_core.h"
#include "ts_packed.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

/* Wire format: [format_byte][payload]
 *   format_byte 0x00 -> payload is ASCII text, NUL-terminated
 *   format_byte 0x01 -> payload is [uint16_t symbol_count][packed bytes]
 * This is the piece mcu_executor.c needs: detect which mode arrived,
 * decode to the same ASCII form either way, then hand off to ts_parse
 * exactly as before. Nothing downstream needs to know which mode was used. */

#define WIRE_FORMAT_ASCII  0x00
#define WIRE_FORMAT_PACKED 0x01

/* Returns a freshly malloc'd ASCII glyph string on success (caller frees),
 * or NULL on any detection/decoding failure. */
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
        if (len < 2) return NULL; /* need at least the 1-byte count */
        uint8_t symbol_count = buf[1]; /* max 255 symbols -- plenty for real configs */
        size_t expected_packed_bytes = (symbol_count + 3) / 4;
        if (len != 2 + expected_packed_bytes) return NULL; /* size mismatch -- reject, don't guess */
        char *out = malloc((size_t)symbol_count + 1);
        if (!out) return NULL;
        if (!ts_unpack(buf + 2, symbol_count, out)) { free(out); return NULL; }
        return out;
    }

    return NULL; /* unknown format byte -- reject rather than guess */
}

int main(int argc, char **argv) {
    if (argc > 1 && strcmp(argv[1], "--from-file") == 0) {
        /* Real integration mode: decode whatever gateway_encode.lua actually wrote. */
        FILE *f = fopen("/tmp/wire_packet.bin", "rb");
        if (!f) { fprintf(stderr, "no wire_packet.bin found\n"); return 1; }
        uint8_t buf[256];
        size_t n = fread(buf, 1, sizeof(buf), f);
        fclose(f);
        printf("[MCU] Read %zu real bytes from the Lua Gateway's actual output\n", n);
        char *decoded = decode_wire_payload(buf, n);
        if (!decoded) { printf("[MCU] REJECTED: could not decode wire packet\n"); return 1; }
        printf("[MCU] Decoded topology: %s\n", decoded);
        TSNode *tree = NULL;
        TSError e = ts_parse(decoded, 64, &tree);
        printf("[MCU] Parse result: %d (0 = OK)\n", e);
        if (e == TS_OK) {
            char *re = NULL;
            ts_encode(tree, &re, NULL);
            printf("[MCU] Re-encoded (should match Lua's topology exactly): %s\n", re);
            free(re);
            ts_free_tree(tree); free(tree);
        }
        free(decoded);
        return 0;
    }
    /* Build a real tree, then simulate BOTH wire formats arriving at the MCU. */
    TSNode *leaf1 = calloc(1, sizeof(TSNode));
    TSNode *leaf2 = calloc(1, sizeof(TSNode));
    TSNode *tree = calloc(1, sizeof(TSNode));
    tree->children = calloc(2, sizeof(TSNode));
    tree->children[0] = *leaf1; tree->children[1] = *leaf2;
    tree->child_count = 2;
    free(leaf1); free(leaf2);

    char *ascii = NULL; size_t ascii_len = 0;
    ts_encode(tree, &ascii, &ascii_len);
    printf("Original tree encodes to: %s (%zu symbols)\n\n", ascii, ascii_len);

    /* --- Simulate an ASCII-mode wire packet arriving --- */
    uint8_t ascii_packet[64];
    ascii_packet[0] = WIRE_FORMAT_ASCII;
    memcpy(ascii_packet + 1, ascii, ascii_len);
    size_t ascii_packet_len = 1 + ascii_len;

    char *decoded1 = decode_wire_payload(ascii_packet, ascii_packet_len);
    printf("[MCU] ASCII-mode packet (%zu bytes on wire) -> decoded: %s -> matches: %s\n",
           ascii_packet_len, decoded1 ? decoded1 : "(NULL)",
           decoded1 && strcmp(decoded1, ascii) == 0 ? "YES" : "NO");

    /* --- Simulate a PACKED-mode wire packet arriving --- */
    uint8_t packed_data[64];
    size_t packed_len = ts_pack(ascii, ascii_len, packed_data);
    uint8_t packed_packet[64];
    packed_packet[0] = WIRE_FORMAT_PACKED;
    packed_packet[1] = (uint8_t)ascii_len; /* 1-byte count, max 255 symbols */
    memcpy(packed_packet + 2, packed_data, packed_len);
    size_t packed_packet_len = 2 + packed_len;

    char *decoded2 = decode_wire_payload(packed_packet, packed_packet_len);
    printf("[MCU] PACKED-mode packet (%zu bytes on wire) -> decoded: %s -> matches: %s\n",
           packed_packet_len, decoded2 ? decoded2 : "(NULL)",
           decoded2 && strcmp(decoded2, ascii) == 0 ? "YES" : "NO");

    printf("\nWire savings for THIS packet: ASCII mode = %zu bytes, PACKED mode = %zu bytes (%.2fx)\n",
           ascii_packet_len, packed_packet_len, (double)ascii_packet_len / packed_packet_len);

    /* --- Reject an unknown format byte rather than guess --- */
    uint8_t bad_packet[4] = {0x99, 0,0,0};
    char *decoded3 = decode_wire_payload(bad_packet, 4);
    printf("Unknown format byte correctly rejected: %s\n", decoded3 == NULL ? "YES" : "NO");

    /* Both decoded strings feed the SAME downstream parser, unchanged. */
    TSNode *parsed_from_ascii_mode = NULL;
    ts_parse(decoded1, 64, &parsed_from_ascii_mode);
    TSNode *parsed_from_packed_mode = NULL;
    ts_parse(decoded2, 64, &parsed_from_packed_mode);
    char *re1 = NULL, *re2 = NULL;
    ts_encode(parsed_from_ascii_mode, &re1, NULL);
    ts_encode(parsed_from_packed_mode, &re2, NULL);
    printf("Both wire modes parse to the IDENTICAL tree downstream: %s\n",
           strcmp(re1, re2) == 0 ? "YES" : "NO");

    free(ascii); free(decoded1); free(decoded2); free(re1); free(re2);
    ts_free_tree(tree); free(tree);
    ts_free_tree(parsed_from_ascii_mode); free(parsed_from_ascii_mode);
    ts_free_tree(parsed_from_packed_mode); free(parsed_from_packed_mode);
    return 0;
}
