/*
 * mod_comdlg32.c — COMDLG32.dll (common dialogs).
 *
 * SPDX-License-Identifier: MIT
 */
#include "airlock/airlock.h"
#include "airlock/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef int BOOL;

/* OPENFILENAMEA is a large struct; we only need to exist for the import. */
static BOOL WINAPI airlock_GetOpenFileNameA(void *ofn)
{
    UNUSED(ofn);
    AIRLOCK_LOG_INFO("COMDLG32.GetOpenFileNameA — cancelled (no UI yet)");
    return 0;
}

static BOOL WINAPI airlock_GetSaveFileNameA(void *ofn)
{
    UNUSED(ofn);
    return 0;
}

static const airlock_export_entry_t k_exports[] = {
    { "GetOpenFileNameA", (void *)&airlock_GetOpenFileNameA },
    { "GetSaveFileNameA", (void *)&airlock_GetSaveFileNameA },
};

static const airlock_module_t k_mod = {
    "comdlg32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const airlock_module_t *airlock_win32_module_comdlg32(void) { return &k_mod; }
