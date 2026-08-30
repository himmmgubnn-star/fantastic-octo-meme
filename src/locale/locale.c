/*
 * locale.c — Windows locale, code pages, and formatting.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/locale.h"

/* Windows-1252 C1 range (0x80..0x9F) → Unicode. */
static const uint16_t k_cp1252_80[32] = {
    0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
    0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
};

static cellar_locale_t g_loc;
static int g_have;

void cellar_locale_english_us(cellar_locale_t *l)
{
    if (!l)
        return;
    memset(l, 0, sizeof *l);
    l->lcid = 0x0409;
    l->acp = CELLAR_CP_1252;
    l->oemcp = CELLAR_CP_437;
    snprintf(l->language, sizeof l->language, "en");
    snprintf(l->country, sizeof l->country, "US");
    snprintf(l->date_fmt, sizeof l->date_fmt, "MM/dd/yyyy");
    snprintf(l->time_fmt, sizeof l->time_fmt, "HH:mm:ss");
    snprintf(l->decimal, sizeof l->decimal, ".");
    snprintf(l->thousand, sizeof l->thousand, ",");
    snprintf(l->keyboard, sizeof l->keyboard, "us");
}

static void ensure(void)
{
    if (!g_have) {
        cellar_locale_english_us(&g_loc);
        g_have = 1;
    }
}

void cellar_locale_set(const cellar_locale_t *l)
{
    if (!l)
        return;
    g_loc = *l;
    g_have = 1;
}

const cellar_locale_t *cellar_locale_get(void)
{
    ensure();
    return &g_loc;
}

