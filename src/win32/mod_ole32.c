/*
 * mod_ole32.c — OLE32.dll (COM initialization and class creation).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/com.h"
#include "airlock/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef int HRESULT;
typedef unsigned long DWORD;
#define S_OK 0
#define E_FAIL 0x80004005
#define COINIT_APARTMENTTHREADED 0x2u

static HRESULT WINAPI airlock_CoInitializeEx(void *reserved, DWORD coinit)
{
    UNUSED(reserved);
    airlock_com_register_builtins();
    return airlock_com_init((coinit & COINIT_APARTMENTTHREADED)
                               ? AIRLOCK_APT_STA : AIRLOCK_APT_MTA) == AIRLOCK_OK
               ? S_OK : E_FAIL;
}

static void WINAPI airlock_CoUninitialize(void)
{
    airlock_com_uninit();
}

static HRESULT WINAPI airlock_CoCreateInstance(const airlock_guid_t *clsid,
                                              void *outer, DWORD ctx,
                                              const airlock_guid_t *iid,
                                              void **out)
{
    UNUSED(outer); UNUSED(ctx);
    if (!airlock_com_inited())
        airlock_CoInitializeEx(NULL, 0);
    return airlock_com_create(clsid, iid, out) == AIRLOCK_OK ? S_OK : E_FAIL;
}

static const airlock_export_entry_t k_exports[] = {
    { "CoInitializeEx",   (void *)&airlock_CoInitializeEx },
    { "CoUninitialize",   (void *)&airlock_CoUninitialize },
    { "CoCreateInstance", (void *)&airlock_CoCreateInstance },
};

static const airlock_module_t k_mod = {
    "ole32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const airlock_module_t *airlock_win32_module_ole32(void) { return &k_mod; }
