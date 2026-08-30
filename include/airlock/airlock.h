/*
 * airlock.h — project-wide common definitions for Airlock.
 *
 * Airlock is a clean-room Windows compatibility layer (a Wine-style runtime)
 * for Linux, written in C11. This header carries the version, status codes,
 * logging, and small portable utilities shared by every module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_AIRLOCK_H
#define AIRLOCK_AIRLOCK_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Version — aligned with the latest tagged milestone. */
#define AIRLOCK_VERSION_MAJOR 0
#define AIRLOCK_VERSION_MINOR 2
#define AIRLOCK_VERSION_PATCH 0

#define AIRLOCK_VERSION_STRING "0.2.0"

/* Forward-declared image model (defined in <airlock/loader.h>). */
struct airlock_image;

/*
 * Status codes returned by every Airlock API that can fail. A status of
 * AIRLOCK_OK means success; anything else is an error. The enumeration is
 * stable (do not reorder) so it can cross the library boundary.
 */
typedef enum airlock_status {
    AIRLOCK_OK = 0,

    /* Generic / argument errors. */
    AIRLOCK_ERR_INVALID_ARGUMENT,
    AIRLOCK_ERR_OUT_OF_MEMORY,
    AIRLOCK_ERR_NOT_IMPLEMENTED,

    /* PE parsing errors (negative-space from here so sublayers can extend). */
    AIRLOCK_ERR_PE_NOT_PE,            /* missing MZ / PE signature            */
    AIRLOCK_ERR_PE_TRUNCATED,         /* buffer shorter than a header claims  */
    AIRLOCK_ERR_PE_BAD_MAGIC,         /* unknown optional header magic        */
    AIRLOCK_ERR_PE_BAD_SECTIONS,      /* malformed section table              */
    AIRLOCK_ERR_PE_BAD_IMPORTS,       /* malformed import directory           */
    AIRLOCK_ERR_PE_BAD_EXPORTS,       /* malformed export directory           */
    AIRLOCK_ERR_PE_BAD_RELOCATIONS,   /* malformed base relocation table      */

    AIRLOCK_ERR_COUNT
} airlock_status_t;

/* Human-readable description of a status code. Never returns NULL. */
const char *airlock_status_string(airlock_status_t status);

/* -------------------------------------------------------------------------
 * Logging
 *
 * Logging is compiled in/out at build time via AIRLOCK_LOG_LEVEL. Levels are
 * numeric so the threshold can be a preprocessor integer.
 * ----------------------------------------------------------------------- */
#define AIRLOCK_LEVEL_ERROR 0
#define AIRLOCK_LEVEL_WARN  1
#define AIRLOCK_LEVEL_INFO  2
#define AIRLOCK_LEVEL_DEBUG 3
#define AIRLOCK_LEVEL_TRACE 4

#ifndef AIRLOCK_LOG_LEVEL
#define AIRLOCK_LOG_LEVEL AIRLOCK_LEVEL_INFO
#endif

void airlock_vlog(int level, const char *module, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#if AIRLOCK_LOG_LEVEL >= AIRLOCK_LEVEL_ERROR
#define AIRLOCK_LOG_ERROR(...) airlock_vlog(AIRLOCK_LEVEL_ERROR, "airlock", __VA_ARGS__)
#else
#define AIRLOCK_LOG_ERROR(...) ((void)0)
#endif

#if AIRLOCK_LOG_LEVEL >= AIRLOCK_LEVEL_WARN
#define AIRLOCK_LOG_WARN(...) airlock_vlog(AIRLOCK_LEVEL_WARN, "airlock", __VA_ARGS__)
#else
#define AIRLOCK_LOG_WARN(...) ((void)0)
#endif

#if AIRLOCK_LOG_LEVEL >= AIRLOCK_LEVEL_INFO
#define AIRLOCK_LOG_INFO(...) airlock_vlog(AIRLOCK_LEVEL_INFO, "airlock", __VA_ARGS__)
#else
#define AIRLOCK_LOG_INFO(...) ((void)0)
#endif

#if AIRLOCK_LOG_LEVEL >= AIRLOCK_LEVEL_DEBUG
#define AIRLOCK_LOG_DEBUG(...) airlock_vlog(AIRLOCK_LEVEL_DEBUG, "airlock", __VA_ARGS__)
#else
#define AIRLOCK_LOG_DEBUG(...) ((void)0)
#endif

#if AIRLOCK_LOG_LEVEL >= AIRLOCK_LEVEL_TRACE
#define AIRLOCK_LOG_TRACE(...) airlock_vlog(AIRLOCK_LEVEL_TRACE, "airlock", __VA_ARGS__)
#else
#define AIRLOCK_LOG_TRACE(...) ((void)0)
#endif

/* -------------------------------------------------------------------------
 * Small portable utilities (implemented in src/loader/airlock_util.c).
 * ----------------------------------------------------------------------- */

/* Min/max that never double-evaluate. */
#define AIRLOCK_MIN(a, b) ((a) < (b) ? (a) : (b))
#define AIRLOCK_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Read little-endian scalars from an unaligned byte buffer. */
uint16_t airlock_le16(const void *p);
uint32_t airlock_le32(const void *p);
uint64_t airlock_le64(const void *p);

/* Stable string hash used to index export tables. djb2 / Bernstein. */
uint32_t airlock_hash_str(const char *s);

/* Recursively create a directory (POSIX mkdir -p). Returns 0 on success. */
int airlock_mkdir_p(const char *path);

/* Join two path components into `dst` (always NUL-terminated). */
void airlock_path_join(char *dst, size_t n, const char *a, const char *b);

/* Bounded copy; always NUL-terminates. */
void airlock_strlcpy(char *dst, size_t n, const char *src);

/* Bounded append; always NUL-terminates. */
void airlock_strlcat(char *dst, size_t n, const char *src);

#endif /* AIRLOCK_AIRLOCK_H */
