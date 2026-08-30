/*
 * mod_version.c — VERSION.dll + GetVersionExA (historically kernel32).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/compat.h"
#include "cellar/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef unsigned long DWORD;
typedef int BOOL;

typedef struct osversioninfoa {
    DWORD dwOSVersionInfoSize;
    DWORD dwMajorVersion;
    DWORD dwMinorVersion;
    DWORD dwBuildNumber;
    DWORD dwPlatformId;
    char  szCSDVersion[128];
} osversioninfoa_t;

static DWORD WINAPI cellar_GetFileVersionInfoSizeA(const char *path, DWORD *handle)
{
    UNUSED(path);
    if (handle)
        *handle = 0;
    return 0; /* no version resource in this milestone */
}

static BOOL WINAPI cellar_GetFileVersionInfoA(const char *path, DWORD handle,
                                              DWORD len, void *data)
{
    UNUSED(path); UNUSED(handle); UNUSED(len); UNUSED(data);
    return 0;
}

static BOOL WINAPI cellar_GetVersionExA(osversioninfoa_t *vi)
{
    const cellar_version_profile_t *p = cellar_version_profile(CELLAR_WIN_10);
    if (!vi || vi->dwOSVersionInfoSize < 20)
        return 0;
    vi->dwMajorVersion = p->major;
    vi->dwMinorVersion = p->minor;
    vi->dwBuildNumber  = p->build;
    vi->dwPlatformId   = 2; /* VER_PLATFORM_WIN32_NT */
    vi->szCSDVersion[0] = '\0';
    return 1;
}

static const cellar_export_entry_t k_exports[] = {
    { "GetFileVersionInfoSizeA", (void *)&cellar_GetFileVersionInfoSizeA },
    { "GetFileVersionInfoA",     (void *)&cellar_GetFileVersionInfoA },
    { "GetVersionExA",           (void *)&cellar_GetVersionExA },
};

static const cellar_module_t k_mod = {
    "version.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const cellar_module_t *cellar_win32_module_version(void) { return &k_mod; }
