/*
 * device.c — Windows device compatibility registry.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/device.h"

#define AIRLOCK_MAX_DEV 32

static airlock_device_t g_dev[AIRLOCK_MAX_DEV];
static uint32_t g_next = 1;

void airlock_device_reset(void)
{
    memset(g_dev, 0, sizeof g_dev);
    g_next = 1;
}

airlock_status_t airlock_device_attach(airlock_dev_kind_t kind, const char *name,
                                     const char *backend, uint32_t *out_id)
{
    size_t i;
    airlock_device_t *d = NULL;
    for (i = 0; i < AIRLOCK_MAX_DEV; i++) {
        if (!g_dev[i].attached && g_dev[i].id == 0) {
            d = &g_dev[i];
            break;
        }
    }
    if (!d)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    memset(d, 0, sizeof *d);
    d->id = g_next++;
    d->kind = kind;
    d->attached = 1;
    snprintf(d->name, sizeof d->name, "%s", name ? name : "device");
    snprintf(d->backend, sizeof d->backend, "%s", backend ? backend : "Stub");
    if (out_id)
        *out_id = d->id;
    return AIRLOCK_OK;
}

airlock_status_t airlock_device_detach(uint32_t id)
{
    size_t i;
    for (i = 0; i < AIRLOCK_MAX_DEV; i++) {
        if (g_dev[i].id == id) {
            memset(&g_dev[i], 0, sizeof g_dev[i]);
            return AIRLOCK_OK;
        }
    }
    return AIRLOCK_ERR_INVALID_ARGUMENT;
}

const airlock_device_t *airlock_device_get(uint32_t id)
{
    size_t i;
    for (i = 0; i < AIRLOCK_MAX_DEV; i++)
        if (g_dev[i].id == id && g_dev[i].attached)
            return &g_dev[i];
    return NULL;
}

size_t airlock_device_list(airlock_device_t *out, size_t cap)
{
    size_t i, n = 0;
    if (!out)
        return 0;
    for (i = 0; i < AIRLOCK_MAX_DEV && n < cap; i++)
        if (g_dev[i].attached)
            out[n++] = g_dev[i];
    return n;
}
