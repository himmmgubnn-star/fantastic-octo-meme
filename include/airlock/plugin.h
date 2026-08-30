/*
 * plugin.h — plugin architecture and backend hot-selection.
 *
 * Subsystems (graphics, audio, input, network) are *backends*. Backends are
 * registered with a preference priority, and the runtime picks the best
 * available one — e.g. Vulkan → OpenGL → Software. Backends can be built in
 * or loaded as shared objects (graphics_backend.so, audio_backend.so, ...)
 * at runtime via dlopen.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_PLUGIN_H
#define AIRLOCK_PLUGIN_H

#include <stddef.h>

#include "airlock.h"

typedef enum airlock_backend_kind {
    AIRLOCK_BACKEND_GRAPHICS = 0,
    AIRLOCK_BACKEND_AUDIO,
    AIRLOCK_BACKEND_INPUT,
    AIRLOCK_BACKEND_NETWORK,
    AIRLOCK_BACKEND_A11Y,
    AIRLOCK_BACKEND_PRINT,
    AIRLOCK_BACKEND_DEVICE,
    AIRLOCK_BACKEND_KIND_COUNT
} airlock_backend_kind_t;

/* A registered backend. Higher priority wins; lower number = better. */
typedef struct airlock_backend {
    const char *name;             /* "Vulkan", "OpenGL", "Software", ...  */
    int         priority;         /* 0 = best, larger = worse fallback    */
    int         available;        /* runtime probe result                 */
    void       *handle;           /* reserved for dlopen handle           */
} airlock_backend_t;

/* ---- Registry ------------------------------------------------------------- */

void airlock_backend_init(void);

/* Register (or update) a backend for a kind. The name string is referenced,
 * not copied — keep it static. */
void airlock_backend_register(airlock_backend_kind_t kind,
                             const char *name, int priority, int available);

/* Mark a registered backend available/unavailable at runtime. */
void airlock_backend_set_available(const char *name, int available);

/* Pick the best available backend for a kind. Returns the name, or "Software"
 * for graphics (always available), or NULL if none. */
const char *airlock_backend_pick(airlock_backend_kind_t kind);

/* List registered backends for a kind into `out` (returns count). */
size_t airlock_backend_list(airlock_backend_kind_t kind,
                           airlock_backend_t *out, size_t cap);

/* ---- Plugin loading ------------------------------------------------------- */

/* Plugin entry point a .so must export (optional). */
typedef void (*airlock_plugin_entry_t)(void);

/* dlopen a plugin .so and call its `airlock_plugin_entry` if present. Returns
 * AIRLOCK_OK on success (even if the symbol is absent). */
airlock_status_t airlock_plugin_load(const char *path);

#endif /* AIRLOCK_PLUGIN_H */
