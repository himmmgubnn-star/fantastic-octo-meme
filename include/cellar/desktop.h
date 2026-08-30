/*
 * desktop.h — Linux desktop integration for Windows applications.
 *
 * .desktop shortcuts, MIME file associations, notifications, an in-process
 * clipboard, and a default-browser hook. The goal is for a Windows program
 * running under Cellar to feel native on a Linux desktop.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_DESKTOP_H
#define CELLAR_DESKTOP_H

#include <stddef.h>

#include "cellar.h"

typedef struct cellar_desktop_entry {
    char name[128];
    char exec[512];
    char icon[256];
    char mime[128];
    char categories[128];
} cellar_desktop_entry_t;

/* Write a freedesktop.org .desktop file into `dir`. */
cellar_status_t cellar_desktop_write_shortcut(const char *dir,
                                              const cellar_desktop_entry_t *e);

/* Append a MIME association (`mime=desktop-id`) to mimeapps.list in `dir`. */
cellar_status_t cellar_desktop_write_mime(const char *dir, const char *mime,
                                          const char *desktop_id);

cellar_status_t cellar_desktop_notify(const char *summary, const char *body);

cellar_status_t cellar_clipboard_set(const char *text);
cellar_status_t cellar_clipboard_get(char *buf, size_t cap);

/* Record the URL a Windows app asked to open (does not spawn a browser). */
cellar_status_t cellar_desktop_open_url(const char *url);
const char *cellar_desktop_last_url(void);

#endif /* CELLAR_DESKTOP_H */
