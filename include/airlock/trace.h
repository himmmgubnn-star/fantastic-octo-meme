/*
 * trace.h — dynamic, category-based runtime tracing.
 *
 * Tracing is selectable at runtime so users pay no overhead when it's off:
 * every trace point is guarded by a single branch on an enabled-category
 * bitmask, and the hot-path helpers return immediately when a category is
 * disabled. Enable via the `AIRLOCK_TRACE` environment variable or the CLI
 * `--trace=graphics,api` option.
 *
 * Categories mirror the compatibility subsystems:
 *   graphics, filesystem, threading, dll, api, audio, timer, compat, all
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_TRACE_H
#define AIRLOCK_TRACE_H

#include <stdint.h>

typedef enum airlock_trace_category {
    AIRLOCK_TRACE_GRAPHICS  = 1u << 0,
    AIRLOCK_TRACE_FILESYSTEM= 1u << 1,
    AIRLOCK_TRACE_THREADING = 1u << 2,
    AIRLOCK_TRACE_DLL       = 1u << 3,
    AIRLOCK_TRACE_API       = 1u << 4,
    AIRLOCK_TRACE_AUDIO     = 1u << 5,
    AIRLOCK_TRACE_TIMER     = 1u << 6,
    AIRLOCK_TRACE_COMPAT    = 1u << 7,
} airlock_trace_category_t;

#define AIRLOCK_TRACE_ALL \
    (AIRLOCK_TRACE_GRAPHICS | AIRLOCK_TRACE_FILESYSTEM | AIRLOCK_TRACE_THREADING | \
     AIRLOCK_TRACE_DLL | AIRLOCK_TRACE_API | AIRLOCK_TRACE_AUDIO | \
     AIRLOCK_TRACE_TIMER | AIRLOCK_TRACE_COMPAT)

/* Global enabled mask. */
uint64_t airlock_trace_enabled_mask(void);

/* True if `cat` is currently traced. */
int airlock_trace_enabled(airlock_trace_category_t cat);

/* Enable/disable categories (OR / AND-NOT). */
void airlock_trace_enable(uint64_t cats);
void airlock_trace_disable(uint64_t cats);

/* Parse a comma-separated list like "graphics,api,timer". Unknown names are
 * ignored. Returns the resulting mask. */
uint64_t airlock_trace_parse(const char *names);

/* Read the AIRLOCK_TRACE env var and apply it (call once at startup). */
void airlock_trace_init_from_env(void);

/* Emit a trace line (only if the category is enabled). */
void airlock_trace(airlock_trace_category_t cat, const char *fmt, ...)
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 2, 3)))
#endif
    ;

/* Convenience macro with an early-out so disabled categories cost a branch. */
#define AIRLOCK_TRACE(cat, ...) \
    do { if (airlock_trace_enabled(cat)) airlock_trace((cat), __VA_ARGS__); } while (0)

#endif /* AIRLOCK_TRACE_H */
