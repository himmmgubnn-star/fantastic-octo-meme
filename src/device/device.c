/*
 * device.c — Windows device compatibility registry.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/device.h"

#define CELLAR_MAX_DEV 32

static cellar_device_t g_dev[CELLAR_MAX_DEV];
static uint32_t g_next = 1;

void cellar_device_reset(void)
{
    memset(g_dev, 0, sizeof g_dev);
    g_next = 1;
}

cellar_status_t cellar_device_attach(cellar_dev_kind_t kind, const char *name,
                                     const char *backend, uint32_t *out_id)
{
    size_t i;
    cellar_device_t *d = NULL;
    for (i = 0; i < CELLAR_MAX_DEV; i++) {
        if (!g_dev[i].attached && g_dev[i].id == 0) {
            d = &g_dev[i];
            break;
        }
    }
    if (!d)
        return CELLAR_ERR_OUT_OF_MEMORY;
    memset(d, 0, sizeof *d);
    d->id = g_next++;
    d->kind = kind;
    d->attached = 1;
    snprintf(d->name, sizeof d->name, "%s", name ? name : "device");
    snprintf(d->backend, sizeof d->backend, "%s", backend ? backend : "Stub");
    if (out_id)
        *out_id = d->id;
    return CELLAR_OK;
}

cellar_status_t cellar_device_detach(uint32_t id)
{
    size_t i;
    for (i = 0; i < CELLAR_MAX_DEV; i++) {
        if (g_dev[i].id == id) {
            memset(&g_dev[i], 0, sizeof g_dev[i]);
            return CELLAR_OK;
        }
    }
    return CELLAR_ERR_INVALID_ARGUMENT;
}

const cellar_device_t *cellar_device_get(uint32_t id)
{
    size_t i;
    for (i = 0; i < CELLAR_MAX_DEV; i++)
        if (g_dev[i].id == id && g_dev[i].attached)
            return &g_dev[i];
    return NULL;
}

size_t cellar_device_list(cellar_device_t *out, size_t cap)
{
    size_t i, n = 0;
    if (!out)
        return 0;
    for (i = 0; i < CELLAR_MAX_DEV && n < cap; i++)
        if (g_dev[i].attached)
            out[n++] = g_dev[i];
    return n;
}
