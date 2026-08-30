/*
 * installer.h — Windows installer compatibility (side effects, not MSI parsing).
 *
 * .exe / .msi installers mutate the registry, create shortcuts, set env vars
 * and drop files. Airlock journals those actions per package so they can be
 * applied to a prefix and rolled back on uninstall. Opaque MSI database
 * parsing is out of scope; this is the environment an installer *runs in*.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_INSTALLER_H
#define AIRLOCK_INSTALLER_H

#include <stddef.h>

#include "airlock.h"

typedef enum airlock_install_kind {
    AIRLOCK_INST_REGISTRY = 0,
    AIRLOCK_INST_SHORTCUT,
    AIRLOCK_INST_ENV,
    AIRLOCK_INST_FILE
} airlock_install_kind_t;

typedef struct airlock_install_action {
    airlock_install_kind_t kind;
    char key[128];
    char value[256];
} airlock_install_action_t;

typedef struct airlock_package {
    char name[128];
    char version[32];
    char publisher[64];
    airlock_install_action_t actions[64];
    size_t action_count;
    int installed;
} airlock_package_t;

airlock_status_t airlock_install_begin(airlock_package_t *pkg, const char *name,
                                     const char *version, const char *publisher);
airlock_status_t airlock_install_add(airlock_package_t *pkg,
                                   airlock_install_kind_t kind,
                                   const char *key, const char *value);
airlock_status_t airlock_install_commit(const char *bottle, airlock_package_t *pkg);
airlock_status_t airlock_install_uninstall(const char *bottle, const char *name);
airlock_status_t airlock_install_load(const char *bottle, const char *name,
                                    airlock_package_t *out);

#endif /* AIRLOCK_INSTALLER_H */
