/*
 * notify.h — Windows → Linux notification translation.
 *
 * Windows balloon / toast notifications are recorded in a history ring and
 * optionally forwarded to the desktop via `notify-send` when present.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_NOTIFY_H
#define CELLAR_NOTIFY_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

typedef struct cellar_notification {
    char     app[64];
    char     summary[128];
    char     body[256];
    int      urgency; /* 0 low, 1 normal, 2 critical */
    uint32_t id;
} cellar_notification_t;

uint32_t cellar_notify_show(const cellar_notification_t *n);
cellar_status_t cellar_notify_close(uint32_t id);
size_t cellar_notify_history(cellar_notification_t *out, size_t cap);

#endif /* CELLAR_NOTIFY_H */
