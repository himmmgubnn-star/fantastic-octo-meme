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

#include "cellar/cellar.h"
#include "cellar/trace.h"

static uint64_t g_mask = 0;

uint64_t cellar_trace_enabled_mask(void) { return g_mask; }

int cellar_trace_enabled(cellar_trace_category_t cat)
{
    return (g_mask & (uint64_t)cat) != 0;
}

void cellar_trace_enable(uint64_t cats)  { g_mask |= cats; }
void cellar_trace_disable(uint64_t cats) { g_mask &= ~cats; }

static const struct { const char *name; uint64_t bit; } k_names[] = {
    { "graphics",   CELLAR_TRACE_GRAPHICS },
    { "filesystem", CELLAR_TRACE_FILESYSTEM },
    { "threading",  CELLAR_TRACE_THREADING },
    { "dll",        CELLAR_TRACE_DLL },
    { "api",        CELLAR_TRACE_API },
    { "audio",      CELLAR_TRACE_AUDIO },
    { "timer",      CELLAR_TRACE_TIMER },
    { "compat",     CELLAR_TRACE_COMPAT },
    { "all",        CELLAR_TRACE_ALL },
    { NULL, 0 },
};

static const char *category_name(cellar_trace_category_t cat)
{
    size_t i;
    for (i = 0; k_names[i].name; i++)
        if (k_names[i].bit == (uint64_t)cat)
            return k_names[i].name;
    return "?";
}

uint64_t cellar_trace_parse(const char *names)
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

void cellar_trace_init_from_env(void)
{
    const char *env = getenv("CELLAR_TRACE");
    if (env && *env)
        g_mask |= cellar_trace_parse(env);
}

void cellar_trace(cellar_trace_category_t cat, const char *fmt, ...)
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
