#ifndef TS_PACKED_H
#define TS_PACKED_H
#include <stddef.h>
#include <stdint.h>

/* 2-bit-per-symbol packing for the three-glyph grammar: _ = 0, / = 1, \ = 2.
 * (Value 3 is unused -- reserved, costs nothing extra.) 4 symbols per byte. */

/* Packs `count` glyph characters from `glyphs` into `out`, which must have
 * room for at least (count + 3) / 4 bytes. Returns the number of bytes
 * written, or 0 on an invalid input character. */
size_t ts_pack(const char *glyphs, size_t count, uint8_t *out);

/* Unpacks exactly `count` symbols from `packed` back into `out` (which must
 * have room for count+1 bytes, for a NUL terminator). The caller must know
 * `count` -- 2-bit packing has no self-terminating marker, unlike the
 * ASCII string form. Returns 1 on success, 0 on invalid packed data. */
int ts_unpack(const uint8_t *packed, size_t count, char *out);

#endif
