/*
 * crash.h — crash-recovery diagnostics.
 *
 * When a Windows program under Airlock faults, the goal is a structured report
 * — not a bare "Program crashed" — capturing the module, thread, the Windows
 * API call in flight, the Linux syscall, loaded DLLs, and the compatibility
 * subsystem involved. This is the EXCEPTION diagnostic structure and a signal
 * handler that fills it in.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_CRASH_H
#define AIRLOCK_CRASH_H

#include <stdint.h>

#include "airlock.h"

typedef struct airlock_crash_info {
    uint32_t    signal;            /* fatal signal (SIGSEGV, SIGABRT, ...)   */
    uintptr_t   fault_addr;        /* faulting address                        */
    uint64_t    thread_id;         /* OS thread id (gettid)                  */
    char        module[128];       /* module where the fault occurred        */
    char        api_call[256];     /* Windows API call in flight (if any)    */
    char        linux_syscall[64]; /* Linux syscall context (best-effort)    */
    char        loaded_dlls[1024]; /* comma-separated loaded DLL list        */
    char        subsystem[64];     /* e.g. "graphics", "audio", "loader"    */
} airlock_crash_info_t;

/* Set the Windows API call currently being executed on this thread (read by
 * the crash handler). `subsystem` tags which Airlock subsystem it belongs to. */
void airlock_crash_set_current_api(const char *subsystem, const char *api);

/* Install handlers for common fatal signals. Returns AIRLOCK_OK on success. */
airlock_status_t airlock_crash_register_handler(void);

/* The most recent crash captured by the handler (or the last info written via
 * airlock_crash_fill). */
const airlock_crash_info_t *airlock_crash_last(void);

/* Fill a crash-info struct with current thread / API context plus the given
 * details (used by the handler and by tests without actually crashing). */
void airlock_crash_fill(airlock_crash_info_t *out, uint32_t signal,
                       uintptr_t fault_addr);

/* Format a crash report into `buf` (e.g. the EXCEPTION block). */
void airlock_crash_format(const airlock_crash_info_t *ci,
                         char *buf, size_t cap);

#endif /* AIRLOCK_CRASH_H */
