/*
 * mod_ntdll.c — ntdll.dll (a few NT APIs programs probe at startup).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/compat.h"
#include "airlock/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef long NTSTATUS;
typedef unsigned long ULONG;

typedef struct rtl_osversioninfo {
    ULONG dwOSVersionInfoSize;
    ULONG dwMajorVersion;
    ULONG dwMinorVersion;
    ULONG dwBuildNumber;
    ULONG dwPlatformId;
    char  szCSDVersion[128];
} rtl_osversioninfo_t;

static NTSTATUS WINAPI airlock_RtlGetVersion(rtl_osversioninfo_t *vi)
{
    const airlock_version_profile_t *p = airlock_version_profile(AIRLOCK_WIN_10);
    if (!vi)
        return (NTSTATUS)0xC0000001L; /* STATUS_UNSUCCESSFUL */
    vi->dwMajorVersion = p->major;
    vi->dwMinorVersion = p->minor;
    vi->dwBuildNumber  = p->build;
    vi->dwPlatformId   = 2;
    vi->szCSDVersion[0] = '\0';
    return 0; /* STATUS_SUCCESS */
}

static NTSTATUS WINAPI airlock_NtQueryInformationProcess(void *proc, ULONG cls,
                                                        void *info, ULONG len,
                                                        ULONG *retlen)
{
    UNUSED(proc); UNUSED(cls); UNUSED(info); UNUSED(len);
    if (retlen)
        *retlen = 0;
    return (NTSTATUS)0xC0000002L; /* STATUS_NOT_IMPLEMENTED */
}

static const airlock_export_entry_t k_exports[] = {
    { "RtlGetVersion",              (void *)&airlock_RtlGetVersion },
    { "NtQueryInformationProcess",  (void *)&airlock_NtQueryInformationProcess },
};

static const airlock_module_t k_mod = {
    "ntdll.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const airlock_module_t *airlock_win32_module_ntdll(void) { return &k_mod; }
