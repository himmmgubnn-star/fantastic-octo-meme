/*
 * mod_kernel32.c — KERNEL32.dll implementation.
 *
 * The first Airlock Win32 module. Functions are registered as exported symbols
 * so the PE loader can bind imports. Currently most are functional stubs: they
 * log the call and return a harmless value. This is the skeleton the emulation
 * layer will flesh out with real implementations over time.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>

#include "airlock/airlock.h"
#include "airlock/win32.h"

/* ---- Windows types used by this module ---------------------------------- */
typedef void *HANDLE;
typedef void *HMODULE;
typedef int   BOOL;
typedef unsigned long DWORD;
typedef unsigned short WORD;
typedef void *LPVOID;
typedef const void *LPCVOID;
typedef unsigned int UINT;
typedef void (*LPTHREAD_START_ROUTINE)(LPVOID);

/* ---- Standard handles ---------------------------------------------------- */
#define INVALID_HANDLE_VALUE ((void *)(uintptr_t)-1)
#define STD_OUTPUT_HANDLE ((DWORD)-11)
#define STD_ERROR_HANDLE  ((DWORD)-12)

/* ---- Win32 calling convention ------------------------------------------- */
#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI /* SysV on Linux: our stubs use the default calling convention */
#endif

#define UNUSED(x) ((void)(x))

/* ---- Implementations ----------------------------------------------------- */

static void WINAPI airlock_ExitProcess(UINT code)
{
    AIRLOCK_LOG_INFO("KERNEL32.ExitProcess(%u) — terminating", code);
    exit((int)code);
}

static HANDLE WINAPI airlock_GetStdHandle(DWORD nStdHandle)
{
    (void)nStdHandle;
    AIRLOCK_LOG_TRACE("KERNEL32.GetStdHandle(%lu)", (unsigned long)nStdHandle);
    /* Placeholder — the console/pipe layer will map to the real descriptors. */
    return INVALID_HANDLE_VALUE;
}

static BOOL WINAPI airlock_WriteFile(HANDLE hFile, LPCVOID lpBuffer,
                                    DWORD nNumberOfBytesToWrite,
                                    DWORD *lpNumberOfBytesWritten,
                                    LPVOID lpOverlapped)
{
    UNUSED(hFile); UNUSED(lpBuffer); UNUSED(nNumberOfBytesToWrite);
    UNUSED(lpOverlapped);
    AIRLOCK_LOG_TRACE("KERNEL32.WriteFile(...)");
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = 0;
    return 0;
}

static HMODULE WINAPI airlock_GetModuleHandleA(const char *lpModuleName)
{
    UNUSED(lpModuleName);
    AIRLOCK_LOG_TRACE("KERNEL32.GetModuleHandleA(%s)",
                     lpModuleName ? lpModuleName : "(null)");
    return NULL; /* not yet implemented */
}

static HMODULE WINAPI airlock_LoadLibraryA(const char *lpLibFileName)
{
    AIRLOCK_LOG_INFO("KERNEL32.LoadLibraryA(%s) — stub",
                    lpLibFileName ? lpLibFileName : "(null)");
    return NULL;
}

static BOOL WINAPI airlock_FreeLibrary(HMODULE hLibModule)
{
    UNUSED(hLibModule);
    AIRLOCK_LOG_TRACE("KERNEL32.FreeLibrary(...)");
    return 1;
}

static LPVOID WINAPI airlock_GetProcAddress(HMODULE hModule, const char *lpProcName)
{
    UNUSED(hModule); UNUSED(lpProcName);
    AIRLOCK_LOG_TRACE("KERNEL32.GetProcAddress(hModule, %s) — stub",
                     lpProcName ? lpProcName : "(null)");
    return NULL;
}

static DWORD WINAPI airlock_GetLastError(void)
{
    AIRLOCK_LOG_TRACE("KERNEL32.GetLastError() — returns 0");
    return 0;
}

static void WINAPI airlock_SetLastError(DWORD dwErrCode)
{
    UNUSED(dwErrCode);
    AIRLOCK_LOG_TRACE("KERNEL32.SetLastError(%lu)", (unsigned long)dwErrCode);
}

static void WINAPI airlock_Sleep(DWORD dwMilliseconds)
{
    UNUSED(dwMilliseconds);
    AIRLOCK_LOG_TRACE("KERNEL32.Sleep(%lu)", (unsigned long)dwMilliseconds);
    /* Real implementation will nanosleep; stub returns immediately. */
}

/* ---- Registered export table -------------------------------------------- */

static const airlock_export_entry_t k_kernel32_exports[] = {
    { "ExitProcess",        (void *)&airlock_ExitProcess },
    { "GetStdHandle",       (void *)&airlock_GetStdHandle },
    { "WriteFile",          (void *)&airlock_WriteFile },
    { "GetModuleHandleA",   (void *)&airlock_GetModuleHandleA },
    { "LoadLibraryA",       (void *)&airlock_LoadLibraryA },
    { "FreeLibrary",        (void *)&airlock_FreeLibrary },
    { "GetProcAddress",     (void *)&airlock_GetProcAddress },
    { "GetLastError",       (void *)&airlock_GetLastError },
    { "SetLastError",       (void *)&airlock_SetLastError },
    { "Sleep",              (void *)&airlock_Sleep },
};

static const airlock_module_t k_kernel32_module = {
    "KERNEL32.dll",
    k_kernel32_exports,
    sizeof k_kernel32_exports / sizeof k_kernel32_exports[0],
};

/* Called from win32/init.c to register this module at startup. */
const airlock_module_t *airlock_win32_module_kernel32(void)
{
    return &k_kernel32_module;
}
