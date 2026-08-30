/*
 * shmem.c — zero-copy SPSC ring buffer.
 *
 * The producer reserves space, writes into the buffer, then releases it by
 * publishing the new tail. The consumer reads from head to the current tail.
 * Because head/tail only ever increase and the buffer is a power-of-two
 * ring, indexing is a mask; no locks and no data-race (single producer,
 * single consumer).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/shmem.h"

static size_t round_up_pow2(size_t n)
{
    size_t v = 1;
    while (v < n)
        v <<= 1;
    return v;
}

airlock_status_t airlock_ring_create(airlock_ring_t *r, size_t capacity)
{
    if (!r || capacity == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(r, 0, sizeof *r);
    r->capacity = round_up_pow2(capacity);
    r->mask = (uint32_t)(r->capacity - 1);
    r->buf = malloc(r->capacity);
    if (!r->buf)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    return AIRLOCK_OK;
}

size_t airlock_ring_capacity(const airlock_ring_t *r)
{
    return r ? r->capacity : 0;
}

size_t airlock_ring_used(const airlock_ring_t *r)
{
    if (!r)
        return 0;
    return (size_t)(r->tail - r->head);
}

size_t airlock_ring_free(const airlock_ring_t *r)
{
    if (!r)
        return 0;
    return r->capacity - (size_t)(r->tail - r->head);
}

size_t airlock_ring_produce(airlock_ring_t *r, const void *data, size_t len)
{
    uint64_t tail, head;
    size_t free_space, first;
    if (!r || !data || len == 0)
        return 0;
    tail = r->tail;
    head = r->head;
    free_space = r->capacity - (size_t)(tail - head);
    if (len > free_space)
        len = free_space;
    if (len == 0)
        return 0;

    first = r->capacity - (size_t)(tail & r->mask);
    if (first > len)
        first = len;
    memcpy(r->buf + (size_t)(tail & r->mask), data, first);
    if (len > first)
        memcpy(r->buf, (const uint8_t *)data + first, len - first);

    /* Publish after the data is visible. */
    r->tail = tail + len;
    return len;
}

size_t airlock_ring_consume(airlock_ring_t *r, void *dst, size_t len)
{
    uint64_t tail, head;
    size_t used, first;
    if (!r || !dst || len == 0)
        return 0;
    tail = r->tail;
    head = r->head;
    used = (size_t)(tail - head);
    if (len > used)
        len = used;
    if (len == 0)
        return 0;

    first = r->capacity - (size_t)(head & r->mask);
    if (first > len)
        first = len;
    memcpy(dst, r->buf + (size_t)(head & r->mask), first);
    if (len > first)
        memcpy((uint8_t *)dst + first, r->buf, len - first);

    r->head = head + len;
    return len;
}

void airlock_ring_drop(airlock_ring_t *r, size_t len)
{
    uint64_t head, tail;
    size_t used;
    if (!r || len == 0)
        return;
    head = r->head;
    tail = r->tail;
    used = (size_t)(tail - head);
    if (len > used)
        len = used;
    r->head = head + len;
}

size_t airlock_ring_peek(const airlock_ring_t *r, void *dst, size_t len)
{
    uint64_t head, tail;
    size_t used, first;
    if (!r || !dst || len == 0)
        return 0;
    head = r->head;
    tail = r->tail;
    used = (size_t)(tail - head);
    if (len > used)
        len = used;
    if (len == 0)
        return 0;
    first = r->capacity - (size_t)(head & r->mask);
    if (first > len)
        first = len;
    memcpy(dst, r->buf + (size_t)(head & r->mask), first);
    if (len > first)
        memcpy((uint8_t *)dst + first, r->buf, len - first);
    return len;
}

void airlock_ring_destroy(airlock_ring_t *r)
{
    if (!r)
        return;
    free(r->buf);
    memset(r, 0, sizeof *r);
}
