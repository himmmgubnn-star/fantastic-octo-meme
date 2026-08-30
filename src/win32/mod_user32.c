/*
 * mod_user32.c — USER32.dll (windows, messages, metrics).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/desktop.h"
#include "airlock/display.h"
#include "airlock/notify.h"
#include "airlock/win32.h"

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

static int WINAPI airlock_MessageBoxA(HWND hwnd, const char *text,
                                     const char *caption, UINT type)
{
    airlock_notification_t n;
    UNUSED(hwnd); UNUSED(type);
    AIRLOCK_LOG_INFO("USER32.MessageBoxA [%s] %s",
                    caption ? caption : "", text ? text : "");
    memset(&n, 0, sizeof n);
    snprintf(n.app, sizeof n.app, "user32");
    snprintf(n.summary, sizeof n.summary, "%s", caption ? caption : "Message");
    snprintf(n.body, sizeof n.body, "%s", text ? text : "");
    airlock_notify_show(&n);
    return IDOK;
}

static HWND WINAPI airlock_GetDesktopWindow(void)
{
    return (HWND)(uintptr_t)1;
}

static HWND WINAPI airlock_GetForegroundWindow(void)
{
    return (HWND)(uintptr_t)1;
}

static int WINAPI airlock_GetSystemMetrics(int nIndex)
{
    const airlock_monitor_t *m = airlock_display_primary(airlock_display_current());
    const airlock_display_t *d = airlock_display_current();
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

static const airlock_export_entry_t k_exports[] = {
    { "MessageBoxA",          (void *)&airlock_MessageBoxA },
    { "GetDesktopWindow",     (void *)&airlock_GetDesktopWindow },
    { "GetForegroundWindow",  (void *)&airlock_GetForegroundWindow },
    { "GetSystemMetrics",     (void *)&airlock_GetSystemMetrics },
};

static const airlock_module_t k_mod = {
    "USER32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const airlock_module_t *airlock_win32_module_user32(void) { return &k_mod; }
