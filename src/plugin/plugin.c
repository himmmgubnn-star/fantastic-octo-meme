/*
 * plugin.c — backend registry and hot-selection.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#ifdef __unix__
#include <dlfcn.h>
#endif

#include "cellar/cellar.h"
#include "cellar/plugin.h"

#define CELLAR_MAX_BACKENDS 16

static cellar_backend_t g_backends[CELLAR_BACKEND_KIND_COUNT][CELLAR_MAX_BACKENDS];
static size_t g_count[CELLAR_BACKEND_KIND_COUNT];

static void seed_defaults(void)
{
    static int seeded = 0;
    if (seeded)
        return;
    seeded = 1;
    /* Graphics: Vulkan preferred, OpenGL fallback, Software emergency. */
    cellar_backend_register(CELLAR_BACKEND_GRAPHICS, "Vulkan",  0, 1);
    cellar_backend_register(CELLAR_BACKEND_GRAPHICS, "OpenGL",  1, 1);
    cellar_backend_register(CELLAR_BACKEND_GRAPHICS, "Software", 2, 1);
    /* Audio: ALSA preferred, PipeWire next, WAV/null last. */
    cellar_backend_register(CELLAR_BACKEND_AUDIO, "ALSA",     0, 1);
    cellar_backend_register(CELLAR_BACKEND_AUDIO, "PipeWire", 1, 0);
    cellar_backend_register(CELLAR_BACKEND_AUDIO, "WAV",      2, 1);
}

void cellar_backend_init(void) { seed_defaults(); }

void cellar_backend_register(cellar_backend_kind_t kind,
                             const char *name, int priority, int available)
{
    size_t i;
    seed_defaults();
    if (kind >= CELLAR_BACKEND_KIND_COUNT || !name)
        return;
    /* Replace an existing backend of the same name. */
    for (i = 0; i < g_count[kind]; i++)
        if (strcmp(g_backends[kind][i].name, name) == 0) {
            g_backends[kind][i].priority = priority;
            g_backends[kind][i].available = available;
            return;
        }
    if (g_count[kind] >= CELLAR_MAX_BACKENDS)
        return;
    g_backends[kind][g_count[kind]].name = name;
    g_backends[kind][g_count[kind]].priority = priority;
    g_backends[kind][g_count[kind]].available = available;
    g_backends[kind][g_count[kind]].handle = NULL;
    g_count[kind]++;
}

void cellar_backend_set_available(const char *name, int available)
{
    cellar_backend_kind_t k;
    size_t i;
    for (k = 0; k < CELLAR_BACKEND_KIND_COUNT; k++)
        for (i = 0; i < g_count[k]; i++)
            if (strcmp(g_backends[k][i].name, name) == 0)
                g_backends[k][i].available = available;
}

const char *cellar_backend_pick(cellar_backend_kind_t kind)
{
    const cellar_backend_t *best = NULL;
    size_t i;
    seed_defaults();
    if (kind >= CELLAR_BACKEND_KIND_COUNT)
        return NULL;
    for (i = 0; i < g_count[kind]; i++) {
        const cellar_backend_t *b = &g_backends[kind][i];
        if (!b->available)
            continue;
        if (!best || b->priority < best->priority)
            best = b;
    }
    if (best)
        return best->name;
    if (kind == CELLAR_BACKEND_GRAPHICS)
        return "Software";
    return NULL;
}

size_t cellar_backend_list(cellar_backend_kind_t kind,
                           cellar_backend_t *out, size_t cap)
{
    size_t i, n;
    seed_defaults();
    if (kind >= CELLAR_BACKEND_KIND_COUNT || !out)
        return 0;
    n = g_count[kind] < cap ? g_count[kind] : cap;
    for (i = 0; i < n; i++)
        out[i] = g_backends[kind][i];
    return n;
}

cellar_status_t cellar_plugin_load(const char *path)
{
#ifdef __unix__
    void *h = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    if (!h)
        return CELLAR_ERR_INVALID_ARGUMENT;
    {
        cellar_plugin_entry_t entry =
            (cellar_plugin_entry_t)(void *)dlsym(h, "cellar_plugin_entry");
        if (entry)
            entry();
    }
    return CELLAR_OK;
#else
    (void)path;
    return CELLAR_ERR_NOT_IMPLEMENTED;
#endif
}
