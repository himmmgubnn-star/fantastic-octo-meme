/*
 * cellar_util.c — shared utilities: status strings, logging, endian reads,
 * and the export-table string hash.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdio.h>

#include "cellar/cellar.h"

const char *cellar_status_string(cellar_status_t status)
{
    switch (status) {
    case CELLAR_OK:                       return "ok";
    case CELLAR_ERR_INVALID_ARGUMENT:     return "invalid argument";
    case CELLAR_ERR_OUT_OF_MEMORY:        return "out of memory";
    case CELLAR_ERR_NOT_IMPLEMENTED:      return "not implemented";
    case CELLAR_ERR_PE_NOT_PE:            return "not a PE image (bad MZ/PE signature)";
    case CELLAR_ERR_PE_TRUNCATED:         return "PE image truncated";
    case CELLAR_ERR_PE_BAD_MAGIC:         return "unknown PE optional-header magic";
    case CELLAR_ERR_PE_BAD_SECTIONS:      return "malformed PE section table";
    case CELLAR_ERR_PE_BAD_IMPORTS:       return "malformed PE import directory";
    case CELLAR_ERR_PE_BAD_EXPORTS:       return "malformed PE export directory";
    case CELLAR_ERR_PE_BAD_RELOCATIONS:   return "malformed PE base relocation table";
    default:                              return "unknown status";
    }
}

static const char *level_label(int level)
{
    switch (level) {
    case CELLAR_LEVEL_ERROR: return "error";
    case CELLAR_LEVEL_WARN:  return "warn";
    case CELLAR_LEVEL_INFO:  return "info";
    case CELLAR_LEVEL_DEBUG: return "debug";
    case CELLAR_LEVEL_TRACE: return "trace";
    default:                 return "?";
    }
}

void cellar_vlog(int level, const char *module, const char *fmt, ...)
{
    FILE *out = (level <= CELLAR_LEVEL_ERROR) ? stderr : stdout;
    char  buf[1024];
    va_list ap;

    va_start(ap, fmt);
    vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);

    fprintf(out, "[%s] %s: %s\n", level_label(level),
            module ? module : "cellar", buf);
}

/* ---- Little-endian readers (safe against unaligned access) --------------- */

uint16_t cellar_le16(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}

uint32_t cellar_le32(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

uint64_t cellar_le64(const void *p)
{
    const uint8_t *b = (const uint8_t *)p;
    return (uint64_t)cellar_le32(b) | ((uint64_t)cellar_le32(b + 4) << 32);
}

/* ---- djb2 (Bernstein) string hash ---------------------------------------- */

uint32_t cellar_hash_str(const char *s)
{
    uint32_t h = 5381;
    unsigned char c;
    while ((c = (unsigned char)*s++) != 0)
        h = ((h << 5) + h) + c; /* h * 33 + c */
    return h;
}
