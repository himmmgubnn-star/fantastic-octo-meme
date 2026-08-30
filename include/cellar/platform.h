/*
 * platform.h — portable OS abstractions used by the Win32 layer.
 *
 * Cellar targets "all Linux environments", which includes Android. Android
 * ships the Bionic libc, which exposes the same POSIX surface Linux does, so
 * a single implementation (src/port/posix.c) serves both. This header is the
 * seam: anything OS-specific goes through here, and porting to a new OS means
 * providing one more implementation file — nothing else changes.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_PLATFORM_H
#define CELLAR_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

/* ---- Clock & sleep ------------------------------------------------------- */

/* Sleep for `ms` milliseconds (never busy-waits; nanosleep on POSIX). */
void cellar_sleep_ms(uint32_t ms);

/* Milliseconds on a monotonic clock (unaffected by wall-clock changes). */
uint64_t cellar_monotonic_ms(void);

/* High-resolution performance counter for QueryPerformanceCounter. The
 * absolute value is meaningless; ratios of two readings have units of seconds
 * when divided by cellar_perf_frequency(). */
uint64_t cellar_perf_counter(void);

/* Nominal frequency of cellar_perf_counter() in counts per second. */
uint64_t cellar_perf_frequency(void);

/* Current time as a 100ns-interval FILETIME since 1601-01-01 UTC, and the
 * local-time breakdown in FILETIME's (year/month/day/...) fields. */
void cellar_system_time_as_filetime(uint64_t *out_100ns_since_1601);

/* ---- Process / thread identity ------------------------------------------- */

/* OS process ID (cellar_getpid) and OS thread ID (gettid on Linux/Bionic). */
uint32_t cellar_getpid(void);
uint32_t cellar_gettid(void);

/* ---- File mapping (zero-copy reads) --------------------------------------- */

/* A read-only memory mapping of a file. `data`/`size` are valid until
 * cellar_unmap_file(). */
typedef struct cellar_mapped_file {
    const void *data;
    size_t      size;
    void       *_region;   /* implementation detail */
    size_t      _region_len;
} cellar_mapped_file_t;

cellar_status_t cellar_map_file(const char *path, cellar_mapped_file_t *out);
void cellar_unmap_file(cellar_mapped_file_t *mf);

#endif /* CELLAR_PLATFORM_H */
