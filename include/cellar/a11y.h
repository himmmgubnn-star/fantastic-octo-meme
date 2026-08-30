/*
 * a11y.h — Windows accessibility compatibility.
 *
 * A small MSAA/UI-Automation-style tree that Cellar can later project onto
 * AT-SPI. Accessibility is easy to overlook in a compatibility layer; this
 * gives Windows apps a place to publish name/role/value for assistive tech.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_A11Y_H
#define CELLAR_A11Y_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

typedef enum cellar_a11y_role {
    CELLAR_A11Y_WINDOW = 0,
    CELLAR_A11Y_BUTTON,
    CELLAR_A11Y_TEXT,
    CELLAR_A11Y_MENU,
    CELLAR_A11Y_CHECKBOX,
    CELLAR_A11Y_LIST
} cellar_a11y_role_t;

typedef struct cellar_a11y_node {
    uint32_t id;
    cellar_a11y_role_t role;
    char     name[64];
    char     value[128];
    uint32_t parent;
    uint32_t child_count;
} cellar_a11y_node_t;

uint32_t cellar_a11y_create(uint32_t parent, cellar_a11y_role_t role,
                            const char *name);
cellar_status_t cellar_a11y_set_value(uint32_t id, const char *value);
const cellar_a11y_node_t *cellar_a11y_get(uint32_t id);
size_t cellar_a11y_children(uint32_t id, uint32_t *out, size_t cap);
void cellar_a11y_reset(void);

#endif /* CELLAR_A11Y_H */
