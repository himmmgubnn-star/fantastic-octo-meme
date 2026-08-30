/*
 * service.c — user-space Windows service manager.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cellar/cellar.h"
#include "cellar/service.h"

#define CELLAR_MAX_SVC 32

static cellar_service_t g_svc[CELLAR_MAX_SVC];
static size_t g_n;

cellar_status_t cellar_svc_register(const char *name, cellar_svc_main_t main)
{
    size_t i;
    if (!name || !*name)
        return CELLAR_ERR_INVALID_ARGUMENT;
    for (i = 0; i < g_n; i++) {
        if (strcasecmp(g_svc[i].name, name) == 0) {
            g_svc[i].main = main;
            return CELLAR_OK;
        }
    }
    if (g_n >= CELLAR_MAX_SVC)
        return CELLAR_ERR_OUT_OF_MEMORY;
    memset(&g_svc[g_n], 0, sizeof g_svc[g_n]);
    snprintf(g_svc[g_n].name, sizeof g_svc[g_n].name, "%s", name);
    g_svc[g_n].state = CELLAR_SVC_STOPPED;
    g_svc[g_n].main = main;
    g_svc[g_n].user_space = 1;
    g_n++;
    return CELLAR_OK;
}

static cellar_service_t *find(const char *name)
{
    size_t i;
    for (i = 0; i < g_n; i++)
        if (strcasecmp(g_svc[i].name, name) == 0)
            return &g_svc[i];
    return NULL;
}

cellar_status_t cellar_svc_start(const char *name)
{
    cellar_service_t *s = name ? find(name) : NULL;
    int rc;
    if (!s)
        return CELLAR_ERR_INVALID_ARGUMENT;
    s->state = CELLAR_SVC_START_PENDING;
    if (s->main) {
        rc = s->main(0, NULL);
        (void)rc;
    }
    s->state = CELLAR_SVC_RUNNING;
    return CELLAR_OK;
}

cellar_status_t cellar_svc_stop(const char *name)
{
    cellar_service_t *s = name ? find(name) : NULL;
    if (!s)
        return CELLAR_ERR_INVALID_ARGUMENT;
    s->state = CELLAR_SVC_STOP_PENDING;
    s->state = CELLAR_SVC_STOPPED;
    return CELLAR_OK;
}

const cellar_service_t *cellar_svc_query(const char *name)
{
    return name ? find(name) : NULL;
}

size_t cellar_svc_list(cellar_service_t *out, size_t cap)
{
    size_t n;
    if (!out)
        return 0;
    n = g_n < cap ? g_n : cap;
    memcpy(out, g_svc, n * sizeof *out);
    return n;
}
