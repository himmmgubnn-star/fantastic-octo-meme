/*
 * service.h — user-space Windows service manager.
 *
 * Applications that expect services (SQL, update helpers, anti-cheat launchers)
 * talk to this manager. Services stay isolated from systemd / host daemons
 * unless the operator explicitly configures otherwise — Cellar never installs
 * a system-wide Linux service on its own.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_SERVICE_H
#define CELLAR_SERVICE_H

#include <stddef.h>

#include "cellar.h"

typedef enum cellar_svc_state {
    CELLAR_SVC_STOPPED = 1,
    CELLAR_SVC_START_PENDING,
    CELLAR_SVC_RUNNING,
    CELLAR_SVC_STOP_PENDING
} cellar_svc_state_t;

typedef int (*cellar_svc_main_t)(int argc, char **argv);

typedef struct cellar_service {
    char name[64];
    cellar_svc_state_t state;
    cellar_svc_main_t  main;
    int user_space; /* always 1 in this build */
} cellar_service_t;

cellar_status_t cellar_svc_register(const char *name, cellar_svc_main_t main);
cellar_status_t cellar_svc_start(const char *name);
cellar_status_t cellar_svc_stop(const char *name);
const cellar_service_t *cellar_svc_query(const char *name);
size_t cellar_svc_list(cellar_service_t *out, size_t cap);

#endif /* CELLAR_SERVICE_H */
