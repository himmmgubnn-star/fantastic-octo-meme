/*
 * shadercache.c — persistent shader pipeline cache.
 *
 * On-disk layout:
 *   header   CELLAR_SHADERCACHE_MAGIC | version | env{gpu,driver,api,flags}
 *            | entry_count
 *   entry    key(u64) | size(u32) | blob[size]
 *
 * On open: if the header's env differs from the requested env, the file is
 * truncated and recreated (invalidation). Entries are appended; lookup scans
 * forward from the file start (cache sizes are typically small, and the API is
 * used by a single thread).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/shadercache.h"

static int le_write(FILE *fp, const void *p, size_t n) { return fwrite(p, 1, n, fp) == n; }

static cellar_status_t write_env(FILE *fp, const cellar_shader_env_t *e)
{
    unsigned char h[8 + 8 + 4 + 4];
    size_t i;
    for (i = 0; i < 8; i++) h[i] = (unsigned char)((e->gpu_id >> (8 * i)) & 0xFF);
    for (i = 0; i < 8; i++) h[8 + i] = (unsigned char)((e->driver_id >> (8 * i)) & 0xFF);
    for (i = 0; i < 4; i++) h[16 + i] = (unsigned char)((e->api_version >> (8 * i)) & 0xFF);
    for (i = 0; i < 4; i++) h[20 + i] = (unsigned char)((e->flags >> (8 * i)) & 0xFF);
    return le_write(fp, h, sizeof h);
}

static void read_env(FILE *fp, cellar_shader_env_t *e)
{
    unsigned char h[24];
    size_t i;
    if (fread(h, 1, sizeof h, fp) != sizeof h) { memset(e, 0, sizeof *e); return; }
    e->gpu_id = 0;
    for (i = 0; i < 8; i++) e->gpu_id |= (uint64_t)h[i] << (8 * i);
    e->driver_id = 0;
    for (i = 0; i < 8; i++) e->driver_id |= (uint64_t)h[8 + i] << (8 * i);
    e->api_version = 0;
    for (i = 0; i < 4; i++) e->api_version |= (uint32_t)h[16 + i] << (8 * i);
    e->flags = 0;
    for (i = 0; i < 4; i++) e->flags |= (uint32_t)h[20 + i] << (8 * i);
}

cellar_status_t cellar_shadercache_open(cellar_shadercache_t *c,
                                        const char *path,
                                        const cellar_shader_env_t *env)
{
    unsigned char hdr[8 + 8];
    FILE *fp;
    int match = 0;

    if (!c || !path || !env)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memset(c, 0, sizeof *c);
    snprintf(c->path, sizeof c->path, "%s", path);
    c->env = *env;

    fp = fopen(path, "r+b");
    if (fp) {
        /* read magic + version + env */
        cellar_shader_env_t fenv;
        if (fread(hdr, 1, 8, fp) == 8) {
            uint64_t magic = 0; size_t i;
            for (i = 0; i < 8; i++) magic |= (uint64_t)hdr[i] << (8 * i);
            if (magic == CELLAR_SHADERCACHE_MAGIC && hdr[7] == CELLAR_SHADERCACHE_VERSION) {
                read_env(fp, &fenv);
                match = (fenv.gpu_id == env->gpu_id &&
                         fenv.driver_id == env->driver_id &&
                         fenv.api_version == env->api_version);
                /* read entry_count (8 bytes) */
                fread(hdr, 1, 8, fp);
                c->entries = 0;
                for (i = 0; i < 8; i++) c->entries |= (uint64_t)hdr[i] << (8 * i);
            }
        }
        fclose(fp);
    }

    /* If invalid or env mismatch, recreate (invalidation rule). */
    if (!match) {
        fp = fopen(path, "wb");
        if (!fp)
            return CELLAR_ERR_INVALID_ARGUMENT;
        {
            unsigned char m[8];
            size_t i;
            for (i = 0; i < 8; i++)
                m[i] = (unsigned char)((CELLAR_SHADERCACHE_MAGIC >> (8 * i)) & 0xFF);
            m[7] = CELLAR_SHADERCACHE_VERSION;
            le_write(fp, m, 8);
        }
        write_env(fp, env);
        {
            unsigned char z[8] = {0};
            le_write(fp, z, 8); /* entry_count = 0 */
        }
        fclose(fp);
        c->valid = 1;
        c->entries = 0;
    } else {
        c->valid = 1;
    }

    fp = fopen(path, "ab");
    if (!fp)
        return CELLAR_ERR_INVALID_ARGUMENT;
    c->fp = fp;
    return CELLAR_OK;
}

