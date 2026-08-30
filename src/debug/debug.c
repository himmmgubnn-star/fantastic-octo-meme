/*
 * debug.c — compatibility debugger snapshot.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/debug.h"
#include "cellar/platform.h"

static cellar_debug_snapshot_t g_snap;

void cellar_debug_begin(const char *process)
{
    memset(&g_snap, 0, sizeof g_snap);
    snprintf(g_snap.process, sizeof g_snap.process, "%s",
             process ? process : "unknown.exe");
    g_snap.pid = cellar_getpid();
    cellar_debug_note_thread(cellar_gettid(), "running");
}

void cellar_debug_note_module(void)
{
    g_snap.module_count++;
}

void cellar_debug_note_handle(void)
{
    g_snap.handle_count++;
}

void cellar_debug_note_thread(uint32_t tid, const char *state)
{
    if (g_snap.thread_count >= 32)
        return;
    g_snap.threads[g_snap.thread_count].tid = tid;
    snprintf(g_snap.threads[g_snap.thread_count].state,
             sizeof g_snap.threads[0].state, "%s", state ? state : "running");
    g_snap.thread_count++;
}

void cellar_debug_note_api(const char *api, const char *subsystem)
{
    snprintf(g_snap.last_api, sizeof g_snap.last_api, "%s", api ? api : "");
    snprintf(g_snap.subsystem, sizeof g_snap.subsystem, "%s",
             subsystem ? subsystem : "");
}

void cellar_debug_exception(const char *status, const char *module,
                            uint32_t offset)
{
    snprintf(g_snap.exception, sizeof g_snap.exception, "%s",
             status ? status : "STATUS_ACCESS_VIOLATION");
    snprintf(g_snap.module_at, sizeof g_snap.module_at, "%s",
             module ? module : "");
    g_snap.offset = offset;
}

void cellar_debug_snapshot(cellar_debug_snapshot_t *out)
{
    if (out)
        *out = g_snap;
}

void cellar_debug_report(const cellar_debug_snapshot_t *s)
{
    if (!s)
        return;
    printf("CELLAR DEBUGGER\n");
    printf("\n");
    printf("Process: %s\n", s->process);
    printf("PID: %u\n", s->pid);
    printf("\n");
    printf("Threads: %u\n", s->thread_count);
    printf("Modules: %u\n", s->module_count);
    printf("Handles: %u\n", s->handle_count);
    printf("\n");
    if (s->exception[0]) {
        printf("Exception:\n");
        printf("%s\n", s->exception);
        printf("\n");
        printf("Module:\n");
        printf("%s + 0x%X\n", s->module_at, s->offset);
        printf("\n");
    }
    printf("Last Windows API:\n");
    printf("%s\n", s->last_api[0] ? s->last_api : "(none)");
    printf("\n");
    printf("Likely subsystem:\n");
    printf("%s\n", s->subsystem[0] ? s->subsystem : "(unknown)");
}
