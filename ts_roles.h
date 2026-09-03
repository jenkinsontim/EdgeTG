#ifndef TS_ROLES_H
#define TS_ROLES_H

#include "ts_core.h"
#include "ts_layers.h"
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Extended role metadata — external to the _/\ string.
 * role_tag is a firmware-defined enum; metadata is an opaque byte block. */

typedef struct {
    size_t   preorder_index;
    uint32_t role_tag;
    TSValue  metadata;      /* optional; data may be NULL, len 0 */
} TSRole;

typedef struct {
    TSRole *roles;
    size_t  count;
    uint32_t max_tag;       /* tags must be in [0, max_tag] */
} TSRoleMap;

/* Build a role map (copies the role array; metadata data pointers are not deep-copied). */
TSError ts_roles_create(const TSRole *roles, size_t count, uint32_t max_tag, TSRoleMap *out);
void    ts_roles_free(TSRoleMap *m);

/* Validate: every node index 0..n-1 has exactly one role, tags in range. */
bool ts_roles_validate(const TSNode *tree, const TSRoleMap *map);

/* Binary serialization (separate artifact, like Layer 1 values).
 * Format: u32 count, u32 max_tag, then count × (u32 index, u32 tag, u32 meta_len, bytes). */
TSError ts_roles_encode(const TSRoleMap *map, uint8_t **out, size_t *out_len);
TSError ts_roles_decode(const uint8_t *buf, size_t len, TSRoleMap *out);
#endif
