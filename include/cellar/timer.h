/*
 * timer.h — high-resolution timing for Windows apps and frame diagnostics.
 *
 * Windows games depend heavily on precise timing (QueryPerformanceCounter,
 * timeGetTime, DWM frame pacing). Cellar provides a calibrated monotonic
 * clock with nanosecond resolution, deadline sleep and spin primitives, and a
 * frame-time tracker that reports FPS, 1% low, average, maximum, and
 * CPU/GPU/translation wait components.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_TIMER_H
#define CELLAR_TIMER_H

#include <stdint.h>
#include <stddef.h>

/* ---- Monotonic high-resolution clock ------------------------------------- */

/* Current monotonic time in nanoseconds (see timer.c for calibration). */
uint64_t cellar_timer_ns(void);

/* Same value in microseconds / milliseconds. */
uint64_t cellar_timer_us(void);
uint64_t cellar_timer_ms(void);

/* Nominal ticks per second (1e9 on a nanosecond clock). */
uint64_t cellar_timer_frequency(void);

/* Sleep until an absolute ns deadline (nanosleep; high precision, low CPU).
 * Returns CELLAR_OK, or CELLAR_ERR_INVALID_ARGUMENT for a past deadline. */
cellar_status_t cellar_timer_sleep_until(uint64_t deadline_ns);

/* Spin (busy-wait) until an absolute ns deadline. Lowest latency, burns CPU;
 * intended for short, latency-critical sections (input sampling, frame sync). */
void cellar_timer_spin_until(uint64_t deadline_ns);

/* ---- Frame-time diagnostics ---------------------------------------------- */

/* Rolling history depth used for 1%-low / 99th percentile computations. */
#define CELLAR_FRAMETIME_HISTORY 240

typedef struct cellar_frametime {
    uint64_t history[CELLAR_FRAMETIME_HISTORY]; /* frame durations, ns */
    size_t   count;
    size_t   pos;
    /* Wait components reported by the app (optional, ns per frame): */
    uint64_t cpu_wait_ns;
    uint64_t gpu_wait_ns;
    uint64_t translate_wait_ns;
} cellar_frametime_t;

void cellar_frametime_init(cellar_frametime_t *ft);

/* Record one completed frame with duration `frame_ns`. */
void cellar_frametime_add(cellar_frametime_t *ft, uint64_t frame_ns);

/* Set the wait components for the next report (ns). */
void cellar_frametime_set_waits(cellar_frametime_t *ft,
                                uint64_t cpu_ns, uint64_t gpu_ns,
                                uint64_t translate_ns);

/* Result of a frame-time report. */
typedef struct cellar_frametime_stats {
    double fps;         /* frames/sec over the window            */
    double frame_ms;    /* average frame duration, ms            */
    double low1_ms;     /* 1% low (99th pct frame duration), ms  */
    double max_ms;      /* maximum frame duration, ms            */
    double cpu_wait_ms;
    double gpu_wait_ms;
    double translate_wait_ms;
} cellar_frametime_stats_t;

/* Compute stats from the recorded history. */
void cellar_frametime_report(const cellar_frametime_t *ft,
                             cellar_frametime_stats_t *out);

#endif /* CELLAR_TIMER_H */
