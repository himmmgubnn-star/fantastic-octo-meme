/*
 * win32.h — the Cellar Win32 API layer.
 *
 * Windows executables import functions from system DLLs (kernel32, user32,
 * gdi32, ntdll, ...). Cellar registers *modules* of native C implementations
 * (currently stubs) into a global export registry. The loader consults this
 * registry while resolving imports, so a Windows binary can bind to
 * MessageBoxA, ExitProcess, HeapAlloc and friends exactly as it would on real
 * Windows.
 *
 * The registry is O(1): each module is indexed by a stable hash of its name,
 * and each export within a module by a hash of the function name.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_WIN32_H
#define CELLAR_WIN32_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

/* A single exported Win32 function. `name` must be a static string (it is
 * not copied). `fn` is a function pointer with the *native Windows calling
 * convention* for that API. */
typedef struct cellar_export_entry {
    const char *name;
    void       *fn;
} cellar_export_entry_t;

/* A registered module (a Win32 system DLL Cellar implements). */
typedef struct cellar_module {
    const char            *name;   /* e.g. "KERNEL32.dll"                  */
    const cellar_export_entry_t *exports;
    size_t                 count;
} cellar_module_t;

/* ---- API ----------------------------------------------------------------- */

/* Register every built-in Win32 module (call once at startup). */
void cellar_win32_init(void);

/* Register a module. The module descriptor and its export array must remain
 * alive for the lifetime of the process (they are referenced, not copied). */
void cellar_win32_register_module(const cellar_module_t *mod);

/* Look up a module by DLL name (case-insensitive on the basename). Returns
 * NULL when unknown. */
const cellar_module_t *cellar_win32_find_module(const char *name);

/* Look up an export within a module by function name. Returns NULL when the
 * module or the function is unknown. */
void *cellar_win32_lookup(const char *module, const char *function);

/* Resolve a single import descriptor entry to a Cellar function pointer. */
void *cellar_win32_resolve(const char *module, const char *function,
                           uint16_t ordinal);

/* Print the contents of the registry to stderr (diagnostics / --list-modules). */
void cellar_win32_dump_registry(void);

#endif /* CELLAR_WIN32_H */
