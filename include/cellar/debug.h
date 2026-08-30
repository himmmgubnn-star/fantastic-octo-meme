/*
 * debug.h — specialized debugger surface for Windows apps under Cellar.
 *
 * A snapshot of process / threads / modules / handles plus the last Windows
 * API and the exception that brought us here. Used by `cellar debug` and by
 * the crash path to produce a structured report instead of a bare abort.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_DEBUG_H
#define CELLAR_DEBUG_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

typedef struct cellar_debug_thread {
    uint32_t tid;
    char     state[16];
} cellar_debug_thread_t;

typedef struct cellar_debug_snapshot {
    char     process[128];
    uint32_t pid;
    uint32_t thread_count;
    uint32_t module_count;
    uint32_t handle_count;
    char     exception[64];
    char     module_at[128];
    uint32_t offset;
    char     last_api[128];
    char     subsystem[64];
    cellar_debug_thread_t threads[32];
} cellar_debug_snapshot_t;

void cellar_debug_begin(const char *process);
void cellar_debug_note_module(void);
void cellar_debug_note_handle(void);
void cellar_debug_note_thread(uint32_t tid, const char *state);
void cellar_debug_note_api(const char *api, const char *subsystem);
void cellar_debug_exception(const char *status, const char *module,
                            uint32_t offset);
void cellar_debug_snapshot(cellar_debug_snapshot_t *out);
void cellar_debug_report(const cellar_debug_snapshot_t *s);

#endif /* CELLAR_DEBUG_H */
