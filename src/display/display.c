/*
 * display.c — virtual Windows display environment.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/display.h"

static airlock_display_t g_current;
static int g_have;

void airlock_display_init_default(airlock_display_t *d)
{
    airlock_monitor_t m;
    if (!d)
        return;
    memset(d, 0, sizeof *d);
    memset(&m, 0, sizeof m);
    snprintf(m.name, sizeof m.name, "DISPLAY1");
    m.width = 1920;
    m.height = 1080;
    m.dpi = 96;
    m.refresh_hz = 60;
    m.hdr = 0;
    m.orientation = AIRLOCK_ORIENT_LANDSCAPE;
    m.primary = 1;
    d->monitors[0] = m;
    d->count = 1;
    d->mode = AIRLOCK_WINDOW_WINDOWED;
}

airlock_status_t airlock_display_add_monitor(airlock_display_t *d,
                                           const airlock_monitor_t *m)
{
    if (!d || !m)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (d->count >= 8)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    d->monitors[d->count++] = *m;
    return AIRLOCK_OK;
}

airlock_status_t airlock_display_set_mode(airlock_display_t *d,
                                        airlock_window_mode_t mode)
{
    if (!d)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    d->mode = mode;
    return AIRLOCK_OK;
}

const airlock_monitor_t *airlock_display_primary(const airlock_display_t *d)
{
    size_t i;
    if (!d || d->count == 0)
        return NULL;
    for (i = 0; i < d->count; i++)
        if (d->monitors[i].primary)
            return &d->monitors[i];
    return &d->monitors[0];
}

float airlock_display_scale(const airlock_monitor_t *m)
{
    if (!m || m->dpi == 0)
        return 1.0f;
    return (float)m->dpi / 96.0f;
}

airlock_display_t *airlock_display_current(void)
{
    if (!g_have) {
        airlock_display_init_default(&g_current);
        g_have = 1;
    }
    return &g_current;
}
