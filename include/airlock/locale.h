/*
 * locale.h — Windows locale and internationalization.
 *
 * Code pages, Unicode conversion, LCID-style locale, date/time/number
 * formatting, and a keyboard-layout tag. Older applications in particular
 * depend on CP1252 / CP437 rather than UTF-8.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_LOCALE_H
#define AIRLOCK_LOCALE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "airlock.h"

#define AIRLOCK_CP_ACP    0      /* active ANSI code page */
#define AIRLOCK_CP_OEMCP  1
#define AIRLOCK_CP_UTF8   65001
#define AIRLOCK_CP_1252   1252
#define AIRLOCK_CP_437    437

typedef struct airlock_locale {
    uint32_t lcid;          /* 0x0409 = English (US) */
    uint32_t acp;           /* ANSI code page */
    uint32_t oemcp;
    char language[16];
    char country[16];
    char date_fmt[24];
    char time_fmt[24];
    char decimal[4];
    char thousand[4];
    char keyboard[16];
} airlock_locale_t;

void airlock_locale_english_us(airlock_locale_t *l);
void airlock_locale_set(const airlock_locale_t *l);
const airlock_locale_t *airlock_locale_get(void);

airlock_status_t airlock_locale_format_date(const struct tm *t,
                                          char *out, size_t cap);
airlock_status_t airlock_locale_format_time(const struct tm *t,
                                          char *out, size_t cap);
airlock_status_t airlock_locale_format_number(double n, char *out, size_t cap);

/* Convert `n` bytes of `in` in code page `cp` to UTF-8. Returns bytes written
 * (excluding NUL), or -1 on overflow / unknown cp. */
int airlock_cp_to_utf8(uint32_t cp, const uint8_t *in, size_t n,
                      char *out, size_t cap);
int airlock_utf8_to_cp(uint32_t cp, const char *in,
                      uint8_t *out, size_t cap);

#endif /* AIRLOCK_LOCALE_H */
