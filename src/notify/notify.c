/*
 * notify.c — Windows → Linux notifications.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/notify.h"

#define CELLAR_NOTIFY_CAP 16

static cellar_notification_t g_hist[CELLAR_NOTIFY_CAP];
static size_t g_n;
static uint32_t g_next = 1;

uint32_t cellar_notify_show(const cellar_notification_t *n)
{
    cellar_notification_t rec;
    if (!n)
        return 0;
    rec = *n;
    rec.id = g_next++;
    if (g_n < CELLAR_NOTIFY_CAP)
        g_hist[g_n++] = rec;
    else {
        memmove(g_hist, g_hist + 1, (CELLAR_NOTIFY_CAP - 1) * sizeof g_hist[0]);
        g_hist[CELLAR_NOTIFY_CAP - 1] = rec;
    }
    /* Best-effort desktop forward. Never fail the API if notify-send is missing. */
    {
        char cmd[640];
        snprintf(cmd, sizeof cmd, "notify-send -u %s -- '%s' '%s' >/dev/null 2>&1",
                 rec.urgency >= 2 ? "critical" :
                 rec.urgency == 0 ? "low" : "normal",
                 rec.summary, rec.body);
        /* Intentionally not system()'d in the library: tests must not spawn.
         * The CLI can call notify-send itself. History is the source of truth. */
        (void)cmd;
    }
    return rec.id;
}

cellar_status_t cellar_notify_close(uint32_t id)
{
    size_t i;
    for (i = 0; i < g_n; i++) {
        if (g_hist[i].id == id) {
            memmove(&g_hist[i], &g_hist[i + 1], (g_n - i - 1) * sizeof g_hist[0]);
            g_n--;
            return CELLAR_OK;
        }
    }
    return CELLAR_ERR_INVALID_ARGUMENT;
}

size_t cellar_notify_history(cellar_notification_t *out, size_t cap)
{
    size_t n;
    if (!out)
        return 0;
    n = g_n < cap ? g_n : cap;
    memcpy(out, g_hist, n * sizeof *out);
    return n;
}
