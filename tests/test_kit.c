/*
 * test_kit.c — tests for the high-res timer + frame diagnostics, lightweight
 * synchronization, shared-memory ring, dynamic tracing, and shader cache.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/shadercache.h"
#include "cellar/shmem.h"
#include "cellar/sync.h"
#include "cellar/timer.h"
#include "cellar/trace.h"

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

/* ---- timer + frametime ---------------------------------------------------- */

static void test_timer(void)
{
    uint64_t a, b, t0, t1;
    cellar_frametime_t ft;
    cellar_frametime_stats_t s;
    int i;

    CHECK(cellar_timer_frequency() == 1000000000ull, "ns clock frequency");
    a = cellar_timer_ns();
    b = cellar_timer_ns();
    CHECK(b >= a, "monotonic");

    /* sleep_until ~15 ms in the future. */
    t0 = cellar_timer_ns();
    CHECK(cellar_timer_sleep_until(t0 + 15u * 1000000u) == CELLAR_OK,
          "sleep_until ok");
    t1 = cellar_timer_ns();
    CHECK(t1 - t0 >= 12u * 1000000u, "sleep_until actually slept >=12ms");

    /* Frame-time stats. */
    cellar_frametime_init(&ft);
    for (i = 0; i < 97; i++)
        cellar_frametime_add(&ft, 8333333u); /* ~120 fps */
    for (i = 0; i < 3; i++)
        cellar_frametime_add(&ft, 40000000u); /* three 40 ms hitches */
    cellar_frametime_set_waits(&ft, 1200000u, 800000u, 400000u);
    cellar_frametime_report(&ft, &s);
    CHECK(s.fps > 100.0 && s.fps < 130.0, "fps ~120");
    CHECK(s.low1_ms > s.frame_ms, "1% low slower than average (hitch)");
    CHECK(s.max_ms >= 40.0, "max frame >= 40ms");
    CHECK(s.cpu_wait_ms == 1.2 && s.gpu_wait_ms == 0.8 &&
          s.translate_wait_ms == 0.4, "wait components reported");
}

/* ---- synchronization ------------------------------------------------------ */

static void test_sync(void)
{
    cellar_spinlock_t sp;
    cellar_mutex_t m;
    cellar_event_t ev;
    cellar_semaphore_t sem;

    cellar_spinlock_init(&sp);
    CHECK(cellar_spinlock_trylock(&sp), "spinlock trylock");
    CHECK(!cellar_spinlock_trylock(&sp), "spinlock held");
    cellar_spinlock_unlock(&sp);

    cellar_mutex_init(&m);
    CHECK(cellar_mutex_trylock(&m), "mutex trylock");
    CHECK(!cellar_mutex_trylock(&m), "mutex held");
    cellar_mutex_unlock(&m);
    cellar_mutex_lock(&m);
    cellar_mutex_unlock(&m);

    cellar_event_init(&ev);
    cellar_event_set(&ev);
    cellar_event_wait(&ev); /* returns immediately since set */
    cellar_event_reset(&ev);

    cellar_semaphore_init(&sem, 2);
    cellar_semaphore_wait(&sem);
    cellar_semaphore_wait(&sem);
    cellar_semaphore_post(&sem);
    cellar_semaphore_wait(&sem); /* should succeed */

    /* Direct futex: waiting on a value that matches times out (returns 1). */
    {
        volatile int u = 5;
        CHECK(cellar_futex_wait(&u, 5, 1) == 1, "futex wait times out");
    }
}

/* ---- shared memory ring --------------------------------------------------- */

