/*
 * api.c — the Airlock Win32 export registry.
 *
 * Modules (KERNEL32.dll, USER32.dll, ...) register here at startup. The PE
 * loader resolves each import thunk against this registry, so a Windows
 * binary binds to Airlock's native implementations.
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/win32.h"

/* Registry = flat, growable array of module descriptors. */
static const airlock_module_t **g_modules = NULL;
static size_t g_module_count = 0;
static size_t g_module_cap = 0;

void airlock_win32_register_module(const airlock_module_t *mod)
{
    if (!mod || !mod->name || !mod->exports)
        return;

    if (g_module_count == g_module_cap) {
        size_t ncap = g_module_cap ? g_module_cap * 2 : 8;
        const airlock_module_t **nm =
            realloc(g_modules, ncap * sizeof *nm);
        if (!nm)
            return;
        g_modules = nm;
        g_module_cap = ncap;
    }
    g_modules[g_module_count++] = mod;
}

/*
 * Case-insensitive, extension-insensitive module comparison.
 * "kernel32" == "KERNEL32.DLL" == "Kernel32.dll".
 */
static int module_name_eq(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b);
    const char *ea = strrchr(a, '.');
    const char *eb = strrchr(b, '.');

    /* Strip ".dll"/".exe" suffixes for comparison. */
    if (ea) la = (size_t)(ea - a);
    if (eb) lb = (size_t)(eb - b);

    if (la != lb)
        return 0;
    for (size_t i = 0; i < la; i++)
        if (tolower((unsigned char)a[i]) != tolower((unsigned char)b[i]))
            return 0;
    return 1;
}

const airlock_module_t *airlock_win32_find_module(const char *name)
{
    size_t i;
    if (!name)
        return NULL;
    for (i = 0; i < g_module_count; i++)
        if (module_name_eq(g_modules[i]->name, name))
            return g_modules[i];
    return NULL;
}

size_t airlock_win32_module_count(void)
{
    return g_module_count;
}

const airlock_module_t *airlock_win32_module_at(size_t i)
{
    if (i >= g_module_count)
        return NULL;
    return g_modules[i];
}

bool airlock_win32_export_exists(const char *module, const char *function)
{
    const airlock_module_t *m = airlock_win32_find_module(module);
    size_t i;
    if (!m || !function)
        return false;
    for (i = 0; i < m->count; i++)
        if (strcmp(m->exports[i].name, function) == 0)
            return true;
    return false;
}

void *airlock_win32_lookup(const char *module, const char *function)
{
    const airlock_module_t *m = airlock_win32_find_module(module);
    size_t i;
    if (!m || !function)
        return NULL;
    for (i = 0; i < m->count; i++)
        if (strcmp(m->exports[i].name, function) == 0)
            return m->exports[i].fn;
    return NULL;
}

void *airlock_win32_resolve(const char *module, const char *function,
                           uint16_t ordinal)
{
    void *p;
    (void)ordinal;
    if (function) {
        p = airlock_win32_lookup(module, function);
        if (p)
            return p;
        AIRLOCK_LOG_DEBUG("unresolved import: %s!%s", module, function);
        return NULL;
    }
    /* Ordinal-only import: Airlock does not yet support ordinal binding. */
    AIRLOCK_LOG_DEBUG("unresolved ordinal import: %s!#%u", module, ordinal);
    return NULL;
}

void airlock_win32_dump_registry(void)
{
    size_t i, j;
    printf("Registered Win32 modules (%zu):\n", g_module_count);
    for (i = 0; i < g_module_count; i++) {
        const airlock_module_t *m = g_modules[i];
        printf("  %-16s %zu exports\n", m->name, m->count);
        for (j = 0; j < m->count; j++)
            printf("    %s\n", m->exports[j].name);
    }
}
