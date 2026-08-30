/*
 * cellar.h — project-wide common definitions for Cellar.
 *
 * Cellar is a clean-room Windows compatibility layer (a Wine-style runtime)
 * for Linux, written in C11. This header carries the version, status codes,
 * logging, and small portable utilities shared by every module.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_CELLAR_H
#define CELLAR_CELLAR_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

/* Version — aligned with the latest tagged milestone. */
#define CELLAR_VERSION_MAJOR 0
#define CELLAR_VERSION_MINOR 2
#define CELLAR_VERSION_PATCH 0

#define CELLAR_VERSION_STRING "0.2.0"

/* Forward-declared image model (defined in <cellar/loader.h>). */
struct cellar_image;

/*
 * Status codes returned by every Cellar API that can fail. A status of
 * CELLAR_OK means success; anything else is an error. The enumeration is
 * stable (do not reorder) so it can cross the library boundary.
 */
typedef enum cellar_status {
    CELLAR_OK = 0,

    /* Generic / argument errors. */
    CELLAR_ERR_INVALID_ARGUMENT,
    CELLAR_ERR_OUT_OF_MEMORY,
    CELLAR_ERR_NOT_IMPLEMENTED,

    /* PE parsing errors (negative-space from here so sublayers can extend). */
    CELLAR_ERR_PE_NOT_PE,            /* missing MZ / PE signature            */
    CELLAR_ERR_PE_TRUNCATED,         /* buffer shorter than a header claims  */
    CELLAR_ERR_PE_BAD_MAGIC,         /* unknown optional header magic        */
    CELLAR_ERR_PE_BAD_SECTIONS,      /* malformed section table              */
    CELLAR_ERR_PE_BAD_IMPORTS,       /* malformed import directory           */
    CELLAR_ERR_PE_BAD_EXPORTS,       /* malformed export directory           */
    CELLAR_ERR_PE_BAD_RELOCATIONS,   /* malformed base relocation table      */

    CELLAR_ERR_COUNT
} cellar_status_t;

/* Human-readable description of a status code. Never returns NULL. */
const char *cellar_status_string(cellar_status_t status);

/* -------------------------------------------------------------------------
 * Logging
 *
 * Logging is compiled in/out at build time via CELLAR_LOG_LEVEL. Levels are
 * numeric so the threshold can be a preprocessor integer.
 * ----------------------------------------------------------------------- */
#define CELLAR_LEVEL_ERROR 0
#define CELLAR_LEVEL_WARN  1
#define CELLAR_LEVEL_INFO  2
#define CELLAR_LEVEL_DEBUG 3
#define CELLAR_LEVEL_TRACE 4

#ifndef CELLAR_LOG_LEVEL
#define CELLAR_LOG_LEVEL CELLAR_LEVEL_INFO
#endif

void cellar_vlog(int level, const char *module, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 3, 4)))
#endif
    ;

#if CELLAR_LOG_LEVEL >= CELLAR_LEVEL_ERROR
#define CELLAR_LOG_ERROR(...) cellar_vlog(CELLAR_LEVEL_ERROR, "cellar", __VA_ARGS__)
#else
#define CELLAR_LOG_ERROR(...) ((void)0)
#endif

#if CELLAR_LOG_LEVEL >= CELLAR_LEVEL_WARN
#define CELLAR_LOG_WARN(...) cellar_vlog(CELLAR_LEVEL_WARN, "cellar", __VA_ARGS__)
#else
#define CELLAR_LOG_WARN(...) ((void)0)
#endif

#if CELLAR_LOG_LEVEL >= CELLAR_LEVEL_INFO
#define CELLAR_LOG_INFO(...) cellar_vlog(CELLAR_LEVEL_INFO, "cellar", __VA_ARGS__)
#else
#define CELLAR_LOG_INFO(...) ((void)0)
#endif

#if CELLAR_LOG_LEVEL >= CELLAR_LEVEL_DEBUG
#define CELLAR_LOG_DEBUG(...) cellar_vlog(CELLAR_LEVEL_DEBUG, "cellar", __VA_ARGS__)
#else
#define CELLAR_LOG_DEBUG(...) ((void)0)
#endif

#if CELLAR_LOG_LEVEL >= CELLAR_LEVEL_TRACE
#define CELLAR_LOG_TRACE(...) cellar_vlog(CELLAR_LEVEL_TRACE, "cellar", __VA_ARGS__)
#else
#define CELLAR_LOG_TRACE(...) ((void)0)
#endif

/* -------------------------------------------------------------------------
 * Small portable utilities (implemented in src/loader/cellar_util.c).
 * ----------------------------------------------------------------------- */

/* Min/max that never double-evaluate. */
#define CELLAR_MIN(a, b) ((a) < (b) ? (a) : (b))
#define CELLAR_MAX(a, b) ((a) > (b) ? (a) : (b))

/* Read little-endian scalars from an unaligned byte buffer. */
uint16_t cellar_le16(const void *p);
uint32_t cellar_le32(const void *p);
uint64_t cellar_le64(const void *p);

/* Stable string hash used to index export tables. djb2 / Bernstein. */
uint32_t cellar_hash_str(const char *s);

/* Recursively create a directory (POSIX mkdir -p). Returns 0 on success. */
int cellar_mkdir_p(const char *path);

/* Join two path components into `dst` (always NUL-terminated). */
void cellar_path_join(char *dst, size_t n, const char *a, const char *b);

/* Bounded copy; always NUL-terminates. */
void cellar_strlcpy(char *dst, size_t n, const char *src);

/* Bounded append; always NUL-terminates. */
void cellar_strlcat(char *dst, size_t n, const char *src);

#endif /* CELLAR_CELLAR_H */
