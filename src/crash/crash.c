/*
 * crash.c — crash diagnostics implementation.
 *
 * The handler records the faulting signal, the current thread, the Windows API
 * call in flight (via a thread-local hook set by the Win32 layer), and the
 * loaded DLL list, then prints a structured EXCEPTION report. For diagnostics
 * it does not attempt to unwind the guest stack (that requires the execution
 * layer); it captures what a compatibility layer can know at this stage.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "airlock/airlock.h"
#include "airlock/crash.h"
#include "airlock/platform.h"
#include "airlock/win32.h"

/* ---- Thread-local current-API hook ---------------------------------------- */

static _Thread_local char t_api[256];
static _Thread_local char t_subsystem[64];

void airlock_crash_set_current_api(const char *subsystem, const char *api)
{
    if (subsystem)
        snprintf(t_subsystem, sizeof t_subsystem, "%s", subsystem);
    if (api)
        snprintf(t_api, sizeof t_api, "%s", api);
}

/* ---- Crash info ----------------------------------------------------------- */

static airlock_crash_info_t g_last;

const airlock_crash_info_t *airlock_crash_last(void) { return &g_last; }

static void list_dlls(char *out, size_t cap)
{
    size_t i, n;
    const airlock_module_t *m;
    size_t pos = 0;
    n = airlock_win32_module_count();
    for (i = 0; i < n; i++) {
        m = airlock_win32_module_at(i);
        if (!m)
            continue;
        pos += (size_t)snprintf(out + pos, cap - pos, "%s%s",
                                pos ? "," : "", m->name);
        if (pos >= cap)
            break;
    }
}

void airlock_crash_fill(airlock_crash_info_t *out, uint32_t signal,
                       uintptr_t fault_addr)
{
    if (!out)
        return;
    memset(out, 0, sizeof *out);
    out->signal = signal;
    out->fault_addr = fault_addr;
    out->thread_id = airlock_gettid();
    if (t_subsystem[0])
        snprintf(out->subsystem, sizeof out->subsystem, "%s", t_subsystem);
    if (t_api[0])
        snprintf(out->api_call, sizeof out->api_call, "%s", t_api);
    snprintf(out->module, sizeof out->module, "%s",
             out->api_call[0] ? "(see API call)" : "(unknown)");
    snprintf(out->linux_syscall, sizeof out->linux_syscall, "(not captured)");
    list_dlls(out->loaded_dlls, sizeof out->loaded_dlls);
}

static void on_fatal_signal(int sig, siginfo_t *si, void *uc)
{
    (void)uc;
    airlock_crash_fill(&g_last, (uint32_t)sig,
                      (uintptr_t)(si ? si->si_addr : NULL));
    /* Print a structured report to stderr, then re-raise with default. */
    {
        char buf[2048];
        airlock_crash_format(&g_last, buf, sizeof buf);
        fputs(buf, stderr);
    }
    signal(sig, SIG_DFL);
    raise(sig);
}

airlock_status_t airlock_crash_register_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof sa);
    sa.sa_sigaction = on_fatal_signal;
    sa.sa_flags = SA_SIGINFO;
    sigemptyset(&sa.sa_mask);
    if (sigaction(SIGSEGV, &sa, NULL) != 0)
        return AIRLOCK_ERR_NOT_IMPLEMENTED;
    if (sigaction(SIGABRT, &sa, NULL) != 0)
        return AIRLOCK_ERR_NOT_IMPLEMENTED;
    if (sigaction(SIGBUS, &sa, NULL) != 0)
        return AIRLOCK_ERR_NOT_IMPLEMENTED;
    if (sigaction(SIGFPE, &sa, NULL) != 0)
        return AIRLOCK_ERR_NOT_IMPLEMENTED;
    return AIRLOCK_OK;
}

static const char *signal_name(uint32_t s)
{
    switch (s) {
    case SIGSEGV: return "SIGSEGV (invalid memory access)";
    case SIGABRT: return "SIGABRT (abort)";
    case SIGBUS:  return "SIGBUS (bus error)";
    case SIGFPE:  return "SIGFPE (arithmetic error)";
    default:      return "unknown";
    }
}

void airlock_crash_format(const airlock_crash_info_t *ci, char *buf, size_t cap)
{
    if (!ci || !buf)
        return;
    snprintf(buf, cap,
        "EXCEPTION\n"
        "├── signal:    %s\n"
        "├── module:    %s\n"
        "├── thread:    %llu\n"
        "├── Windows API call: %s\n"
        "├── Linux syscall:    %s\n"
        "├── subsystem: %s\n"
        "└── loaded DLLs: %s\n",
        signal_name(ci->signal),
        ci->module[0] ? ci->module : "(unknown)",
        (unsigned long long)ci->thread_id,
        ci->api_call[0] ? ci->api_call : "(none)",
        ci->linux_syscall,
        ci->subsystem[0] ? ci->subsystem : "(none)",
        ci->loaded_dlls[0] ? ci->loaded_dlls : "(none)");
}
