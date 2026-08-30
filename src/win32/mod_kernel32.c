/*
 * mod_kernel32.c — KERNEL32.dll implementation.
 *
 * The first Cellar Win32 module. Functions are registered as exported symbols
 * so the PE loader can bind imports. Currently most are functional stubs: they
 * log the call and return a harmless value. This is the skeleton the emulation
 * layer will flesh out with real implementations over time.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>

#include "cellar/cellar.h"
#include "cellar/win32.h"

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

static void WINAPI cellar_ExitProcess(UINT code)
{
    CELLAR_LOG_INFO("KERNEL32.ExitProcess(%u) — terminating", code);
    exit((int)code);
}

static HANDLE WINAPI cellar_GetStdHandle(DWORD nStdHandle)
{
    (void)nStdHandle;
    CELLAR_LOG_TRACE("KERNEL32.GetStdHandle(%lu)", (unsigned long)nStdHandle);
    /* Placeholder — the console/pipe layer will map to the real descriptors. */
    return INVALID_HANDLE_VALUE;
}

static BOOL WINAPI cellar_WriteFile(HANDLE hFile, LPCVOID lpBuffer,
                                    DWORD nNumberOfBytesToWrite,
                                    DWORD *lpNumberOfBytesWritten,
                                    LPVOID lpOverlapped)
{
    UNUSED(hFile); UNUSED(lpBuffer); UNUSED(nNumberOfBytesToWrite);
    UNUSED(lpOverlapped);
    CELLAR_LOG_TRACE("KERNEL32.WriteFile(...)");
    if (lpNumberOfBytesWritten)
        *lpNumberOfBytesWritten = 0;
    return 0;
}

static HMODULE WINAPI cellar_GetModuleHandleA(const char *lpModuleName)
{
    UNUSED(lpModuleName);
    CELLAR_LOG_TRACE("KERNEL32.GetModuleHandleA(%s)",
                     lpModuleName ? lpModuleName : "(null)");
    return NULL; /* not yet implemented */
}

static HMODULE WINAPI cellar_LoadLibraryA(const char *lpLibFileName)
{
    CELLAR_LOG_INFO("KERNEL32.LoadLibraryA(%s) — stub",
                    lpLibFileName ? lpLibFileName : "(null)");
    return NULL;
}

static BOOL WINAPI cellar_FreeLibrary(HMODULE hLibModule)
{
    UNUSED(hLibModule);
    CELLAR_LOG_TRACE("KERNEL32.FreeLibrary(...)");
    return 1;
}

static LPVOID WINAPI cellar_GetProcAddress(HMODULE hModule, const char *lpProcName)
{
    UNUSED(hModule); UNUSED(lpProcName);
    CELLAR_LOG_TRACE("KERNEL32.GetProcAddress(hModule, %s) — stub",
                     lpProcName ? lpProcName : "(null)");
    return NULL;
}

static DWORD WINAPI cellar_GetLastError(void)
{
    CELLAR_LOG_TRACE("KERNEL32.GetLastError() — returns 0");
    return 0;
}

static void WINAPI cellar_SetLastError(DWORD dwErrCode)
{
    UNUSED(dwErrCode);
    CELLAR_LOG_TRACE("KERNEL32.SetLastError(%lu)", (unsigned long)dwErrCode);
}

static void WINAPI cellar_Sleep(DWORD dwMilliseconds)
{
    UNUSED(dwMilliseconds);
    CELLAR_LOG_TRACE("KERNEL32.Sleep(%lu)", (unsigned long)dwMilliseconds);
    /* Real implementation will nanosleep; stub returns immediately. */
}

/* ---- Registered export table -------------------------------------------- */

static const cellar_export_entry_t k_kernel32_exports[] = {
    { "ExitProcess",        (void *)&cellar_ExitProcess },
    { "GetStdHandle",       (void *)&cellar_GetStdHandle },
    { "WriteFile",          (void *)&cellar_WriteFile },
    { "GetModuleHandleA",   (void *)&cellar_GetModuleHandleA },
    { "LoadLibraryA",       (void *)&cellar_LoadLibraryA },
    { "FreeLibrary",        (void *)&cellar_FreeLibrary },
    { "GetProcAddress",     (void *)&cellar_GetProcAddress },
    { "GetLastError",       (void *)&cellar_GetLastError },
    { "SetLastError",       (void *)&cellar_SetLastError },
    { "Sleep",              (void *)&cellar_Sleep },
};

static const cellar_module_t k_kernel32_module = {
    "KERNEL32.dll",
    k_kernel32_exports,
    sizeof k_kernel32_exports / sizeof k_kernel32_exports[0],
};

/* Called from win32/init.c to register this module at startup. */
const cellar_module_t *cellar_win32_module_kernel32(void)
{
    return &k_kernel32_module;
}
