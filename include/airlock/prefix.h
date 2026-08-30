/*
 * prefix.h — isolated Windows environment (bottle) manager.
 *
 * Each named prefix has its own registry, DLL configuration, Windows-version
 * behavior, filesystem (drive_c), environment variables, and graphics/audio
 * settings:
 *
 *   airlock prefix create Game
 *   airlock prefix launch Game game.exe
 *   airlock prefix backup Game game.bak
 *   airlock prefix restore Game game.bak
 *   airlock prefix delete Game
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_PREFIX_H
#define AIRLOCK_PREFIX_H

#include <stddef.h>

#include "airlock.h"
#include "compat.h"

typedef struct airlock_prefix_info {
    char name[64];
    char path[512];
    airlock_version_mode_t version_mode;
    char gfx[32];
    char audio[32];
    char arch[8];       /* "win32" / "win64" (fixed at creation) */
    char runner[32];    /* per-prefix Wine/Proton runner         */
    int  exists;
} airlock_prefix_info_t;

/* Create a bottle under `root`/`name` (drive_c tree + prefix.conf). The
 * default architecture is win64. */
airlock_status_t airlock_prefix_create(const char *root, const char *name);

/* Create with an explicit architecture: "win32" or "win64". Architecture is
 * fixed at creation (converting later is not supported). */
airlock_status_t airlock_prefix_create_arch(const char *root, const char *name,
                                          const char *arch);

/* Clone a bottle into a new name (files + config). */
airlock_status_t airlock_prefix_clone(const char *root, const char *src,
                                    const char *dst);

/* Export = backup; import = restore. Semantic aliases so the front end can
 * speak "export a container" / "import a container". */
airlock_status_t airlock_prefix_export(const char *root, const char *name,
                                     const char *archive_path);
airlock_status_t airlock_prefix_import(const char *root, const char *name,
                                     const char *archive_path);

/* Generic key=value settings read/write on the prefix.conf file. These expose
 * the "settings worth exposing" surface (Windows version, runner, graphics
 * backend, resolution, virtual desktop, Box64 preset, CPU/frame limits,
 * ESync/FSync, DLL overrides) at container scope. */
airlock_status_t airlock_prefix_get_setting(const char *root, const char *name,
                                          const char *key,
                                          char *out, size_t cap);
airlock_status_t airlock_prefix_set_setting(const char *root, const char *name,
                                          const char *key, const char *value);

airlock_status_t airlock_prefix_delete(const char *root, const char *name);

airlock_status_t airlock_prefix_info(const char *root, const char *name,
                                   airlock_prefix_info_t *out);

airlock_status_t airlock_prefix_set_config(const char *root, const char *name,
                                         airlock_version_mode_t mode,
                                         const char *gfx, const char *audio);

/* Custom archive (not tar) of the bottle's small metadata + files. */
airlock_status_t airlock_prefix_backup(const char *root, const char *name,
                                     const char *archive_path);
airlock_status_t airlock_prefix_restore(const char *root, const char *name,
                                      const char *archive_path);

size_t airlock_prefix_list(const char *root, airlock_prefix_info_t *out,
                          size_t cap);

/* Absolute path of `root`/`name` into `dst`. */
void airlock_prefix_path(char *dst, size_t n, const char *root,
                        const char *name);

#endif /* AIRLOCK_PREFIX_H */
