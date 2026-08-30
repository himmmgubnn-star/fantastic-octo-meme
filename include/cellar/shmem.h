/*
 * shmem.h — zero-copy shared memory between compatibility-layer components.
 *
 * A single-producer/single-consumer lock-free ring buffer used to move data
 * (audio frames, graphics command streams, input events) between Cellar
 * components without copying through an intermediate compatibility buffer or
 * taking locks. The producer writes, then advances the tail; the consumer
 * reads, then advances the head. Both indices are monotonic counters, so the
 * buffer is safe without locks or atomics on the data region.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_SHMEM_H
#define CELLAR_SHMEM_H

#include <stddef.h>
#include <stdint.h>

typedef struct cellar_ring {
    uint8_t *buf;            /* capacity bytes (owned)         */
    size_t   capacity;       /* must be power of two           */
    uint32_t mask;
    volatile uint64_t head;  /* consumer read index (monotonic)*/
    volatile uint64_t tail;  /* producer write index (monotonic)*/
} cellar_ring_t;

/* Create a ring with `capacity` bytes (rounded up to a power of two). */
cellar_status_t cellar_ring_create(cellar_ring_t *r, size_t capacity);

/* Bytes currently stored (available to consume). */
size_t cellar_ring_used(const cellar_ring_t *r);
/* Bytes of free space (available to produce). */
size_t cellar_ring_free(const cellar_ring_t *r);
/* Total capacity in bytes. */
size_t cellar_ring_capacity(const cellar_ring_t *r);

/* Producer: append `len` bytes from `data`. Returns bytes written (may be
 * less than `len` when the ring is full). */
size_t cellar_ring_produce(cellar_ring_t *r, const void *data, size_t len);

/* Consumer: copy up to `len` bytes into `dst`. Returns bytes copied. */
size_t cellar_ring_consume(cellar_ring_t *r, void *dst, size_t len);

/* Consumer: drop up to `len` bytes without copying. */
void cellar_ring_drop(cellar_ring_t *r, size_t len);

/* Peek at `len` bytes from the head without consuming (copies). Returns the
 * number of bytes available to peek into dst (min(len, used)). */
size_t cellar_ring_peek(const cellar_ring_t *r, void *dst, size_t len);

void cellar_ring_destroy(cellar_ring_t *r);

#endif /* CELLAR_SHMEM_H */
