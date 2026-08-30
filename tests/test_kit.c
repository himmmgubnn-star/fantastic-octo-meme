/*
 * test_kit.c — tests for the high-res timer + frame diagnostics, lightweight
 * synchronization, shared-memory ring, dynamic tracing, and shader cache.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/shadercache.h"
#include "airlock/shmem.h"
#include "airlock/sync.h"
#include "airlock/timer.h"
#include "airlock/trace.h"

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

/* ---- timer + frametime ---------------------------------------------------- */

static void test_timer(void)
{
    uint64_t a, b, t0, t1;
    airlock_frametime_t ft;
    airlock_frametime_stats_t s;
    int i;

    CHECK(airlock_timer_frequency() == 1000000000ull, "ns clock frequency");
    a = airlock_timer_ns();
    b = airlock_timer_ns();
    CHECK(b >= a, "monotonic");

    /* sleep_until ~15 ms in the future. */
    t0 = airlock_timer_ns();
    CHECK(airlock_timer_sleep_until(t0 + 15u * 1000000u) == AIRLOCK_OK,
          "sleep_until ok");
    t1 = airlock_timer_ns();
    CHECK(t1 - t0 >= 12u * 1000000u, "sleep_until actually slept >=12ms");

    /* Frame-time stats. */
    airlock_frametime_init(&ft);
    for (i = 0; i < 97; i++)
        airlock_frametime_add(&ft, 8333333u); /* ~120 fps */
    for (i = 0; i < 3; i++)
        airlock_frametime_add(&ft, 40000000u); /* three 40 ms hitches */
    airlock_frametime_set_waits(&ft, 1200000u, 800000u, 400000u);
    airlock_frametime_report(&ft, &s);
    CHECK(s.fps > 100.0 && s.fps < 130.0, "fps ~120");
    CHECK(s.low1_ms > s.frame_ms, "1% low slower than average (hitch)");
    CHECK(s.max_ms >= 40.0, "max frame >= 40ms");
    CHECK(s.cpu_wait_ms == 1.2 && s.gpu_wait_ms == 0.8 &&
          s.translate_wait_ms == 0.4, "wait components reported");
}

/* ---- synchronization ------------------------------------------------------ */

static void test_sync(void)
{
    airlock_spinlock_t sp;
    airlock_mutex_t m;
    airlock_event_t ev;
    airlock_semaphore_t sem;

    airlock_spinlock_init(&sp);
    CHECK(airlock_spinlock_trylock(&sp), "spinlock trylock");
    CHECK(!airlock_spinlock_trylock(&sp), "spinlock held");
    airlock_spinlock_unlock(&sp);

    airlock_mutex_init(&m);
    CHECK(airlock_mutex_trylock(&m), "mutex trylock");
    CHECK(!airlock_mutex_trylock(&m), "mutex held");
    airlock_mutex_unlock(&m);
    airlock_mutex_lock(&m);
    airlock_mutex_unlock(&m);

    airlock_event_init(&ev);
    airlock_event_set(&ev);
    airlock_event_wait(&ev); /* returns immediately since set */
    airlock_event_reset(&ev);

    airlock_semaphore_init(&sem, 2);
    airlock_semaphore_wait(&sem);
    airlock_semaphore_wait(&sem);
    airlock_semaphore_post(&sem);
    airlock_semaphore_wait(&sem); /* should succeed */

    /* Direct futex: waiting on a value that matches times out (returns 1). */
    {
        volatile int u = 5;
        CHECK(airlock_futex_wait(&u, 5, 1) == 1, "futex wait times out");
    }
}

/* ---- shared memory ring --------------------------------------------------- */

