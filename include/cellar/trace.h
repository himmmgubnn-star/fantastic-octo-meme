/*
 * trace.h — dynamic, category-based runtime tracing.
 *
 * Tracing is selectable at runtime so users pay no overhead when it's off:
 * every trace point is guarded by a single branch on an enabled-category
 * bitmask, and the hot-path helpers return immediately when a category is
 * disabled. Enable via the `CELLAR_TRACE` environment variable or the CLI
 * `--trace=graphics,api` option.
 *
 * Categories mirror the compatibility subsystems:
 *   graphics, filesystem, threading, dll, api, audio, timer, compat, all
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_TRACE_H
#define CELLAR_TRACE_H

#include <stdint.h>

typedef enum cellar_trace_category {
    CELLAR_TRACE_GRAPHICS  = 1u << 0,
    CELLAR_TRACE_FILESYSTEM= 1u << 1,
    CELLAR_TRACE_THREADING = 1u << 2,
    CELLAR_TRACE_DLL       = 1u << 3,
    CELLAR_TRACE_API       = 1u << 4,
    CELLAR_TRACE_AUDIO     = 1u << 5,
    CELLAR_TRACE_TIMER     = 1u << 6,
    CELLAR_TRACE_COMPAT    = 1u << 7,
} cellar_trace_category_t;

#define CELLAR_TRACE_ALL \
    (CELLAR_TRACE_GRAPHICS | CELLAR_TRACE_FILESYSTEM | CELLAR_TRACE_THREADING | \
     CELLAR_TRACE_DLL | CELLAR_TRACE_API | CELLAR_TRACE_AUDIO | \
     CELLAR_TRACE_TIMER | CELLAR_TRACE_COMPAT)

/* Global enabled mask. */
uint64_t cellar_trace_enabled_mask(void);

/* True if `cat` is currently traced. */
int cellar_trace_enabled(cellar_trace_category_t cat);

/* Enable/disable categories (OR / AND-NOT). */
void cellar_trace_enable(uint64_t cats);
void cellar_trace_disable(uint64_t cats);

/* Parse a comma-separated list like "graphics,api,timer". Unknown names are
 * ignored. Returns the resulting mask. */
uint64_t cellar_trace_parse(const char *names);

/* Read the CELLAR_TRACE env var and apply it (call once at startup). */
void cellar_trace_init_from_env(void);

/* Emit a trace line (only if the category is enabled). */
void cellar_trace(cellar_trace_category_t cat, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* Convenience macro with an early-out so disabled categories cost a branch. */
#define CELLAR_TRACE(cat, ...) \
    do { if (cellar_trace_enabled(cat)) cellar_trace((cat), __VA_ARGS__); } while (0)

#endif /* CELLAR_TRACE_H */
