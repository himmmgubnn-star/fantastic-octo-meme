/*
 * win32.h — the Airlock Win32 API layer.
 *
 * Windows executables import functions from system DLLs (kernel32, user32,
 * gdi32, ntdll, ...). Airlock registers *modules* of native C implementations
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
#ifndef AIRLOCK_WIN32_H
#define AIRLOCK_WIN32_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

/* A single exported Win32 function. `name` must be a static string (it is
 * not copied). `fn` is a function pointer with the *native Windows calling
 * convention* for that API. */
typedef struct airlock_export_entry {
    const char *name;
    void       *fn;
} airlock_export_entry_t;

/* A registered module (a Win32 system DLL Airlock implements). */
typedef struct airlock_module {
    const char            *name;   /* e.g. "KERNEL32.dll"                  */
    const airlock_export_entry_t *exports;
    size_t                 count;
} airlock_module_t;

/* ---- API ----------------------------------------------------------------- */

/* Register every built-in Win32 module (call once at startup). */
void airlock_win32_init(void);

/* Register a module. The module descriptor and its export array must remain
 * alive for the lifetime of the process (they are referenced, not copied). */
void airlock_win32_register_module(const airlock_module_t *mod);

/* Look up a module by DLL name (case-insensitive on the basename). Returns
 * NULL when unknown. */
const airlock_module_t *airlock_win32_find_module(const char *name);

/* Number of registered modules and the i-th module (for iteration). */
size_t airlock_win32_module_count(void);
const airlock_module_t *airlock_win32_module_at(size_t i);

/* Non-logging check: is `function` exported by `module`? (Used by the
 * compatibility analyzer so coverage scoring doesn't spam the log.) */
bool airlock_win32_export_exists(const char *module, const char *function);

/* Look up an export within a module by function name. Returns NULL when the
 * module or the function is unknown. */
void *airlock_win32_lookup(const char *module, const char *function);

/* Resolve a single import descriptor entry to a Airlock function pointer. */
void *airlock_win32_resolve(const char *module, const char *function,
                           uint16_t ordinal);

/* Print the contents of the registry to stderr (diagnostics / --list-modules). */
void airlock_win32_dump_registry(void);

#endif /* AIRLOCK_WIN32_H */
