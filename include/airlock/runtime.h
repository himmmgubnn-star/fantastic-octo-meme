/*
 * runtime.h — Windows runtime dependency manager.
 *
 * Tracks legitimate redistributables an application may need (Visual C++
 * runtime, .NET, DirectX components, Windows fonts, common system libraries)
 * inside a prefix. Installation is a reproducible marker + directory layout
 * rather than downloading binaries: Airlock never fetches proprietary blobs.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_RUNTIME_H
#define AIRLOCK_RUNTIME_H

#include <stddef.h>

#include "airlock.h"

typedef enum airlock_runtime_kind {
    AIRLOCK_RT_VCRUNTIME = 0,
    AIRLOCK_RT_DOTNET,
    AIRLOCK_RT_DIRECTX,
    AIRLOCK_RT_FONTS,
    AIRLOCK_RT_SYSTEMLIBS,
    AIRLOCK_RT_COUNT
} airlock_runtime_kind_t;

typedef struct airlock_runtime {
    airlock_runtime_kind_t kind;
    const char *name;
    const char *version;
    int  installed;
    char install_path[256];
} airlock_runtime_t;

/* Bind the manager to a bottle path (`<prefix>/<name>`). */
airlock_status_t airlock_runtime_init(const char *bottle);

airlock_runtime_kind_t airlock_runtime_parse(const char *name);
const char *airlock_runtime_kind_name(airlock_runtime_kind_t k);

airlock_status_t airlock_runtime_install(airlock_runtime_kind_t kind);
airlock_status_t airlock_runtime_uninstall(airlock_runtime_kind_t kind);
int airlock_runtime_is_installed(airlock_runtime_kind_t kind);

size_t airlock_runtime_list(airlock_runtime_t *out, size_t cap);
void airlock_runtime_report(void);

#endif /* AIRLOCK_RUNTIME_H */
