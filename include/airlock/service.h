/*
 * service.h — user-space Windows service manager.
 *
 * Applications that expect services (SQL, update helpers, anti-cheat launchers)
 * talk to this manager. Services stay isolated from systemd / host daemons
 * unless the operator explicitly configures otherwise — Airlock never installs
 * a system-wide Linux service on its own.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_SERVICE_H
#define AIRLOCK_SERVICE_H

#include <stddef.h>

#include "airlock.h"

typedef enum airlock_svc_state {
    AIRLOCK_SVC_STOPPED = 1,
    AIRLOCK_SVC_START_PENDING,
    AIRLOCK_SVC_RUNNING,
    AIRLOCK_SVC_STOP_PENDING
} airlock_svc_state_t;

typedef int (*airlock_svc_main_t)(int argc, char **argv);

typedef struct airlock_service {
    char name[64];
    airlock_svc_state_t state;
    airlock_svc_main_t  main;
    int user_space; /* always 1 in this build */
} airlock_service_t;

airlock_status_t airlock_svc_register(const char *name, airlock_svc_main_t main);
airlock_status_t airlock_svc_start(const char *name);
airlock_status_t airlock_svc_stop(const char *name);
const airlock_service_t *airlock_svc_query(const char *name);
size_t airlock_svc_list(airlock_service_t *out, size_t cap);

#endif /* AIRLOCK_SERVICE_H */
