/*
 * mod_ole32.c — OLE32.dll (COM initialization and class creation).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/com.h"
#include "cellar/win32.h"

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

static HRESULT WINAPI cellar_CoInitializeEx(void *reserved, DWORD coinit)
{
    UNUSED(reserved);
    cellar_com_register_builtins();
    return cellar_com_init((coinit & COINIT_APARTMENTTHREADED)
                               ? CELLAR_APT_STA : CELLAR_APT_MTA) == CELLAR_OK
               ? S_OK : E_FAIL;
}

static void WINAPI cellar_CoUninitialize(void)
{
    cellar_com_uninit();
}

static HRESULT WINAPI cellar_CoCreateInstance(const cellar_guid_t *clsid,
                                              void *outer, DWORD ctx,
                                              const cellar_guid_t *iid,
                                              void **out)
{
    UNUSED(outer); UNUSED(ctx);
    if (!cellar_com_inited())
        cellar_CoInitializeEx(NULL, 0);
    return cellar_com_create(clsid, iid, out) == CELLAR_OK ? S_OK : E_FAIL;
}

static const cellar_export_entry_t k_exports[] = {
    { "CoInitializeEx",   (void *)&cellar_CoInitializeEx },
    { "CoUninitialize",   (void *)&cellar_CoUninitialize },
    { "CoCreateInstance", (void *)&cellar_CoCreateInstance },
};

static const cellar_module_t k_mod = {
    "ole32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const cellar_module_t *cellar_win32_module_ole32(void) { return &k_mod; }
