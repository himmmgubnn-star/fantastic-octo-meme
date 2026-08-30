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
#ifndef CELLAR_PLUGIN_H
#define CELLAR_PLUGIN_H

#include <stddef.h>

#include "cellar.h"

typedef enum cellar_backend_kind {
    CELLAR_BACKEND_GRAPHICS = 0,
    CELLAR_BACKEND_AUDIO,
    CELLAR_BACKEND_INPUT,
    CELLAR_BACKEND_NETWORK,
    CELLAR_BACKEND_A11Y,
    CELLAR_BACKEND_PRINT,
    CELLAR_BACKEND_DEVICE,
    CELLAR_BACKEND_KIND_COUNT
} cellar_backend_kind_t;

/* A registered backend. Higher priority wins; lower number = better. */
typedef struct cellar_backend {
    const char *name;             /* "Vulkan", "OpenGL", "Software", ...  */
    int         priority;         /* 0 = best, larger = worse fallback    */
    int         available;        /* runtime probe result                 */
    void       *handle;           /* reserved for dlopen handle           */
} cellar_backend_t;

/* ---- Registry ------------------------------------------------------------- */

void cellar_backend_init(void);

/* Register (or update) a backend for a kind. The name string is referenced,
 * not copied — keep it static. */
void cellar_backend_register(cellar_backend_kind_t kind,
                             const char *name, int priority, int available);

/* Mark a registered backend available/unavailable at runtime. */
void cellar_backend_set_available(const char *name, int available);

/* Pick the best available backend for a kind. Returns the name, or "Software"
 * for graphics (always available), or NULL if none. */
const char *cellar_backend_pick(cellar_backend_kind_t kind);

/* List registered backends for a kind into `out` (returns count). */
size_t cellar_backend_list(cellar_backend_kind_t kind,
                           cellar_backend_t *out, size_t cap);

/* ---- Plugin loading ------------------------------------------------------- */

/* Plugin entry point a .so must export (optional). */
typedef void (*cellar_plugin_entry_t)(void);

/* dlopen a plugin .so and call its `cellar_plugin_entry` if present. Returns
 * CELLAR_OK on success (even if the symbol is absent). */
cellar_status_t cellar_plugin_load(const char *path);

#endif /* CELLAR_PLUGIN_H */
