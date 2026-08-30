/*
 * perf.c — performance kit implementation.
 *
 * SPDX-License-Identifier: MIT
 */
#include <string.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <unistd.h>

#include "airlock/airlock.h"
#include "airlock/perf.h"
#include "airlock/platform.h"

static airlock_perf_counters_t g_counters;
static airlock_perf_options_t  g_options = {
    .papi           = 0,
    .mmap_threshold = 64 * 1024, /* mmap files >= 64 KiB                    */
    .large_pages    = 0,
};

const airlock_perf_counters_t *airlock_perf_counters(void)
{
    return &g_counters;
}

void airlock_perf_count_images(void)      { g_counters.images_loaded++; }
void airlock_perf_count_imports(uint64_t n){ g_counters.imports_resolved += n; }
void airlock_perf_count_map(uint64_t bytes){ g_counters.map_bytes += bytes; }
void airlock_perf_count_mmap_read(void)   { g_counters.mmap_reads++; }
void airlock_perf_count_audio(uint64_t b) { g_counters.audio_bytes += b; }

const airlock_perf_options_t *airlock_perf_options(void)
{
    return &g_options;
}

airlock_status_t airlock_perf_set_options(const airlock_perf_options_t *opt)
{
    if (!opt)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    g_options = *opt;
    return AIRLOCK_OK;
}

/* ---- Ring buffer ---------------------------------------------------------- */

#define AIRLOCK_PERF_RING_CAP 256

static airlock_perf_sample_t g_ring[AIRLOCK_PERF_RING_CAP];
static size_t g_ring_pos = 0;
static size_t g_ring_count = 0;

void airlock_perf_trace(const char *label, uint64_t value)
{
    airlock_perf_sample_t *s = &g_ring[g_ring_pos];
    s->label = label;
    s->value = value;
    s->t_ms = airlock_monotonic_ms();
    g_ring_pos = (g_ring_pos + 1) % AIRLOCK_PERF_RING_CAP;
    if (g_ring_count < AIRLOCK_PERF_RING_CAP)
        g_ring_count++;
}

size_t airlock_perf_ring_drain(airlock_perf_sample_t *out, size_t n)
{
    size_t copy, i, oldest;
    if (!out || n == 0)
        return 0;
    copy = n < g_ring_count ? n : g_ring_count;
    /* The `count` most-recent entries are contiguous ending at g_ring_pos
     * (with wrap). The oldest of those is at (pos - count + CAP) % CAP. */
    oldest = (g_ring_pos + AIRLOCK_PERF_RING_CAP - g_ring_count) %
             AIRLOCK_PERF_RING_CAP;
    for (i = 0; i < copy; i++)
        out[i] = g_ring[(oldest + i) % AIRLOCK_PERF_RING_CAP];
    return copy;
}

/* ---- Helpers -------------------------------------------------------------- */

airlock_status_t airlock_perf_hint_high_performance(void)
{
    /* Increase the base priority slightly if permitted. This is advisory and
     * non-fatal if unsupported on the current platform. */
#if defined(PRIO_PROCESS)
    if (setpriority(PRIO_PROCESS, 0, -5) == 0) {
        g_counters.perf_freq_hints++;
        return AIRLOCK_OK;
    }
#endif
    return AIRLOCK_ERR_NOT_IMPLEMENTED;
}

void airlock_prefault(const void *data, size_t len)
{
    /* Touching one byte per page forces each page in. */
    const volatile unsigned char *p = (const volatile unsigned char *)data;
    size_t pagesize = (size_t)sysconf(_SC_PAGESIZE);
    if (pagesize == 0)
        pagesize = 4096;
    for (size_t off = 0; off < len; off += pagesize)
        (void)p[off < len ? off : len - 1]; /* volatile read forces page in */
    g_counters.prefault_calls++;
}
