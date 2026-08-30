/*
 * shadercache.h — persistent shader pipeline cache.
 *
 * Compiling shaders is a major source of stutter in games. Airlock caches
 * compiled/processed shader blobs persistently, keyed by a hash of the shader
 * source plus the GPU, driver, and API-version identity. The cache header
 * records that identity so a different GPU, driver, or API version invalidates
 * the whole cache (a safety net for stale shaders) — item 10 of the feature
 * set.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_SHADERCACHE_H
#define AIRLOCK_SHADERCACHE_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

/* Full 64-bit on-disk signature: high byte carries the cache version. */
#define AIRLOCK_SHADERCACHE_MAGIC 0x0143454C53480D00ULL
#define AIRLOCK_SHADERCACHE_VERSION 1 /* == (AIRLOCK_SHADERCACHE_MAGIC >> 56) */

/* Identity that must match for the cache to be valid. */
typedef struct airlock_shader_env {
    uint64_t gpu_id;      /* PCI vendor|device, or a driver-assigned id */
    uint64_t driver_id;   /* driver/build fingerprint                    */
    uint32_t api_version; /* e.g. 0x00410000 for Vulkan 1.1              */
    uint32_t flags;
} airlock_shader_env_t;

typedef struct airlock_shadercache {
    int      valid;
    airlock_shader_env_t env;
    FILE    *fp;          /* open for append after load                    */
    char     path[512];
    uint64_t entries;
} airlock_shadercache_t;

/* Open a cache file. If it exists and its environment matches `env`, the
 * cache is usable; otherwise it is reset (invalidated) and reused. */
airlock_status_t airlock_shadercache_open(airlock_shadercache_t *c,
                                        const char *path,
                                        const airlock_shader_env_t *env);

/* Look up `key`; on hit copies up to `cap` bytes into `blob` and returns the
 * stored size, or returns 0 on miss. */
size_t airlock_shadercache_lookup(const airlock_shadercache_t *c, uint64_t key,
                                 void *blob, size_t cap);

/* Insert `len` bytes under `key`. Returns AIRLOCK_OK on success. */
airlock_status_t airlock_shadercache_insert(airlock_shadercache_t *c,
                                          uint64_t key,
                                          const void *blob, size_t len);

/* Flush + close the cache. */
airlock_status_t airlock_shadercache_close(airlock_shadercache_t *c);

/* ---- Key helpers ---------------------------------------------------------- */

/* Stable hash of a shader byte blob (FNV-1a 64-bit). */
uint64_t airlock_shader_hash(const void *data, size_t len);

/* Compose a lookup key from a shader hash + a per-program salt. */
uint64_t airlock_shader_cache_key(uint64_t shader_hash, uint32_t salt);

#endif /* AIRLOCK_SHADERCACHE_H */
