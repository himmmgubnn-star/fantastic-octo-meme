/*
 * a11y.c — accessibility tree (MSAA/UIA-shaped, AT-SPI-ready).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "airlock/a11y.h"
#include "airlock/airlock.h"

#define AIRLOCK_A11Y_MAX 64

static airlock_a11y_node_t g_nodes[AIRLOCK_A11Y_MAX];
static uint32_t g_next = 1;

static void ensure_root(void)
{
    if (g_nodes[1].id)
        return;
    g_nodes[1].id = 1;
    g_nodes[1].role = AIRLOCK_A11Y_WINDOW;
    snprintf(g_nodes[1].name, sizeof g_nodes[1].name, "Desktop");
    g_nodes[1].parent = 0;
    g_next = 2;
}

void airlock_a11y_reset(void)
{
    memset(g_nodes, 0, sizeof g_nodes);
    g_next = 1;
}

uint32_t airlock_a11y_create(uint32_t parent, airlock_a11y_role_t role,
                            const char *name)
{
    uint32_t id;
    ensure_root();
    if (g_next >= AIRLOCK_A11Y_MAX)
        return 0;
    if (parent == 0)
        parent = 1;
    if (parent >= AIRLOCK_A11Y_MAX || g_nodes[parent].id == 0)
        return 0;
    id = g_next++;
    memset(&g_nodes[id], 0, sizeof g_nodes[id]);
    g_nodes[id].id = id;
    g_nodes[id].role = role;
    g_nodes[id].parent = parent;
    snprintf(g_nodes[id].name, sizeof g_nodes[id].name, "%s", name ? name : "");
    g_nodes[parent].child_count++;
    return id;
}

airlock_status_t airlock_a11y_set_value(uint32_t id, const char *value)
{
    if (id == 0 || id >= AIRLOCK_A11Y_MAX || g_nodes[id].id == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    snprintf(g_nodes[id].value, sizeof g_nodes[id].value, "%s",
             value ? value : "");
    return AIRLOCK_OK;
}

const airlock_a11y_node_t *airlock_a11y_get(uint32_t id)
{
    if (id == 0 || id >= AIRLOCK_A11Y_MAX || g_nodes[id].id == 0)
        return NULL;
    return &g_nodes[id];
}

size_t airlock_a11y_children(uint32_t id, uint32_t *out, size_t cap)
{
    size_t n = 0;
    uint32_t i;
    if (!out || id >= AIRLOCK_A11Y_MAX)
        return 0;
    ensure_root();
    for (i = 1; i < g_next && n < cap; i++) {
        if (g_nodes[i].id && g_nodes[i].parent == id)
            out[n++] = i;
    }
    return n;
}
