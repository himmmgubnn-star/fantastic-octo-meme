/*
 * sync.h — lightweight Windows-synchronization primitives.
 *
 * Heavily multithreaded games hammer the Win32 synchronization APIs
 * (EnterCriticalSection, WaitForSingleObject, CreateEvent, CreateSemaphore,
 * ...). Airlock implements them on top of tiny futex-based primitives that add
 * essentially no overhead in the uncontended case (a single atomic operation
 * and no syscall), and only fall back to a futex_wait syscall when actually
 * contended.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_SYNC_H
#define AIRLOCK_SYNC_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Lightweight spinlock -------------------------------------------------- */

typedef struct airlock_spinlock {
    volatile int state; /* 0 free, 1 held */
} airlock_spinlock_t;

void airlock_spinlock_init(airlock_spinlock_t *l);
void airlock_spinlock_lock(airlock_spinlock_t *l);
int  airlock_spinlock_trylock(airlock_spinlock_t *l);
void airlock_spinlock_unlock(airlock_spinlock_t *l);

/* ---- Futex-based mutex ----------------------------------------------------- */
/* Uncontended: one atomic CAS, no syscall. Contended: futex_wait. */

typedef struct airlock_mutex {
    volatile int state; /* 0 unlocked, 1 locked (no waiters), 2 contended */
} airlock_mutex_t;

void airlock_mutex_init(airlock_mutex_t *m);
void airlock_mutex_lock(airlock_mutex_t *m);
int  airlock_mutex_trylock(airlock_mutex_t *m);
void airlock_mutex_unlock(airlock_mutex_t *m);
void airlock_mutex_destroy(airlock_mutex_t *m);

/* ---- Futex-based event (auto-reset semantics) ------------------------------ */

typedef struct airlock_event {
    volatile int signaled; /* 1 when set */
} airlock_event_t;

void airlock_event_init(airlock_event_t *e);
void airlock_event_set(airlock_event_t *e);
void airlock_event_reset(airlock_event_t *e);
void airlock_event_wait(airlock_event_t *e);

/* ---- Futex-based counting semaphore ---------------------------------------- */

typedef struct airlock_semaphore {
    volatile int count;
} airlock_semaphore_t;

void airlock_semaphore_init(airlock_semaphore_t *s, int initial);
void airlock_semaphore_post(airlock_semaphore_t *s);   /* release */
void airlock_semaphore_wait(airlock_semaphore_t *s);   /* acquire */

/* ---- Futex primitives (used by the Win32 layer) ---------------------------- */

/* Wait while *uaddr == val (with a timeout of 0 = infinite). Returns 0 on
 * wake, 1 on timeout, -1 on error. */
int airlock_futex_wait(volatile int *uaddr, int val, uint64_t timeout_ms);
void airlock_futex_wake(volatile int *uaddr, int count);

#endif /* AIRLOCK_SYNC_H */
