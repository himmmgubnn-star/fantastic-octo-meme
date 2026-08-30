/*
 * mod_shell32.c — SHELL32.dll (known folders, ShellExecute).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/desktop.h"
#include "airlock/shell.h"
#include "airlock/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef void *HWND;
typedef int HRESULT;
typedef int BOOL;
typedef unsigned long DWORD;

#define S_OK 0
#define E_FAIL 0x80004005
#define CSIDL_APPDATA        0x001a
#define CSIDL_COMMON_APPDATA 0x0023
#define CSIDL_LOCAL_APPDATA  0x001c
#define CSIDL_WINDOWS        0x0024
#define CSIDL_SYSTEM         0x0025
#define CSIDL_PROGRAM_FILES  0x0026
#define CSIDL_PROFILE        0x0028

static HRESULT WINAPI airlock_SHGetFolderPathA(HWND hwnd, int csidl, void *token,
                                              DWORD flags, char *path)
{
    const char *src = NULL;
    UNUSED(hwnd); UNUSED(token); UNUSED(flags);
    if (!path)
        return E_FAIL;
    path[0] = '\0';
    switch (csidl & 0xFF) {
    case CSIDL_APPDATA:        src = airlock_shell_get(AIRLOCK_ENV_APPDATA); break;
    case CSIDL_LOCAL_APPDATA:  src = airlock_shell_get(AIRLOCK_ENV_LOCALAPPDATA); break;
    case CSIDL_COMMON_APPDATA: src = airlock_shell_get(AIRLOCK_ENV_PROGRAMDATA); break;
    case CSIDL_WINDOWS:        src = airlock_shell_get(AIRLOCK_ENV_WINDIR); break;
    case CSIDL_SYSTEM: {
        snprintf(path, 260, "%s/system32", airlock_shell_get(AIRLOCK_ENV_WINDIR));
        path[259] = '\0';
        return S_OK;
    }
    case CSIDL_PROGRAM_FILES:  src = airlock_shell_get(AIRLOCK_ENV_PROGRAMFILES); break;
    case CSIDL_PROFILE:        src = airlock_shell_get(AIRLOCK_ENV_USERPROFILE); break;
    default: return E_FAIL;
    }
    if (!src || !*src)
        return E_FAIL;
    snprintf(path, 260, "%s", src);
    return S_OK;
}

static unsigned long WINAPI airlock_ShellExecuteA(HWND hwnd, const char *op,
                                                 const char *file, const char *params,
                                                 const char *dir, int show)
{
    UNUSED(hwnd); UNUSED(op); UNUSED(params); UNUSED(dir); UNUSED(show);
    if (file && (strncmp(file, "http://", 7) == 0 || strncmp(file, "https://", 8) == 0))
        airlock_desktop_open_url(file);
    AIRLOCK_LOG_INFO("SHELL32.ShellExecuteA(%s)", file ? file : "");
    return 42; /* >32 means success */
}

static const airlock_export_entry_t k_exports[] = {
    { "SHGetFolderPathA", (void *)&airlock_SHGetFolderPathA },
    { "ShellExecuteA",    (void *)&airlock_ShellExecuteA },
};

static const airlock_module_t k_mod = {
    "SHELL32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const airlock_module_t *airlock_win32_module_shell32(void) { return &k_mod; }
