/*
 * a11y.h — Windows accessibility compatibility.
 *
 * A small MSAA/UI-Automation-style tree that Airlock can later project onto
 * AT-SPI. Accessibility is easy to overlook in a compatibility layer; this
 * gives Windows apps a place to publish name/role/value for assistive tech.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_A11Y_H
#define AIRLOCK_A11Y_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

typedef enum airlock_a11y_role {
    AIRLOCK_A11Y_WINDOW = 0,
    AIRLOCK_A11Y_BUTTON,
    AIRLOCK_A11Y_TEXT,
    AIRLOCK_A11Y_MENU,
    AIRLOCK_A11Y_CHECKBOX,
    AIRLOCK_A11Y_LIST
} airlock_a11y_role_t;

typedef struct airlock_a11y_node {
    uint32_t id;
    airlock_a11y_role_t role;
    char     name[64];
    char     value[128];
    uint32_t parent;
    uint32_t child_count;
} airlock_a11y_node_t;

uint32_t airlock_a11y_create(uint32_t parent, airlock_a11y_role_t role,
                            const char *name);
airlock_status_t airlock_a11y_set_value(uint32_t id, const char *value);
const airlock_a11y_node_t *airlock_a11y_get(uint32_t id);
size_t airlock_a11y_children(uint32_t id, uint32_t *out, size_t cap);
void airlock_a11y_reset(void);

#endif /* AIRLOCK_A11Y_H */