static void test_shmem(void)
{
    airlock_ring_t r;
    char in[128], out[128];
    size_t n;

    memset(in, 'A', 32);
    CHECK(airlock_ring_create(&r, 100) == AIRLOCK_OK, "ring create");
    CHECK(airlock_ring_capacity(&r) >= 100, "ring capacity rounded to pow2");

    n = airlock_ring_produce(&r, in, 32);
    CHECK(n == 32, "produce 32");
    CHECK(airlock_ring_used(&r) == 32, "used 32");

    n = airlock_ring_peek(&r, out, 32);
    CHECK(n == 32 && out[0] == 'A', "peek");
    CHECK(airlock_ring_used(&r) == 32, "peek doesn't consume");

    n = airlock_ring_consume(&r, out, 32);
    CHECK(n == 32 && out[31] == 'A', "consume 32");
    CHECK(airlock_ring_used(&r) == 0, "empty after consume");

    /* Wrap-around: fill most of the ring, then produce again (capacity 128). */
    n = airlock_ring_produce(&r, in, 90);
    CHECK(n == 90, "produce near-full");
    n = airlock_ring_produce(&r, in, 100);
    CHECK(n == 38, "produce capped by free space (128-90)");
    n = airlock_ring_consume(&r, out, 90);
    CHECK(n == 90, "consume wraps correctly");
    n = airlock_ring_consume(&r, out, 38);
    CHECK(n == 38, "consume the rest");

    airlock_ring_destroy(&r);
}

/* ---- tracing -------------------------------------------------------------- */

static void test_trace(void)
{
    uint64_t mask;

    airlock_trace_disable(AIRLOCK_TRACE_ALL);
    mask = airlock_trace_parse("graphics, api, timer");
    CHECK((mask & AIRLOCK_TRACE_GRAPHICS) && (mask & AIRLOCK_TRACE_API) &&
          (mask & AIRLOCK_TRACE_TIMER), "parse names into mask");
    CHECK((mask & AIRLOCK_TRACE_FILESYSTEM) == 0, "unlisted cat stays off");

    airlock_trace_enable(mask);
    CHECK(airlock_trace_enabled(AIRLOCK_TRACE_GRAPHICS), "graphics enabled");
    CHECK(!airlock_trace_enabled(AIRLOCK_TRACE_DLL), "dll still off");
    airlock_trace(AIRLOCK_TRACE_API, "test %d", 42); /* must not crash */
    airlock_trace_disable(AIRLOCK_TRACE_ALL);
    CHECK(!airlock_trace_enabled(AIRLOCK_TRACE_ALL), "disable all");
}

/* ---- shader cache --------------------------------------------------------- */

static void test_shadercache(void)
{
    const char *path = "airlock-test-shadercache.bin";
    airlock_shader_env_t env = {0x10DE2204ull, 0x100ULL, 0x00410000u, 0};
    airlock_shadercache_t c;
    const char blob[] = "compiled-spirv-binary";
    uint64_t sh = airlock_shader_hash("void main(){}", 13);
    uint64_t key = airlock_shader_cache_key(sh, 0xfeed);
    char got[128];
    size_t n;

    /* Insert, close, reopen (same env) -> hit. */
    CHECK(airlock_shadercache_open(&c, path, &env) == AIRLOCK_OK, "cache open");
    CHECK(airlock_shadercache_insert(&c, key, blob, sizeof blob) == AIRLOCK_OK,
          "cache insert");
    CHECK(airlock_shadercache_close(&c) == AIRLOCK_OK, "cache close");

    CHECK(airlock_shadercache_open(&c, path, &env) == AIRLOCK_OK, "cache reopen");
    n = airlock_shadercache_lookup(&c, key, got, sizeof got);
    CHECK(n == sizeof blob && memcmp(got, blob, n) == 0, "cache hit after reopen");
    CHECK(airlock_shadercache_close(&c) == AIRLOCK_OK, "cache close 2");

    /* Different GPU -> invalidation rule: no hit. */
    {
        airlock_shader_env_t env2 = env;
        env2.gpu_id = 0x1002u; /* AMD */
        CHECK(airlock_shadercache_open(&c, path, &env2) == AIRLOCK_OK, "reopen other gpu");
        CHECK(airlock_shadercache_lookup(&c, key, got, sizeof got) == 0,
              "invalidation: different GPU misses");
        CHECK(airlock_shadercache_close(&c) == AIRLOCK_OK, "cache close 3");
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
