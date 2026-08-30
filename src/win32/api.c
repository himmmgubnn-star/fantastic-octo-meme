/*
 * api.c — the Cellar Win32 export registry.
 *
 * Modules (KERNEL32.dll, USER32.dll, ...) register here at startup. The PE
 * loader resolves each import thunk against this registry, so a Windows
 * binary binds to Cellar's native implementations.
 *
 * SPDX-License-Identifier: MIT
 */
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/win32.h"

/* Registry = flat, growable array of module descriptors. */
static const cellar_module_t **g_modules = NULL;
static size_t g_module_count = 0;
static size_t g_module_cap = 0;

void cellar_win32_register_module(const cellar_module_t *mod)
{
    if (!mod || !mod->name || !mod->exports)
        return;

    if (g_module_count == g_module_cap) {
        size_t ncap = g_module_cap ? g_module_cap * 2 : 8;
        const cellar_module_t **nm =
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

const cellar_module_t *cellar_win32_find_module(const char *name)
{
    size_t i;
    if (!name)
        return NULL;
    for (i = 0; i < g_module_count; i++)
        if (module_name_eq(g_modules[i]->name, name))
            return g_modules[i];
    return NULL;
}

void *cellar_win32_lookup(const char *module, const char *function)
{
    const cellar_module_t *m = cellar_win32_find_module(module);
    size_t i;
    if (!m || !function)
        return NULL;
    for (i = 0; i < m->count; i++)
        if (strcmp(m->exports[i].name, function) == 0)
            return m->exports[i].fn;
    return NULL;
}

void *cellar_win32_resolve(const char *module, const char *function,
                           uint16_t ordinal)
{
    void *p;
    (void)ordinal;
    if (function) {
        p = cellar_win32_lookup(module, function);
        if (p)
            return p;
        CELLAR_LOG_DEBUG("unresolved import: %s!%s", module, function);
        return NULL;
    }
    /* Ordinal-only import: Cellar does not yet support ordinal binding. */
    CELLAR_LOG_DEBUG("unresolved ordinal import: %s!#%u", module, ordinal);
    return NULL;
}

void cellar_win32_dump_registry(void)
{
    size_t i, j;
    printf("Registered Win32 modules (%zu):\n", g_module_count);
    for (i = 0; i < g_module_count; i++) {
        const cellar_module_t *m = g_modules[i];
        printf("  %-16s %zu exports\n", m->name, m->count);
        for (j = 0; j < m->count; j++)
            printf("    %s\n", m->exports[j].name);
    }
}
