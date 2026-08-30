/*
 * timer.c — high-resolution timing implementation.
 *
 * Uses CLOCK_MONOTONIC at nanosecond resolution via clock_gettime. Sleeps use
 * nanosleep (absolute via clock_nanosleep where available) so they remain
 * precise without busy-waiting; airlock_timer_spin_until provides an explicit
 * busy-wait for latency-critical, short windows.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include <string.h>
#include <time.h>

#include "airlock/airlock.h"
#include "airlock/timer.h"
#include "airlock/trace.h"

uint64_t airlock_timer_frequency(void)
{
    return 1000000000ull; /* nanoseconds per second */
}

uint64_t airlock_timer_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t airlock_timer_us(void) { return airlock_timer_ns() / 1000u; }
uint64_t airlock_timer_ms(void) { return airlock_timer_ns() / 1000000u; }

airlock_status_t airlock_timer_sleep_until(uint64_t deadline_ns)
{
    uint64_t now = airlock_timer_ns();
    struct timespec ts;
    if (deadline_ns <= now)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    ts.tv_sec  = (time_t)(deadline_ns / 1000000000ull);
    ts.tv_nsec = (long)(deadline_ns % 1000000000ull);
    while (clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &ts, NULL) != 0)
        ; /* EINTR: keep waiting toward the same absolute deadline */
    AIRLOCK_TRACE(AIRLOCK_TRACE_TIMER, "sleep_until(%llu) done",
                 (unsigned long long)deadline_ns);
    return AIRLOCK_OK;
}

void airlock_timer_spin_until(uint64_t deadline_ns)
{
    while (airlock_timer_ns() < deadline_ns)
        ; /* deliberate busy-wait */
}

/* ---- Frame-time tracking -------------------------------------------------- */

void airlock_frametime_init(airlock_frametime_t *ft)
{
    if (ft) {
        ft->count = 0;
        ft->pos = 0;
        ft->cpu_wait_ns = 0;
        ft->gpu_wait_ns = 0;
        ft->translate_wait_ns = 0;
    }
}

void airlock_frametime_add(airlock_frametime_t *ft, uint64_t frame_ns)
{
    if (!ft)
        return;
    ft->history[ft->pos] = frame_ns;
    ft->pos = (ft->pos + 1) % AIRLOCK_FRAMETIME_HISTORY;
    if (ft->count < AIRLOCK_FRAMETIME_HISTORY)
        ft->count++;
}

void airlock_frametime_set_waits(airlock_frametime_t *ft,
                                uint64_t cpu_ns, uint64_t gpu_ns,
                                uint64_t translate_ns)
{
    if (!ft)
        return;
    ft->cpu_wait_ns = cpu_ns;
    ft->gpu_wait_ns = gpu_ns;
    ft->translate_wait_ns = translate_ns;
}

void airlock_frametime_report(const airlock_frametime_t *ft,
                             airlock_frametime_stats_t *out)
{
    double sum = 0;
    uint64_t sorted[AIRLOCK_FRAMETIME_HISTORY];
    size_t i, n;
    if (!ft || !out)
        return;
    memset(out, 0, sizeof *out);

    n = ft->count;
    if (n == 0)
        return;

    for (i = 0; i < n; i++) {
        size_t idx = (ft->pos + AIRLOCK_FRAMETIME_HISTORY - n + i) %
                     AIRLOCK_FRAMETIME_HISTORY;
        sorted[i] = ft->history[idx];
        sum += (double)sorted[i];
    }

    /* Sort ascending for percentile / max. */
    for (i = 1; i < n; i++) {
        uint64_t key = sorted[i];
        size_t j = i;
        while (j > 0 && sorted[j - 1] > key) {
            sorted[j] = sorted[j - 1];
            j--;
        }
        sorted[j] = key;
    }

    out->frame_ms = (sum / (double)n) / 1e6;
    out->fps = n > 0 ? 1e9 / (sum / (double)n) : 0.0;
    /* 1% low = the frame duration at the 99th percentile (1% of frames are
     * worse). Index = ceil(0.99 * n) - 1. */
    {
        size_t idx = n ? (size_t)(0.99 * (double)n) - 1 : 0;
        if (idx >= n) idx = n - 1;
        out->low1_ms = (double)sorted[idx] / 1e6;
    }
    out->max_ms = (double)sorted[n - 1] / 1e6;
    out->cpu_wait_ms = (double)ft->cpu_wait_ns / 1e6;
    out->gpu_wait_ms = (double)ft->gpu_wait_ns / 1e6;
    out->translate_wait_ms = (double)ft->translate_wait_ns / 1e6;
}
