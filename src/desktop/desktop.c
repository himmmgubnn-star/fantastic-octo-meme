/*
 * desktop.c — Linux desktop integration.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/desktop.h"
#include "cellar/notify.h"

static char g_clipboard[4096];
static char g_last_url[512];

cellar_status_t cellar_desktop_write_shortcut(const char *dir,
                                              const cellar_desktop_entry_t *e)
{
    char path[1024], id[160];
    FILE *f;
    size_t i;
    if (!dir || !e || !e->name[0])
        return CELLAR_ERR_INVALID_ARGUMENT;
    cellar_mkdir_p(dir);
    snprintf(id, sizeof id, "%s", e->name);
    for (i = 0; id[i]; i++)
        if (id[i] == ' ') id[i] = '-';
    snprintf(path, sizeof path, "%s/%s.desktop", dir, id);
    f = fopen(path, "w");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fprintf(f, "[Desktop Entry]\n");
    fprintf(f, "Type=Application\n");
    fprintf(f, "Name=%s\n", e->name);
    fprintf(f, "Exec=%s\n", e->exec[0] ? e->exec : "cellar");
    if (e->icon[0])
        fprintf(f, "Icon=%s\n", e->icon);
    fprintf(f, "Categories=%s\n", e->categories[0] ? e->categories : "Utility;");
    if (e->mime[0])
        fprintf(f, "MimeType=%s\n", e->mime);
    fprintf(f, "StartupNotify=true\n");
    fclose(f);
    return CELLAR_OK;
}

cellar_status_t cellar_desktop_write_mime(const char *dir, const char *mime,
                                          const char *desktop_id)
{
    char path[1024];
    FILE *f;
    if (!dir || !mime || !desktop_id)
        return CELLAR_ERR_INVALID_ARGUMENT;
    cellar_mkdir_p(dir);
    snprintf(path, sizeof path, "%s/mimeapps.list", dir);
    f = fopen(path, "a");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fprintf(f, "%s=%s\n", mime, desktop_id);
    fclose(f);
    return CELLAR_OK;
}

cellar_status_t cellar_desktop_notify(const char *summary, const char *body)
{
    cellar_notification_t n;
    memset(&n, 0, sizeof n);
    snprintf(n.app, sizeof n.app, "cellar");
    snprintf(n.summary, sizeof n.summary, "%s", summary ? summary : "");
    snprintf(n.body, sizeof n.body, "%s", body ? body : "");
    n.urgency = 1;
    cellar_notify_show(&n);
    return CELLAR_OK;
}

cellar_status_t cellar_clipboard_set(const char *text)
{
    snprintf(g_clipboard, sizeof g_clipboard, "%s", text ? text : "");
    return CELLAR_OK;
}

cellar_status_t cellar_clipboard_get(char *buf, size_t cap)
{
    if (!buf || cap == 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    snprintf(buf, cap, "%s", g_clipboard);
    return CELLAR_OK;
}

cellar_status_t cellar_desktop_open_url(const char *url)
{
    if (!url)
        return CELLAR_ERR_INVALID_ARGUMENT;
    snprintf(g_last_url, sizeof g_last_url, "%s", url);
    return CELLAR_OK;
}

const char *cellar_desktop_last_url(void)
{
    return g_last_url;
}
