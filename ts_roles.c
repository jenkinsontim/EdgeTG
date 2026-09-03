#include "ts_roles.h"
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void put_u32(uint8_t *p, uint32_t x) {
    p[0]=(uint8_t)x; p[1]=(uint8_t)(x>>8); p[2]=(uint8_t)(x>>16); p[3]=(uint8_t)(x>>24);
}
static uint32_t get_u32(const uint8_t *p) {
    return (uint32_t)p[0]|((uint32_t)p[1]<<8)|((uint32_t)p[2]<<16)|((uint32_t)p[3]<<24);
}

TSError ts_roles_create(const TSRole *roles, size_t count, uint32_t max_tag, TSRoleMap *out) {
    if (!out) return TS_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    if (count && !roles) return TS_ERR_INVALID_ARG;
    out->roles = count ? (TSRole *)calloc(count, sizeof(TSRole)) : NULL;
    if (count && !out->roles) return TS_ERR_OOM;
    if (count) memcpy(out->roles, roles, count * sizeof(TSRole));
    out->count = count;
    out->max_tag = max_tag;
    return TS_OK;
}

void ts_roles_free(TSRoleMap *m) {
    if (!m) return;
    free(m->roles);
    memset(m, 0, sizeof(*m));
}

bool ts_roles_validate(const TSNode *tree, const TSRoleMap *map) {
    if (!tree || !map) return false;
    size_t n = ts_count_nodes(tree);
    if (map->count != n) return false;
    bool *seen = (bool *)calloc(n, sizeof(bool));
    if (!seen) return false;
    for (size_t i = 0; i < map->count; i++) {
        size_t idx = map->roles[i].preorder_index;
        if (idx >= n || seen[idx]) { free(seen); return false; }
        if (map->roles[i].role_tag > map->max_tag) { free(seen); return false; }
        seen[idx] = true;
    }
    for (size_t i = 0; i < n; i++) if (!seen[i]) { free(seen); return false; }
    free(seen);
    return true;
}

TSError ts_roles_encode(const TSRoleMap *map, uint8_t **out, size_t *out_len) {
    if (!map || !out) return TS_ERR_INVALID_ARG;
    size_t total = 8;
    for (size_t i = 0; i < map->count; i++) {
        if (map->roles[i].metadata.len > UINT32_MAX) return TS_ERR_INVALID_ARG;
        total += 12 + map->roles[i].metadata.len;
    }
    uint8_t *buf = (uint8_t *)malloc(total ? total : 1);
    if (!buf) return TS_ERR_OOM;
    put_u32(buf, (uint32_t)map->count);
    put_u32(buf + 4, map->max_tag);
    size_t pos = 8;
    for (size_t i = 0; i < map->count; i++) {
        put_u32(buf + pos, (uint32_t)map->roles[i].preorder_index); pos += 4;
        put_u32(buf + pos, map->roles[i].role_tag); pos += 4;
        put_u32(buf + pos, (uint32_t)map->roles[i].metadata.len); pos += 4;
        if (map->roles[i].metadata.len)
            memcpy(buf + pos, map->roles[i].metadata.data, map->roles[i].metadata.len);
        pos += map->roles[i].metadata.len;
    }
    *out = buf;
    if (out_len) *out_len = total;
    return TS_OK;
}

TSError ts_roles_decode(const uint8_t *buf, size_t len, TSRoleMap *out) {
    if (!buf || !out || len < 8) return TS_ERR_INVALID_ARG;
    memset(out, 0, sizeof(*out));
    uint32_t count = get_u32(buf);
    uint32_t max_tag = get_u32(buf + 4);
    size_t pos = 8;
    TSRole *roles = count ? (TSRole *)calloc(count, sizeof(TSRole)) : NULL;
    if (count && !roles) return TS_ERR_OOM;
    for (uint32_t i = 0; i < count; i++) {
        if (len - pos < 12) { free(roles); return TS_ERR_INVALID_ARG; }
        roles[i].preorder_index = get_u32(buf + pos); pos += 4;
        roles[i].role_tag = get_u32(buf + pos); pos += 4;
        uint32_t mlen = get_u32(buf + pos); pos += 4;
        if (mlen > len - pos) { free(roles); return TS_ERR_INVALID_ARG; }
        if (mlen) {
            uint8_t *d = (uint8_t *)malloc(mlen);
            if (!d) { free(roles); return TS_ERR_OOM; }
            memcpy(d, buf + pos, mlen);
            roles[i].metadata.data = d;
            roles[i].metadata.len = mlen;
        }
        pos += mlen;
    }
    if (pos != len) {
        for (uint32_t i = 0; i < count; i++) free((void *)roles[i].metadata.data);
        free(roles); return TS_ERR_INVALID_ARG;
    }
    out->roles = roles;
    out->count = count;
    out->max_tag = max_tag;
    return TS_OK;
}
