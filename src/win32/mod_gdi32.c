/*
 * mod_gdi32.c — GDI32.dll (device caps, a handful of DC calls).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>

#include "cellar/cellar.h"
#include "cellar/display.h"
#include "cellar/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef void *HDC;
typedef int BOOL;

#define HORZRES    8
#define VERTRES    10
#define LOGPIXELSX 88
#define LOGPIXELSY 90
#define NUMCOLORS  24
#define BITSPIXEL  12
#define VREFRESH   116

static HDC WINAPI cellar_CreateCompatibleDC(HDC hdc)
{
    UNUSED(hdc);
    return (HDC)(uintptr_t)0xD00;
}

static BOOL WINAPI cellar_DeleteDC(HDC hdc)
{
    UNUSED(hdc);
    return 1;
}

static int WINAPI cellar_GetDeviceCaps(HDC hdc, int index)
{
    const cellar_monitor_t *m = cellar_display_primary(cellar_display_current());
    UNUSED(hdc);
    if (!m)
        return 0;
    switch (index) {
    case HORZRES:    return (int)m->width;
    case VERTRES:    return (int)m->height;
    case LOGPIXELSX:
    case LOGPIXELSY: return (int)m->dpi;
    case BITSPIXEL:  return m->hdr ? 30 : 24;
    case NUMCOLORS:  return -1;
    case VREFRESH:   return (int)m->refresh_hz;
    default:         return 0;
    }
}

static const cellar_export_entry_t k_exports[] = {
    { "CreateCompatibleDC", (void *)&cellar_CreateCompatibleDC },
    { "DeleteDC",           (void *)&cellar_DeleteDC },
    { "GetDeviceCaps",      (void *)&cellar_GetDeviceCaps },
};

static const cellar_module_t k_mod = {
    "gdi32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const cellar_module_t *cellar_win32_module_gdi32(void) { return &k_mod; }
