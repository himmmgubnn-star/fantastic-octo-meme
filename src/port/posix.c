/*
 * posix.c — POSIX platform implementation (Linux and Android/Bionic).
 *
 * Android's Bionic libc and Linux glibc/musl expose the same POSIX calls used
 * here (clock_gettime, nanosleep, mmap, getpid, gettid), so this one file
 * serves every "all Linux environments" target. Only the thread-id call needs
 * a small fallback: older glibc lacks gettid(2), so use syscall(SYS_gettid).
 *
 * SPDX-License-Identifier: MIT
 */
/* Expose nanosleep/clock_gettime/syscall under strict feature-test defaults. */
#define _POSIX_C_SOURCE 200809L
#ifndef _GNU_SOURCE
#define _GNU_SOURCE /* for syscall(SYS_gettid) on Linux */
#endif

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#ifdef __linux__
#include <sys/syscall.h>
#endif

#include "cellar/cellar.h"
#include "cellar/platform.h"

void cellar_sleep_ms(uint32_t ms)
{
    struct timespec ts;
    ts.tv_sec  = (time_t)(ms / 1000u);
    ts.tv_nsec = (long)((ms % 1000u) * 1000000u);
    while (nanosleep(&ts, &ts) == -1 && errno == EINTR)
        ; /* retry on signal interruption */
}

uint64_t cellar_monotonic_ms(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

uint64_t cellar_perf_counter(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
}

uint64_t cellar_perf_frequency(void)
{
    return 1000000000ull; /* perf counter counts nanoseconds */
}

/* Difference between the POSIX epoch (1970) and the FILETIME epoch (1601). */
#define CELLAR_EPOCH_1970_TO_1601 ((uint64_t)11644473600ull)

void cellar_system_time_as_filetime(uint64_t *out_100ns_since_1601)
{
    struct timespec ts;
    uint64_t ns;
    if (clock_gettime(CLOCK_REALTIME, &ts) != 0) {
        *out_100ns_since_1601 = 0;
        return;
    }
    ns = (uint64_t)ts.tv_sec * 1000000000ull + (uint64_t)ts.tv_nsec;
    *out_100ns_since_1601 = (ns / 100ull) + CELLAR_EPOCH_1970_TO_1601 * 10000000ull;
}

uint32_t cellar_getpid(void)
{
    return (uint32_t)getpid();
}

uint32_t cellar_gettid(void)
{
#ifdef __linux__
    return (uint32_t)syscall(SYS_gettid);
#else
    return (uint32_t)(uintptr_t)(unsigned long)getpid();
#endif
}

cellar_status_t cellar_map_file(const char *path, cellar_mapped_file_t *out)
{
    int fd;
    struct stat st;
    void *map;
    size_t len;

    if (!path || !out)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);

    fd = open(path, O_RDONLY);
    if (fd < 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        close(fd);
        return CELLAR_ERR_INVALID_ARGUMENT;
    }
    len = (size_t)st.st_size;
    if (len == 0) {
        /* mmap of a zero-length file fails on Linux; substitute a page. */
        static const char empty[1] = {0};
        out->data = empty;
        out->size = 0;
        out->_region = NULL;
        out->_region_len = 0;
        close(fd);
        return CELLAR_OK;
    }

    map = mmap(NULL, len, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);
    if (map == MAP_FAILED)
        return CELLAR_ERR_OUT_OF_MEMORY;

    out->data = map;
    out->size = len;
    out->_region = map;
    out->_region_len = len;
    return CELLAR_OK;
}

void cellar_unmap_file(cellar_mapped_file_t *mf)
{
    if (!mf)
        return;
    if (mf->_region)
        munmap(mf->_region, mf->_region_len);
    memset(mf, 0, sizeof *mf);
}