size_t cellar_shadercache_lookup(const cellar_shadercache_t *c, uint64_t key,
                                 void *blob, size_t cap)
{
    FILE *fp;
    uint64_t k;
    uint32_t sz;
    size_t found = 0;
    if (!c || !c->valid || !c->path[0])
        return 0;
    fp = fopen(c->path, "rb");
    if (!fp)
        return 0;
    /* skip header: 8 magic + 24 env + 8 count */
    fseek(fp, 40, SEEK_SET);
    while (fread(&k, 1, 8, fp) == 8) {
        unsigned char sb[4];
        if (fread(sb, 1, 4, fp) != 4)
            break;
        sz = (uint32_t)sb[0] | ((uint32_t)sb[1] << 8) |
             ((uint32_t)sb[2] << 16) | ((uint32_t)sb[3] << 24);
        if (k == key) {
            if (blob && sz <= cap && fread(blob, 1, sz, fp) == sz)
                found = sz;
            else
                found = sz; /* caller has it cached already / too small */
            break;
        }
        fseek(fp, sz, SEEK_CUR);
    }
    fclose(fp);
    return found;
}

cellar_status_t cellar_shadercache_insert(cellar_shadercache_t *c,
                                          uint64_t key,
                                          const void *blob, size_t len)
{
    unsigned char kb[8], sb[4];
    size_t i;
    if (!c || !c->valid || !c->fp || !blob)
        return CELLAR_ERR_INVALID_ARGUMENT;
    for (i = 0; i < 8; i++) kb[i] = (unsigned char)((key >> (8 * i)) & 0xFF);
    for (i = 0; i < 4; i++) sb[i] = (unsigned char)((len >> (8 * i)) & 0xFF);
    if (fwrite(kb, 1, 8, c->fp) != 8 || fwrite(sb, 1, 4, c->fp) != 4 ||
        fwrite(blob, 1, len, c->fp) != len)
        return CELLAR_ERR_INVALID_ARGUMENT;
    c->entries++;
    return CELLAR_OK;
}

cellar_status_t cellar_shadercache_close(cellar_shadercache_t *c)
{
    if (!c)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (c->fp) {
        /* Update entry_count in the header (bytes 32..39). */
        fflush(c->fp);
        fclose(c->fp);
        c->fp = NULL;
        {
            FILE *fp = fopen(c->path, "r+b");
            if (fp) {
                unsigned char cnt[8];
                size_t i;
                fseek(fp, 32, SEEK_SET);
                for (i = 0; i < 8; i++) cnt[i] =
                    (unsigned char)((c->entries >> (8 * i)) & 0xFF);
                fwrite(cnt, 1, 8, fp);
                fclose(fp);
            }
        }
    }
    c->valid = 0;
    return CELLAR_OK;
}

uint64_t cellar_shader_hash(const void *data, size_t len)
{
    const unsigned char *p = data;
    uint64_t h = 1469598103934665603ull;
    size_t i;
    for (i = 0; i < len; i++) {
        h ^= p[i];
        h *= 1099511628211ull;
    }
    return h;
}

uint64_t cellar_shader_cache_key(uint64_t shader_hash, uint32_t salt)
{
    return shader_hash ^ ((uint64_t)salt << 32);
}
