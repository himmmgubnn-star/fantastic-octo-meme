/*
 * notify.h — Windows → Linux notification translation.
 *
 * Windows balloon / toast notifications are recorded in a history ring and
 * optionally forwarded to the desktop via `notify-send` when present.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_NOTIFY_H
#define AIRLOCK_NOTIFY_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

typedef struct airlock_notification {
    char     app[64];
    char     summary[128];
    char     body[256];
    int      urgency; /* 0 low, 1 normal, 2 critical */
    uint32_t id;
} airlock_notification_t;

uint32_t airlock_notify_show(const airlock_notification_t *n);
airlock_status_t airlock_notify_close(uint32_t id);
size_t airlock_notify_history(airlock_notification_t *out, size_t cap);

#endif /* AIRLOCK_NOTIFY_H */
