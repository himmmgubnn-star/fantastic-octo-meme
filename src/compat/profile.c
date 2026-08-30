/*
 * profile.c — per-application compatibility profiles and Windows-version
 * behavior modes.
 *
 * A per-application profile is a small key=value file under a prefix
 * directory (default ~/.cellar/prefixes/<app>/profile.conf). It remembers a
 * configuration that worked, so subsequent launches reuse it automatically.
 *
 * A version behavior profile is more than a reported version number: it
 * carries behavioral flags the compatibility layer acts on (DPI awareness,
 * UTF-8 code page, touch input, threadpool model, ARM translation).
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

#include "cellar/cellar.h"
#include "cellar/compat.h"

/* ---- Windows version behavior profiles ----------------------------------- */

static const cellar_version_profile_t k_win7 = {
    .mode = CELLAR_WIN_7, .name = "Windows 7",
    .major = 6, .minor = 1, .build = 7601, .sp_major = 1, .sp_minor = 0,
    .product_type = 1,
    .high_dpi_aware_by_default = 0, .prefer_utf8_codepage = 0,
    .touch_input_available = 0, .modern_threadpool = 0, .arm_translation = 0,
};
static const cellar_version_profile_t k_win81 = {
    .mode = CELLAR_WIN_81, .name = "Windows 8.1",
    .major = 6, .minor = 3, .build = 9600, .sp_major = 0, .sp_minor = 0,
    .product_type = 1,
    .high_dpi_aware_by_default = 0, .prefer_utf8_codepage = 0,
    .touch_input_available = 1, .modern_threadpool = 1, .arm_translation = 0,
};
static const cellar_version_profile_t k_win10 = {
    .mode = CELLAR_WIN_10, .name = "Windows 10",
    .major = 10, .minor = 0, .build = 19045, .sp_major = 0, .sp_minor = 0,
    .product_type = 1,
    .high_dpi_aware_by_default = 1, .prefer_utf8_codepage = 1,
    .touch_input_available = 1, .modern_threadpool = 1, .arm_translation = 0,
};
static const cellar_version_profile_t k_win11 = {
    .mode = CELLAR_WIN_11, .name = "Windows 11",
    .major = 10, .minor = 0, .build = 22631, .sp_major = 0, .sp_minor = 0,
    .product_type = 1,
    .high_dpi_aware_by_default = 1, .prefer_utf8_codepage = 1,
    .touch_input_available = 1, .modern_threadpool = 1, .arm_translation = 1,
};

const cellar_version_profile_t *cellar_version_profile(cellar_version_mode_t m)
{
    switch (m) {
    case CELLAR_WIN_7:  return &k_win7;
    case CELLAR_WIN_81: return &k_win81;
    case CELLAR_WIN_10: return &k_win10;
    case CELLAR_WIN_11: return &k_win11;
    default:            return &k_win10;
    }
}

/* ---- Prefix directory ----------------------------------------------------- */

const char *cellar_prefix_dir(void)
{
    static char buf[1024];
    const char *env = getenv("CELLAR_PREFIX");
    if (env && *env) {
        snprintf(buf, sizeof buf, "%s", env);
        return buf;
    }
    {
        const char *home = getenv("HOME");
        snprintf(buf, sizeof buf, "%s/.cellar/prefixes",
                 home && *home ? home : ".");
    }
    return buf;
}

/* ---- key=value helpers ---------------------------------------------------- */

static const char *find_field(const char *conf, const char *key,
                              char *dst, size_t dstn)
{
    const char *p = conf;
    size_t klen = strlen(key);
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *e = v;
            while (*e && *e != '\n') e++;
            {
                size_t n = (size_t)(e - v);
                if (n >= dstn) n = dstn - 1;
                memcpy(dst, v, n);
                dst[n] = '\0';
            }
            return dst;
        }
        p = strchr(p, '\n');
        if (p) p++;
    }
    return NULL;
}

static int field_int(const char *conf, const char *key, int def)
{
    char b[64];
    return find_field(conf, key, b, sizeof b) ? atoi(b) : def;
}

/* ---- Save / load ---------------------------------------------------------- */

cellar_status_t cellar_profile_save(const char *prefix_dir,
                                    const cellar_app_profile_t *p)
{
    char dir[1024], path[1152];
    FILE *f;
    if (!prefix_dir || !p || !p->app_name[0])
        return CELLAR_ERR_INVALID_ARGUMENT;

    mkdir(prefix_dir, 0755); /* best-effort; may already exist */
    snprintf(dir, sizeof dir, "%s/%s", prefix_dir, p->app_name);
    mkdir(dir, 0755);        /* best-effort; may already exist */
    snprintf(path, sizeof path, "%s/profile.conf", dir);

    f = fopen(path, "w");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fprintf(f, "# Cellar compatibility profile for %s\n", p->app_name);
    fprintf(f, "version_mode=%d\n", (int)p->version_mode);
    fprintf(f, "gfx_backend=%s\n", p->gfx_backend);
    fprintf(f, "audio_backend=%s\n", p->audio_backend);
    fprintf(f, "dll_overrides=%s\n", p->dll_overrides);
    fprintf(f, "low_latency_sync=%d\n", p->low_latency_sync);
    fprintf(f, "shader_cache_enabled=%d\n", p->shader_cache_enabled);
    fprintf(f, "last_good=%d\n", p->last_good);
    fclose(f);
    return CELLAR_OK;
}

cellar_status_t cellar_profile_load(const char *prefix_dir,
                                    const char *app_name,
                                    cellar_app_profile_t *out)
{
    char path[1152];
    char buf[8192];
    FILE *f;
    size_t n;
    if (!prefix_dir || !app_name || !out)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);

    snprintf(path, sizeof path, "%s/%s/profile.conf", prefix_dir, app_name);
    f = fopen(path, "rb");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT; /* not found */
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';

    snprintf(out->app_name, sizeof out->app_name, "%s", app_name);
    out->version_mode = (cellar_version_mode_t)field_int(buf, "version_mode", CELLAR_WIN_10);
    find_field(buf, "gfx_backend", out->gfx_backend, sizeof out->gfx_backend);
    find_field(buf, "audio_backend", out->audio_backend, sizeof out->audio_backend);
    find_field(buf, "dll_overrides", out->dll_overrides, sizeof out->dll_overrides);
    out->low_latency_sync = field_int(buf, "low_latency_sync", 0);
    out->shader_cache_enabled = field_int(buf, "shader_cache_enabled", 0);
    out->last_good = field_int(buf, "last_good", 0);
    return CELLAR_OK;
}

cellar_status_t cellar_profile_mark_last_good(const char *prefix_dir,
                                              const cellar_app_profile_t *p)
{
    cellar_app_profile_t tmp;
    if (!p)
        return CELLAR_ERR_INVALID_ARGUMENT;
    tmp = *p;
    tmp.last_good = 1;
    return cellar_profile_save(prefix_dir, &tmp);
}
