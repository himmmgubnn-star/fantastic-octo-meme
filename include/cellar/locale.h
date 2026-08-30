/*
 * locale.h — Windows locale and internationalization.
 *
 * Code pages, Unicode conversion, LCID-style locale, date/time/number
 * formatting, and a keyboard-layout tag. Older applications in particular
 * depend on CP1252 / CP437 rather than UTF-8.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_LOCALE_H
#define CELLAR_LOCALE_H

#include <stddef.h>
#include <stdint.h>
#include <time.h>

#include "cellar.h"

#define CELLAR_CP_ACP    0      /* active ANSI code page */
#define CELLAR_CP_OEMCP  1
#define CELLAR_CP_UTF8   65001
#define CELLAR_CP_1252   1252
#define CELLAR_CP_437    437

typedef struct cellar_locale {
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
} cellar_locale_t;

void cellar_locale_english_us(cellar_locale_t *l);
void cellar_locale_set(const cellar_locale_t *l);
const cellar_locale_t *cellar_locale_get(void);

cellar_status_t cellar_locale_format_date(const struct tm *t,
                                          char *out, size_t cap);
cellar_status_t cellar_locale_format_time(const struct tm *t,
                                          char *out, size_t cap);
cellar_status_t cellar_locale_format_number(double n, char *out, size_t cap);

/* Convert `n` bytes of `in` in code page `cp` to UTF-8. Returns bytes written
 * (excluding NUL), or -1 on overflow / unknown cp. */
int cellar_cp_to_utf8(uint32_t cp, const uint8_t *in, size_t n,
                      char *out, size_t cap);
int cellar_utf8_to_cp(uint32_t cp, const char *in,
                      uint8_t *out, size_t cap);

#endif /* CELLAR_LOCALE_H */