cellar_status_t cellar_locale_format_date(const struct tm *t,
                                          char *out, size_t cap)
{
    if (!t || !out || cap == 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    ensure();
    snprintf(out, cap, "%02d/%02d/%04d",
             t->tm_mon + 1, t->tm_mday, t->tm_year + 1900);
    return CELLAR_OK;
}

cellar_status_t cellar_locale_format_time(const struct tm *t,
                                          char *out, size_t cap)
{
    if (!t || !out || cap == 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    snprintf(out, cap, "%02d:%02d:%02d", t->tm_hour, t->tm_min, t->tm_sec);
    return CELLAR_OK;
}

cellar_status_t cellar_locale_format_number(double n, char *out, size_t cap)
{
    char raw[64], grouped[80];
    char *dot, *p, *g;
    int digits, i;
    ensure();
    if (!out || cap == 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    snprintf(raw, sizeof raw, "%.2f", n);
    dot = strchr(raw, '.');
    digits = dot ? (int)(dot - raw) : (int)strlen(raw);
    g = grouped;
    p = raw;
    if (*p == '-') {
        *g++ = *p++;
        digits--;
    }
    for (i = 0; i < digits; i++) {
        *g++ = *p++;
        if ((digits - i - 1) % 3 == 0 && i + 1 < digits)
            *g++ = g_loc.thousand[0] ? g_loc.thousand[0] : ',';
    }
    if (dot) {
        *g++ = g_loc.decimal[0] ? g_loc.decimal[0] : '.';
        p++; /* skip original '.' */
        while (*p)
            *g++ = *p++;
    }
    *g = '\0';
    snprintf(out, cap, "%s", grouped);
    return CELLAR_OK;
}

static size_t utf8_put(char *out, size_t cap, size_t o, uint32_t cp)
{
    if (cp < 0x80) {
        if (o + 1 >= cap) return cap;
        out[o] = (char)cp;
        return o + 1;
    }
    if (cp < 0x800) {
        if (o + 2 >= cap) return cap;
        out[o]     = (char)(0xC0 | (cp >> 6));
        out[o + 1] = (char)(0x80 | (cp & 0x3F));
        return o + 2;
    }
    if (o + 3 >= cap) return cap;
    out[o]     = (char)(0xE0 | (cp >> 12));
    out[o + 1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[o + 2] = (char)(0x80 | (cp & 0x3F));
    return o + 3;
}

static uint32_t map_cp(uint32_t cp, uint8_t b)
{
    if (cp == CELLAR_CP_ACP)
        cp = cellar_locale_get()->acp;
    if (cp == CELLAR_CP_OEMCP)
        cp = cellar_locale_get()->oemcp;
    if (cp == CELLAR_CP_UTF8 || cp == 65001)
        return b;
    if (cp == CELLAR_CP_1252) {
        if (b >= 0x80 && b <= 0x9F)
            return k_cp1252_80[b - 0x80];
        return b;
    }
    if (cp == CELLAR_CP_437)
        return b; /* identity for 0x00-0x7F; high bytes kept as-is for tests */
    return b;
}

int cellar_cp_to_utf8(uint32_t cp, const uint8_t *in, size_t n,
                      char *out, size_t cap)
{
    size_t i, o = 0;
    if (!in || !out || cap == 0)
        return -1;
    if (cp == CELLAR_CP_UTF8 || cp == 65001) {
        size_t cpy = n < cap - 1 ? n : cap - 1;
        memcpy(out, in, cpy);
        out[cpy] = '\0';
        return (int)cpy;
    }
    for (i = 0; i < n; i++) {
        size_t no = utf8_put(out, cap, o, map_cp(cp, in[i]));
        if (no >= cap)
            return -1;
        o = no;
    }
    out[o] = '\0';
    return (int)o;
}

int cellar_utf8_to_cp(uint32_t cp, const char *in, uint8_t *out, size_t cap)
{
    size_t i, o = 0;
    if (!in || !out || cap == 0)
        return -1;
    if (cp == CELLAR_CP_ACP)
        cp = cellar_locale_get()->acp;
    if (cp == CELLAR_CP_UTF8 || cp == 65001) {
        size_t n = strlen(in);
        size_t cpy = n < cap ? n : cap - 1;
        memcpy(out, in, cpy);
        if (cpy < cap)
            out[cpy] = 0;
        return (int)cpy;
    }
    /* Round-trip ASCII and the 1252 C1 table. Multi-byte UTF-8 that isn't in
     * the table becomes '?'. */
    for (i = 0; in[i] && o + 1 < cap; ) {
        unsigned char c = (unsigned char)in[i];
        if (c < 0x80) {
            out[o++] = c;
            i++;
            continue;
        }
        /* 2- or 3-byte UTF-8 → look up 1252. */
        {
            uint32_t u = 0;
            size_t adv = 1;
            if ((c & 0xE0) == 0xC0 && in[i + 1]) {
                u = ((uint32_t)(c & 0x1F) << 6) |
                    ((uint32_t)in[i + 1] & 0x3F);
                adv = 2;
            } else if ((c & 0xF0) == 0xE0 && in[i + 1] && in[i + 2]) {
                u = ((uint32_t)(c & 0x0F) << 12) |
                    ((uint32_t)((unsigned char)in[i + 1] & 0x3F) << 6) |
                    ((uint32_t)((unsigned char)in[i + 2] & 0x3F));
                adv = 3;
            } else {
                out[o++] = (uint8_t)'?';
                i++;
                continue;
            }
            if (cp == CELLAR_CP_1252 && u >= 0x80 && u <= 0xFF) {
                out[o++] = (uint8_t)u;
            } else if (cp == CELLAR_CP_1252) {
                int k, found = 0;
                for (k = 0; k < 32; k++)
                    if (k_cp1252_80[k] == (uint16_t)u) {
                        out[o++] = (uint8_t)(0x80 + k);
                        found = 1;
                        break;
                    }
                if (!found)
                    out[o++] = (uint8_t)'?';
            } else {
                out[o++] = (u < 0x100) ? (uint8_t)u : (uint8_t)'?';
            }
            i += adv;
        }
    }
    if (o < cap)
        out[o] = 0;
    return (int)o;
}
