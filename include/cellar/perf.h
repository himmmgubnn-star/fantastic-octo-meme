/*
 * perf.h — performance tuning, runtime controls, and telemetry.
 *
 * Cellar targets game-like Windows workloads, so it exposes a small perf kit:
 *
 *   - CPU frequency scaling hints (game performance optimization),
 *   - latency/throughput tunables (mmap reads, JIT-style large pages),
 *   - counters + time-series snapshots for profiling,
 *   - a `CELLAR_PERF_*` readout for debugging.
 *
 * Nothing here is invasive by default: scaling hints are advisory, and the
 * sample buffers are bounded ring buffers so `papi=1` never grows unbounded.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_PERF_H
#define CELLAR_PERF_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

/* ---- Counters ------------------------------------------------------------- */

typedef struct cellar_perf_counters {
    uint64_t images_loaded;       /* PE images parsed                      */
    uint64_t imports_resolved;    /* import thunks bound                   */
    uint64_t map_bytes;           /* bytes of sections mapped              */
    uint64_t mmap_reads;          /* zero-copy file maps                   */
    uint64_t audio_bytes;         /* PCM bytes routed to audio backend     */
    uint64_t perf_freq_hints;     /* CPU frequency hints issued            */
    uint64_t prefault_calls;      /* page pre-faults issued                */
} cellar_perf_counters_t;

const cellar_perf_counters_t *cellar_perf_counters(void);

/* Bump counters from the loader/Win32 layer (all cheap increments). */
void cellar_perf_count_images(void);
void cellar_perf_count_imports(uint64_t n);
void cellar_perf_count_map(uint64_t bytes);
void cellar_perf_count_mmap_read(void);
void cellar_perf_count_audio(uint64_t bytes);

/* ---- Tunables ------------------------------------------------------------- */

typedef struct cellar_perf_options {
    int papi;                 /* 1 = populate pages in new mappings        */
    int mmap_threshold;       /* min file size to mmap instead of read     */
    int large_pages;          /* 1 = request large pages where available   */
} cellar_perf_options_t;

/* Read the current options (defaults built-in). */
const cellar_perf_options_t *cellar_perf_options(void);

/* Configure the tunables; returns CELLAR_OK. */
cellar_status_t cellar_perf_set_options(const cellar_perf_options_t *opt);

/* ---- Tracing -------------------------------------------------------------- */

/* A single timestamped sample (e.g. "waveOutWrite: 64"). */
typedef struct cellar_perf_sample {
    const char *label;
    uint64_t    value;
    uint64_t    t_ms;
} cellar_perf_sample_t;

/* Push a labelled timestamped value into the bounded ring buffer. */
void cellar_perf_trace(const char *label, uint64_t value);

/* Copy up to `n` most-recent samples into `out`; returns the count copied. */
size_t cellar_perf_ring_drain(cellar_perf_sample_t *out, size_t n);

/* ---- Helpers -------------------------------------------------------------- */

/* Advise the OS scheduler to prefer high performance for this process. This
 * is advisory only (sched_setaffinity/proc-policy are not touched). Returns
 * CELLAR_OK on success, or an error when unsupported. */
cellar_status_t cellar_perf_hint_high_performance(void);

/* Pre-fault the pages backing `data..data+len` so later touch is fast. */
void cellar_prefault(const void *data, size_t len);

#endif /* CELLAR_PERF_H */
