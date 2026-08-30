/*
 * sync.h — lightweight Windows-synchronization primitives.
 *
 * Heavily multithreaded games hammer the Win32 synchronization APIs
 * (EnterCriticalSection, WaitForSingleObject, CreateEvent, CreateSemaphore,
 * ...). Cellar implements them on top of tiny futex-based primitives that add
 * essentially no overhead in the uncontended case (a single atomic operation
 * and no syscall), and only fall back to a futex_wait syscall when actually
 * contended.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_SYNC_H
#define CELLAR_SYNC_H

#include <stdint.h>
#include <stdbool.h>

/* ---- Lightweight spinlock -------------------------------------------------- */

typedef struct cellar_spinlock {
    volatile int state; /* 0 free, 1 held */
} cellar_spinlock_t;

void cellar_spinlock_init(cellar_spinlock_t *l);
void cellar_spinlock_lock(cellar_spinlock_t *l);
int  cellar_spinlock_trylock(cellar_spinlock_t *l);
void cellar_spinlock_unlock(cellar_spinlock_t *l);

/* ---- Futex-based mutex ----------------------------------------------------- */
/* Uncontended: one atomic CAS, no syscall. Contended: futex_wait. */

typedef struct cellar_mutex {
    volatile int state; /* 0 unlocked, 1 locked (no waiters), 2 contended */
} cellar_mutex_t;

void cellar_mutex_init(cellar_mutex_t *m);
void cellar_mutex_lock(cellar_mutex_t *m);
int  cellar_mutex_trylock(cellar_mutex_t *m);
void cellar_mutex_unlock(cellar_mutex_t *m);
void cellar_mutex_destroy(cellar_mutex_t *m);

/* ---- Futex-based event (auto-reset semantics) ------------------------------ */

typedef struct cellar_event {
    volatile int signaled; /* 1 when set */
} cellar_event_t;

void cellar_event_init(cellar_event_t *e);
void cellar_event_set(cellar_event_t *e);
void cellar_event_reset(cellar_event_t *e);
void cellar_event_wait(cellar_event_t *e);

/* ---- Futex-based counting semaphore ---------------------------------------- */

typedef struct cellar_semaphore {
    volatile int count;
} cellar_semaphore_t;

void cellar_semaphore_init(cellar_semaphore_t *s, int initial);
void cellar_semaphore_post(cellar_semaphore_t *s);   /* release */
void cellar_semaphore_wait(cellar_semaphore_t *s);   /* acquire */

/* ---- Futex primitives (used by the Win32 layer) ---------------------------- */

/* Wait while *uaddr == val (with a timeout of 0 = infinite). Returns 0 on
 * wake, 1 on timeout, -1 on error. */
int cellar_futex_wait(volatile int *uaddr, int val, uint64_t timeout_ms);
void cellar_futex_wake(volatile int *uaddr, int count);

#endif /* CELLAR_SYNC_H */
