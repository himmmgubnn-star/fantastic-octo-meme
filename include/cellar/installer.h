/*
 * installer.h — Windows installer compatibility (side effects, not MSI parsing).
 *
 * .exe / .msi installers mutate the registry, create shortcuts, set env vars
 * and drop files. Cellar journals those actions per package so they can be
 * applied to a prefix and rolled back on uninstall. Opaque MSI database
 * parsing is out of scope; this is the environment an installer *runs in*.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_INSTALLER_H
#define CELLAR_INSTALLER_H

#include <stddef.h>

#include "cellar.h"

typedef enum cellar_install_kind {
    CELLAR_INST_REGISTRY = 0,
    CELLAR_INST_SHORTCUT,
    CELLAR_INST_ENV,
    CELLAR_INST_FILE
} cellar_install_kind_t;

typedef struct cellar_install_action {
    cellar_install_kind_t kind;
    char key[128];
    char value[256];
} cellar_install_action_t;

typedef struct cellar_package {
    char name[128];
    char version[32];
    char publisher[64];
    cellar_install_action_t actions[64];
    size_t action_count;
    int installed;
} cellar_package_t;

cellar_status_t cellar_install_begin(cellar_package_t *pkg, const char *name,
                                     const char *version, const char *publisher);
cellar_status_t cellar_install_add(cellar_package_t *pkg,
                                   cellar_install_kind_t kind,
                                   const char *key, const char *value);
cellar_status_t cellar_install_commit(const char *bottle, cellar_package_t *pkg);
cellar_status_t cellar_install_uninstall(const char *bottle, const char *name);
cellar_status_t cellar_install_load(const char *bottle, const char *name,
                                    cellar_package_t *out);

#endif /* CELLAR_INSTALLER_H */
