#include "ts_packed.h"

static int glyph_to_code(char c) {
    if (c == '_') return 0;
    if (c == '/') return 1;
    if (c == '\\') return 2;
    return -1;
}
static char code_to_glyph(int code) {
    switch (code) {
        case 0: return '_';
        case 1: return '/';
        case 2: return '\\';
        default: return '?';
    }
}

size_t ts_pack(const char *glyphs, size_t count, uint8_t *out) {
    size_t out_len = (count + 3) / 4;
    for (size_t i = 0; i < out_len; i++) out[i] = 0;
    for (size_t i = 0; i < count; i++) {
        int code = glyph_to_code(glyphs[i]);
        if (code < 0) return 0; /* invalid symbol */
        size_t byte_idx = i / 4;
        int shift = (i % 4) * 2;
        out[byte_idx] |= (uint8_t)(code << shift);
    }
    return out_len;
}

int ts_unpack(const uint8_t *packed, size_t count, char *out) {
    for (size_t i = 0; i < count; i++) {
        size_t byte_idx = i / 4;
        int shift = (i % 4) * 2;
        int code = (packed[byte_idx] >> shift) & 0x03;
        if (code == 3) return 0; /* unused code point -- corrupted data */
        out[i] = code_to_glyph(code);
    }
    out[count] = '\0';
    return 1;
}
