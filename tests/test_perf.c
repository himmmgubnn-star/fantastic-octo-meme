/*
 * test_perf.c — unit tests for the performance kit.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/perf.h"
#include "cellar/platform.h"

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

int main(void)
{
    cellar_perf_counters_t before = *cellar_perf_counters();
    cellar_perf_options_t opt = *cellar_perf_options();
    cellar_perf_sample_t ring[8];
    size_t n;
    static char buf[8192];

    /* Counters increment. */
    cellar_perf_count_images();
    cellar_perf_count_imports(7);
    cellar_perf_count_map(1234);
    CHECK(cellar_perf_counters()->images_loaded == before.images_loaded + 1,
          "images counter increments");
    CHECK(cellar_perf_counters()->imports_resolved == before.imports_resolved + 7,
          "imports counter increments");
    CHECK(cellar_perf_counters()->map_bytes == before.map_bytes + 1234,
          "map bytes counter increments");

    /* Options set/get round-trip. */
    opt.papi = 1;
    opt.large_pages = 1;
    CHECK(cellar_perf_set_options(&opt) == CELLAR_OK, "set options ok");
    CHECK(cellar_perf_options()->papi == 1, "papi option stored");
    CHECK(cellar_perf_set_options(NULL) == CELLAR_ERR_INVALID_ARGUMENT,
          "set options rejects NULL");

    /* Prefault runs without touching/crashing. */
    cellar_prefault(buf, sizeof buf);
    CHECK(cellar_perf_counters()->prefault_calls != 0, "prefault counted");

    /* Ring buffer: push a few, drain the most recent. */
    cellar_perf_trace("alpha", 1);
    cellar_perf_trace("bravo", 2);
    cellar_perf_trace("charlie", 3);
    n = cellar_perf_ring_drain(ring, 8);
    CHECK(n >= 3, "ring drained at least 3");
    CHECK(ring[n - 1].value == 3 && strcmp(ring[n - 1].label, "charlie") == 0,
          "most recent sample is last");

    /* Monotonic clock and perf counter are monotonic and finite. */
    CHECK(cellar_monotonic_ms() > 0, "monotonic clock returns positive");
    CHECK(cellar_perf_frequency() > 0, "perf frequency nonzero");

    if (g_failures == 0) {
        printf("test_perf: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_perf: %d test(s) failed\n", g_failures);
    return 1;
}
