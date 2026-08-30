/*
 * sync.c — lightweight synchronization primitives.
 *
 * All primitives are built on Linux futexes. The uncontended fast path is a
 * single atomic compare-and-swap with no syscall; the slow path drops into
 * futex_wait / futex_wake only when a thread actually contends. On platforms
 * without futex (fallback), primitives degrade to a spin/yield loop so they
 * remain correct (documented trade-off: higher CPU when contended).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#include <errno.h>
#include <stdatomic.h>
#include <sched.h>
#include <string.h>
#include <time.h>

#ifdef __linux__
#include <sys/syscall.h>
#include <unistd.h>
#include <linux/futex.h>
#endif

#include "airlock/airlock.h"
#include "airlock/sync.h"

/* ---- Futex primitives ------------------------------------------------------ */

int airlock_futex_wait(volatile int *uaddr, int val, uint64_t timeout_ms)
{
#ifdef __linux__
    struct timespec ts, *tsp = NULL;
    if (timeout_ms) {
        ts.tv_sec  = (time_t)(timeout_ms / 1000u);
        ts.tv_nsec = (long)((timeout_ms % 1000u) * 1000000u);
        tsp = &ts;
    }
    {
        long r = syscall(SYS_futex, uaddr, FUTEX_WAIT_PRIVATE, val, tsp,
                         NULL, 0);
        if (r == 0)
            return 0;              /* woken by a futex_wake               */
        if (r == -1 && errno == ETIMEDOUT)
            return 1;              /* timeout elapsed                     */
        /* EINTR / EAGAIN: spurious wake or value changed; retry by caller */
        return -1;
    }
#else
    (void)uaddr; (void)val; (void)timeout_ms;
    sched_yield();
    return -1;
#endif
}

void airlock_futex_wake(volatile int *uaddr, int count)
{
#ifdef __linux__
    syscall(SYS_futex, uaddr, FUTEX_WAKE_PRIVATE, count, NULL, NULL, 0);
#else
    (void)uaddr; (void)count;
#endif
}

/* ---- Spinlock -------------------------------------------------------------- */

void airlock_spinlock_init(airlock_spinlock_t *l) { l->state = 0; }

void airlock_spinlock_lock(airlock_spinlock_t *l)
{
    for (;;) {
        if (atomic_exchange((atomic_int *)&l->state, 1) == 0)
            return;
        while (atomic_load((atomic_int *)&l->state) != 0)
            sched_yield();
    }
}

int airlock_spinlock_trylock(airlock_spinlock_t *l)
{
    return atomic_exchange((atomic_int *)&l->state, 1) == 0;
}

void airlock_spinlock_unlock(airlock_spinlock_t *l)
{
    atomic_store((atomic_int *)&l->state, 0);
}

/* ---- Futex mutex ----------------------------------------------------------- */

void airlock_mutex_init(airlock_mutex_t *m) { m->state = 0; }

void airlock_mutex_lock(airlock_mutex_t *m)
{
    int expected = 0;
    /* Fast path: 0 -> 1, uncontended (no syscall). */
    if (atomic_compare_exchange_weak((atomic_int *)&m->state, &expected, 1))
        return;
    /* Slow path: mark contended (2) and sleep until we acquire it. */
    if (expected != 2)
        atomic_exchange((atomic_int *)&m->state, 2);
    for (;;) {
        airlock_futex_wait(&m->state, 2, 0);
        expected = 0;
        if (atomic_compare_exchange_weak((atomic_int *)&m->state, &expected, 2))
            break;
        atomic_exchange((atomic_int *)&m->state, 2);
    }
}

int airlock_mutex_trylock(airlock_mutex_t *m)
{
    return atomic_exchange((atomic_int *)&m->state, 1) == 0;
}

void airlock_mutex_unlock(airlock_mutex_t *m)
{
    if (atomic_exchange((atomic_int *)&m->state, 0) == 2)
        airlock_futex_wake(&m->state, 1);
}

void airlock_mutex_destroy(airlock_mutex_t *m) { (void)m; }

/* ---- Futex event ----------------------------------------------------------- */

void airlock_event_init(airlock_event_t *e) { e->signaled = 0; }

void airlock_event_set(airlock_event_t *e)
{
    atomic_store((atomic_int *)&e->signaled, 1);
    airlock_futex_wake(&e->signaled, 1);
}

void airlock_event_reset(airlock_event_t *e)
{
    atomic_store((atomic_int *)&e->signaled, 0);
}

void airlock_event_wait(airlock_event_t *e)
{
    for (;;) {
        if (atomic_load((atomic_int *)&e->signaled))
            break;
        airlock_futex_wait(&e->signaled, 0, 0);
    }
}

/* ---- Futex semaphore ------------------------------------------------------- */

void airlock_semaphore_init(airlock_semaphore_t *s, int initial)
{
    s->count = initial;
}

void airlock_semaphore_post(airlock_semaphore_t *s)
{
    (void)atomic_fetch_add((atomic_int *)&s->count, 1);
    airlock_futex_wake(&s->count, 1);
}

void airlock_semaphore_wait(airlock_semaphore_t *s)
{
    for (;;) {
        int c = atomic_load((atomic_int *)&s->count);
        if (c > 0 && atomic_compare_exchange_weak((atomic_int *)&s->count,
                                                  &c, c - 1))
            return;
        airlock_futex_wait(&s->count, c, 0);
    }
}
