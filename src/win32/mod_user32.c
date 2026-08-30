/*
 * mod_user32.c — USER32.dll (windows, messages, metrics).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/desktop.h"
#include "cellar/display.h"
#include "cellar/notify.h"
#include "cellar/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef void *HWND;
typedef unsigned int UINT;
typedef int BOOL;

#define IDOK 1
#define SM_CXSCREEN 0
#define SM_CYSCREEN 1
#define SM_CXFULLSCREEN 16
#define SM_CYFULLSCREEN 17
#define SM_CMONITORS 80

static int WINAPI cellar_MessageBoxA(HWND hwnd, const char *text,
                                     const char *caption, UINT type)
{
    cellar_notification_t n;
    UNUSED(hwnd); UNUSED(type);
    CELLAR_LOG_INFO("USER32.MessageBoxA [%s] %s",
                    caption ? caption : "", text ? text : "");
    memset(&n, 0, sizeof n);
    snprintf(n.app, sizeof n.app, "user32");
    snprintf(n.summary, sizeof n.summary, "%s", caption ? caption : "Message");
    snprintf(n.body, sizeof n.body, "%s", text ? text : "");
    cellar_notify_show(&n);
    return IDOK;
}

static HWND WINAPI cellar_GetDesktopWindow(void)
{
    return (HWND)(uintptr_t)1;
}

static HWND WINAPI cellar_GetForegroundWindow(void)
{
    return (HWND)(uintptr_t)1;
}

static int WINAPI cellar_GetSystemMetrics(int nIndex)
{
    const cellar_monitor_t *m = cellar_display_primary(cellar_display_current());
    const cellar_display_t *d = cellar_display_current();
    uint32_t w = m ? m->width : 1920;
    uint32_t h = m ? m->height : 1080;
    switch (nIndex) {
    case SM_CXSCREEN:
    case SM_CXFULLSCREEN:
        return (int)w;
    case SM_CYSCREEN:
    case SM_CYFULLSCREEN:
        return (int)h;
    case SM_CMONITORS:
        return d ? (int)d->count : 1;
    default:
        return 0;
    }
}

static const cellar_export_entry_t k_exports[] = {
    { "MessageBoxA",          (void *)&cellar_MessageBoxA },
    { "GetDesktopWindow",     (void *)&cellar_GetDesktopWindow },
    { "GetForegroundWindow",  (void *)&cellar_GetForegroundWindow },
    { "GetSystemMetrics",     (void *)&cellar_GetSystemMetrics },
};

static const cellar_module_t k_mod = {
    "USER32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const cellar_module_t *cellar_win32_module_user32(void) { return &k_mod; }
