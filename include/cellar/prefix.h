/*
 * prefix.h — isolated Windows environment (bottle) manager.
 *
 * Each named prefix has its own registry, DLL configuration, Windows-version
 * behavior, filesystem (drive_c), environment variables, and graphics/audio
 * settings:
 *
 *   cellar prefix create Game
 *   cellar prefix launch Game game.exe
 *   cellar prefix backup Game game.bak
 *   cellar prefix restore Game game.bak
 *   cellar prefix delete Game
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_PREFIX_H
#define CELLAR_PREFIX_H

#include <stddef.h>

#include "cellar.h"
#include "compat.h"

typedef struct cellar_prefix_info {
    char name[64];
    char path[512];
    cellar_version_mode_t version_mode;
    char gfx[32];
    char audio[32];
    int  exists;
} cellar_prefix_info_t;

/* Create a bottle under `root`/`name` (drive_c tree + prefix.conf). */
cellar_status_t cellar_prefix_create(const char *root, const char *name);

cellar_status_t cellar_prefix_delete(const char *root, const char *name);

cellar_status_t cellar_prefix_info(const char *root, const char *name,
                                   cellar_prefix_info_t *out);

cellar_status_t cellar_prefix_set_config(const char *root, const char *name,
                                         cellar_version_mode_t mode,
                                         const char *gfx, const char *audio);

/* Custom archive (not tar) of the bottle's small metadata + files. */
cellar_status_t cellar_prefix_backup(const char *root, const char *name,
                                     const char *archive_path);
cellar_status_t cellar_prefix_restore(const char *root, const char *name,
                                      const char *archive_path);

size_t cellar_prefix_list(const char *root, cellar_prefix_info_t *out,
                          size_t cap);

/* Absolute path of `root`/`name` into `dst`. */
void cellar_prefix_path(char *dst, size_t n, const char *root,
                        const char *name);

#endif /* CELLAR_PREFIX_H */
