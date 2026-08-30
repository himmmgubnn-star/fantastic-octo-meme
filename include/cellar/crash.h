/*
 * crash.h — crash-recovery diagnostics.
 *
 * When a Windows program under Cellar faults, the goal is a structured report
 * — not a bare "Program crashed" — capturing the module, thread, the Windows
 * API call in flight, the Linux syscall, loaded DLLs, and the compatibility
 * subsystem involved. This is the EXCEPTION diagnostic structure and a signal
 * handler that fills it in.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_CRASH_H
#define CELLAR_CRASH_H

#include <stdint.h>

#include "cellar.h"

typedef struct cellar_crash_info {
    uint32_t    signal;            /* fatal signal (SIGSEGV, SIGABRT, ...)   */
    uintptr_t   fault_addr;        /* faulting address                        */
    uint64_t    thread_id;         /* OS thread id (gettid)                  */
    char        module[128];       /* module where the fault occurred        */
    char        api_call[256];     /* Windows API call in flight (if any)    */
    char        linux_syscall[64]; /* Linux syscall context (best-effort)    */
    char        loaded_dlls[1024]; /* comma-separated loaded DLL list        */
    char        subsystem[64];     /* e.g. "graphics", "audio", "loader"    */
} cellar_crash_info_t;

/* Set the Windows API call currently being executed on this thread (read by
 * the crash handler). `subsystem` tags which Cellar subsystem it belongs to. */
void cellar_crash_set_current_api(const char *subsystem, const char *api);

/* Install handlers for common fatal signals. Returns CELLAR_OK on success. */
cellar_status_t cellar_crash_register_handler(void);

/* The most recent crash captured by the handler (or the last info written via
 * cellar_crash_fill). */
const cellar_crash_info_t *cellar_crash_last(void);

/* Fill a crash-info struct with current thread / API context plus the given
 * details (used by the handler and by tests without actually crashing). */
void cellar_crash_fill(cellar_crash_info_t *out, uint32_t signal,
                       uintptr_t fault_addr);

/* Format a crash report into `buf` (e.g. the EXCEPTION block). */
void cellar_crash_format(const cellar_crash_info_t *ci,
                         char *buf, size_t cap);

#endif /* CELLAR_CRASH_H */