static void test_shmem(void)
{
    cellar_ring_t r;
    char in[128], out[128];
    size_t n;

    memset(in, 'A', 32);
    CHECK(cellar_ring_create(&r, 100) == CELLAR_OK, "ring create");
    CHECK(cellar_ring_capacity(&r) >= 100, "ring capacity rounded to pow2");

    n = cellar_ring_produce(&r, in, 32);
    CHECK(n == 32, "produce 32");
    CHECK(cellar_ring_used(&r) == 32, "used 32");

    n = cellar_ring_peek(&r, out, 32);
    CHECK(n == 32 && out[0] == 'A', "peek");
    CHECK(cellar_ring_used(&r) == 32, "peek doesn't consume");

    n = cellar_ring_consume(&r, out, 32);
    CHECK(n == 32 && out[31] == 'A', "consume 32");
    CHECK(cellar_ring_used(&r) == 0, "empty after consume");

    /* Wrap-around: fill most of the ring, then produce again (capacity 128). */
    n = cellar_ring_produce(&r, in, 90);
    CHECK(n == 90, "produce near-full");
    n = cellar_ring_produce(&r, in, 100);
    CHECK(n == 38, "produce capped by free space (128-90)");
    n = cellar_ring_consume(&r, out, 90);
    CHECK(n == 90, "consume wraps correctly");
    n = cellar_ring_consume(&r, out, 38);
    CHECK(n == 38, "consume the rest");

    cellar_ring_destroy(&r);
}

/* ---- tracing -------------------------------------------------------------- */

static void test_trace(void)
{
    uint64_t mask;

    cellar_trace_disable(CELLAR_TRACE_ALL);
    mask = cellar_trace_parse("graphics, api, timer");
    CHECK((mask & CELLAR_TRACE_GRAPHICS) && (mask & CELLAR_TRACE_API) &&
          (mask & CELLAR_TRACE_TIMER), "parse names into mask");
    CHECK((mask & CELLAR_TRACE_FILESYSTEM) == 0, "unlisted cat stays off");

    cellar_trace_enable(mask);
    CHECK(cellar_trace_enabled(CELLAR_TRACE_GRAPHICS), "graphics enabled");
    CHECK(!cellar_trace_enabled(CELLAR_TRACE_DLL), "dll still off");
    cellar_trace(CELLAR_TRACE_API, "test %d", 42); /* must not crash */
    cellar_trace_disable(CELLAR_TRACE_ALL);
    CHECK(!cellar_trace_enabled(CELLAR_TRACE_ALL), "disable all");
}

/* ---- shader cache --------------------------------------------------------- */

static void test_shadercache(void)
{
    const char *path = "cellar-test-shadercache.bin";
    cellar_shader_env_t env = {0x10DE2204ull, 0x100ULL, 0x00410000u, 0};
    cellar_shadercache_t c;
    const char blob[] = "compiled-spirv-binary";
    uint64_t sh = cellar_shader_hash("void main(){}", 13);
    uint64_t key = cellar_shader_cache_key(sh, 0xfeed);
    char got[128];
    size_t n;

    /* Insert, close, reopen (same env) -> hit. */
    CHECK(cellar_shadercache_open(&c, path, &env) == CELLAR_OK, "cache open");
    CHECK(cellar_shadercache_insert(&c, key, blob, sizeof blob) == CELLAR_OK,
          "cache insert");
    CHECK(cellar_shadercache_close(&c) == CELLAR_OK, "cache close");

    CHECK(cellar_shadercache_open(&c, path, &env) == CELLAR_OK, "cache reopen");
    n = cellar_shadercache_lookup(&c, key, got, sizeof got);
    CHECK(n == sizeof blob && memcmp(got, blob, n) == 0, "cache hit after reopen");
    CHECK(cellar_shadercache_close(&c) == CELLAR_OK, "cache close 2");

    /* Different GPU -> invalidation rule: no hit. */
    {
        cellar_shader_env_t env2 = env;
        env2.gpu_id = 0x1002u; /* AMD */
        CHECK(cellar_shadercache_open(&c, path, &env2) == CELLAR_OK, "reopen other gpu");
        CHECK(cellar_shadercache_lookup(&c, key, got, sizeof got) == 0,
              "invalidation: different GPU misses");
        CHECK(cellar_shadercache_close(&c) == CELLAR_OK, "cache close 3");
    }

    remove(path);
}

int main(void)
{
    test_timer();
    test_sync();
    test_shmem();
    test_trace();
    test_shadercache();

    if (g_failures == 0) {
        printf("test_kit: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_kit: %d test(s) failed\n", g_failures);
    return 1;
}
