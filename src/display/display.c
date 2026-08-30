/*
 * display.c — virtual Windows display environment.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/display.h"

static cellar_display_t g_current;
static int g_have;

void cellar_display_init_default(cellar_display_t *d)
{
    cellar_monitor_t m;
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
    m.orientation = CELLAR_ORIENT_LANDSCAPE;
    m.primary = 1;
    d->monitors[0] = m;
    d->count = 1;
    d->mode = CELLAR_WINDOW_WINDOWED;
}

cellar_status_t cellar_display_add_monitor(cellar_display_t *d,
                                           const cellar_monitor_t *m)
{
    if (!d || !m)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (d->count >= 8)
        return CELLAR_ERR_OUT_OF_MEMORY;
    d->monitors[d->count++] = *m;
    return CELLAR_OK;
}

cellar_status_t cellar_display_set_mode(cellar_display_t *d,
                                        cellar_window_mode_t mode)
{
    if (!d)
        return CELLAR_ERR_INVALID_ARGUMENT;
    d->mode = mode;
    return CELLAR_OK;
}

const cellar_monitor_t *cellar_display_primary(const cellar_display_t *d)
{
    size_t i;
    if (!d || d->count == 0)
        return NULL;
    for (i = 0; i < d->count; i++)
        if (d->monitors[i].primary)
            return &d->monitors[i];
    return &d->monitors[0];
}

float cellar_display_scale(const cellar_monitor_t *m)
{
    if (!m || m->dpi == 0)
        return 1.0f;
    return (float)m->dpi / 96.0f;
}

cellar_display_t *cellar_display_current(void)
{
    if (!g_have) {
        cellar_display_init_default(&g_current);
        g_have = 1;
    }
    return &g_current;
}
