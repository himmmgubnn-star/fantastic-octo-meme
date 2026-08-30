/*
 * trace.c — tracing implementation.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/trace.h"

static uint64_t g_mask = 0;

uint64_t airlock_trace_enabled_mask(void) { return g_mask; }

int airlock_trace_enabled(airlock_trace_category_t cat)
{
    return (g_mask & (uint64_t)cat) != 0;
}

void airlock_trace_enable(uint64_t cats)  { g_mask |= cats; }
void airlock_trace_disable(uint64_t cats) { g_mask &= ~cats; }

static const struct { const char *name; uint64_t bit; } k_names[] = {
    { "graphics",   AIRLOCK_TRACE_GRAPHICS },
    { "filesystem", AIRLOCK_TRACE_FILESYSTEM },
    { "threading",  AIRLOCK_TRACE_THREADING },
    { "dll",        AIRLOCK_TRACE_DLL },
    { "api",        AIRLOCK_TRACE_API },
    { "audio",      AIRLOCK_TRACE_AUDIO },
    { "timer",      AIRLOCK_TRACE_TIMER },
    { "compat",     AIRLOCK_TRACE_COMPAT },
    { "all",        AIRLOCK_TRACE_ALL },
    { NULL, 0 },
};

static const char *category_name(airlock_trace_category_t cat)
{
    size_t i;
    for (i = 0; k_names[i].name; i++)
        if (k_names[i].bit == (uint64_t)cat)
            return k_names[i].name;
    return "?";
}

uint64_t airlock_trace_parse(const char *names)
{
    uint64_t mask = 0;
    char buf[512];
    char *p, *save = NULL;
    if (!names)
        return 0;
    snprintf(buf, sizeof buf, "%s", names);
    for (p = strtok_r(buf, ",", &save); p;
         p = strtok_r(NULL, ",", &save)) {
        size_t i;
        /* trim whitespace */
        while (*p == ' ' || *p == '\t') p++;
        for (i = 0; k_names[i].name; i++)
            if (strcasecmp(p, k_names[i].name) == 0)
                mask |= k_names[i].bit;
    }
    return mask;
}

void airlock_trace_init_from_env(void)
{
    const char *env = getenv("AIRLOCK_TRACE");
    if (env && *env)
        g_mask |= airlock_trace_parse(env);
}

void airlock_trace(airlock_trace_category_t cat, const char *fmt, ...)
{
    char line[512];
    va_list ap;
    if (!(g_mask & (uint64_t)cat))
        return;
    va_start(ap, fmt);
    vsnprintf(line, sizeof line, fmt, ap);
    va_end(ap);
    fprintf(stderr, "[trace:%s] %s\n", category_name(cat), line);
}
