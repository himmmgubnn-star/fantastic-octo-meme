/*
 * runtime.h — Windows runtime dependency manager.
 *
 * Tracks legitimate redistributables an application may need (Visual C++
 * runtime, .NET, DirectX components, Windows fonts, common system libraries)
 * inside a prefix. Installation is a reproducible marker + directory layout
 * rather than downloading binaries: Cellar never fetches proprietary blobs.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_RUNTIME_H
#define CELLAR_RUNTIME_H

#include <stddef.h>

#include "cellar.h"

typedef enum cellar_runtime_kind {
    CELLAR_RT_VCRUNTIME = 0,
    CELLAR_RT_DOTNET,
    CELLAR_RT_DIRECTX,
    CELLAR_RT_FONTS,
    CELLAR_RT_SYSTEMLIBS,
    CELLAR_RT_COUNT
} cellar_runtime_kind_t;

typedef struct cellar_runtime {
    cellar_runtime_kind_t kind;
    const char *name;
    const char *version;
    int  installed;
    char install_path[256];
} cellar_runtime_t;

/* Bind the manager to a bottle path (`<prefix>/<name>`). */
cellar_status_t cellar_runtime_init(const char *bottle);

cellar_runtime_kind_t cellar_runtime_parse(const char *name);
const char *cellar_runtime_kind_name(cellar_runtime_kind_t k);

cellar_status_t cellar_runtime_install(cellar_runtime_kind_t kind);
cellar_status_t cellar_runtime_uninstall(cellar_runtime_kind_t kind);
int cellar_runtime_is_installed(cellar_runtime_kind_t kind);

size_t cellar_runtime_list(cellar_runtime_t *out, size_t cap);
void cellar_runtime_report(void);

#endif /* CELLAR_RUNTIME_H */
