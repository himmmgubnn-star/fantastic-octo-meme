/*
 * service.c — user-space Windows service manager.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/service.h"

#define AIRLOCK_MAX_SVC 32

static airlock_service_t g_svc[AIRLOCK_MAX_SVC];
static size_t g_n;

airlock_status_t airlock_svc_register(const char *name, airlock_svc_main_t main)
{
    size_t i;
    if (!name || !*name)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    for (i = 0; i < g_n; i++) {
        if (strcasecmp(g_svc[i].name, name) == 0) {
            g_svc[i].main = main;
            return AIRLOCK_OK;
        }
    }
    if (g_n >= AIRLOCK_MAX_SVC)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    memset(&g_svc[g_n], 0, sizeof g_svc[g_n]);
    snprintf(g_svc[g_n].name, sizeof g_svc[g_n].name, "%s", name);
    g_svc[g_n].state = AIRLOCK_SVC_STOPPED;
    g_svc[g_n].main = main;
    g_svc[g_n].user_space = 1;
    g_n++;
    return AIRLOCK_OK;
}

static airlock_service_t *find(const char *name)
{
    size_t i;
    for (i = 0; i < g_n; i++)
        if (strcasecmp(g_svc[i].name, name) == 0)
            return &g_svc[i];
    return NULL;
}

airlock_status_t airlock_svc_start(const char *name)
{
    airlock_service_t *s = name ? find(name) : NULL;
    int rc;
    if (!s)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    s->state = AIRLOCK_SVC_START_PENDING;
    if (s->main) {
        rc = s->main(0, NULL);
        (void)rc;
    }
    s->state = AIRLOCK_SVC_RUNNING;
    return AIRLOCK_OK;
}

airlock_status_t airlock_svc_stop(const char *name)
{
    airlock_service_t *s = name ? find(name) : NULL;
    if (!s)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    s->state = AIRLOCK_SVC_STOP_PENDING;
    s->state = AIRLOCK_SVC_STOPPED;
    return AIRLOCK_OK;
}

const airlock_service_t *airlock_svc_query(const char *name)
{
    return name ? find(name) : NULL;
}

size_t airlock_svc_list(airlock_service_t *out, size_t cap)
{
    size_t n;
    if (!out)
        return 0;
    n = g_n < cap ? g_n : cap;
    memcpy(out, g_svc, n * sizeof *out);
    return n;
}
