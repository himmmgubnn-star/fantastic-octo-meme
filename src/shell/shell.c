/*
 * shell.c — Windows shell environment variables mapped into a prefix.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cellar/cellar.h"
#include "cellar/shell.h"

static char g_bottle[512];
static char g_paths[CELLAR_ENV_COUNT][512];
static int g_inited;

static const char *const k_names[CELLAR_ENV_COUNT] = {
    "APPDATA",
    "LOCALAPPDATA",
    "PROGRAMDATA",
    "TEMP",
    "USERPROFILE",
    "WINDIR",
    "SYSTEMROOT",
    "PROGRAMFILES",
    "HOMEDRIVE",
    "HOMEPATH",
};

cellar_status_t cellar_shell_init(const char *bottle)
{
    char drive[640];
    if (!bottle || !*bottle)
        return CELLAR_ERR_INVALID_ARGUMENT;
    snprintf(g_bottle, sizeof g_bottle, "%s", bottle);
    cellar_path_join(drive, sizeof drive, bottle, "drive_c");

    cellar_path_join(g_paths[CELLAR_ENV_WINDIR], sizeof g_paths[0],
                     drive, "windows");
    snprintf(g_paths[CELLAR_ENV_SYSTEMROOT], sizeof g_paths[0], "%s",
             g_paths[CELLAR_ENV_WINDIR]);
    cellar_path_join(g_paths[CELLAR_ENV_PROGRAMFILES], sizeof g_paths[0],
                     drive, "Program Files");
    cellar_path_join(g_paths[CELLAR_ENV_PROGRAMDATA], sizeof g_paths[0],
                     drive, "ProgramData");
    cellar_path_join(g_paths[CELLAR_ENV_USERPROFILE], sizeof g_paths[0],
                     drive, "users/user");
    cellar_path_join(g_paths[CELLAR_ENV_APPDATA], sizeof g_paths[0],
                     g_paths[CELLAR_ENV_USERPROFILE], "AppData/Roaming");
    cellar_path_join(g_paths[CELLAR_ENV_LOCALAPPDATA], sizeof g_paths[0],
                     g_paths[CELLAR_ENV_USERPROFILE], "AppData/Local");
    cellar_path_join(g_paths[CELLAR_ENV_TEMP], sizeof g_paths[0],
                     g_paths[CELLAR_ENV_LOCALAPPDATA], "Temp");
    snprintf(g_paths[CELLAR_ENV_HOMEDRIVE], sizeof g_paths[0], "C:");
    snprintf(g_paths[CELLAR_ENV_HOMEPATH], sizeof g_paths[0], "\\users\\user");
    g_inited = 1;
    return CELLAR_OK;
}

cellar_status_t cellar_shell_ensure_dirs(void)
{
    cellar_shell_var_t v;
    char sys32[640], fonts[640], desktop[640], dos[640];
    if (!g_inited)
        return CELLAR_ERR_INVALID_ARGUMENT;
    for (v = 0; v < CELLAR_ENV_COUNT; v++) {
        if (v == CELLAR_ENV_HOMEDRIVE || v == CELLAR_ENV_HOMEPATH)
            continue;
        if (cellar_mkdir_p(g_paths[v]) != 0)
            return CELLAR_ERR_INVALID_ARGUMENT;
    }
    cellar_path_join(sys32, sizeof sys32, g_paths[CELLAR_ENV_WINDIR], "system32");
    cellar_path_join(fonts, sizeof fonts, g_paths[CELLAR_ENV_WINDIR], "fonts");
    cellar_path_join(desktop, sizeof desktop, g_paths[CELLAR_ENV_USERPROFILE],
                     "Desktop");
    cellar_path_join(dos, sizeof dos, g_bottle, "dosdevices");
    cellar_mkdir_p(sys32);
    cellar_mkdir_p(fonts);
    cellar_mkdir_p(desktop);
    cellar_mkdir_p(dos);
    return CELLAR_OK;
}

const char *cellar_shell_get(cellar_shell_var_t v)
{
    if (!g_inited || v >= CELLAR_ENV_COUNT)
        return "";
    return g_paths[v];
}

const char *cellar_shell_name(cellar_shell_var_t v)
{
    if (v >= CELLAR_ENV_COUNT)
        return "";
    return k_names[v];
}

const char *cellar_shell_bottle(void)
{
    return g_bottle;
}

static const char *lookup_var(const char *name, size_t n)
{
    cellar_shell_var_t v;
    for (v = 0; v < CELLAR_ENV_COUNT; v++) {
        if (strlen(k_names[v]) == n && strncasecmp(k_names[v], name, n) == 0)
            return g_paths[v];
    }
    return NULL;
}

cellar_status_t cellar_shell_expand(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    const char *p;
    if (!in || !out || cap == 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (!g_inited)
        return CELLAR_ERR_INVALID_ARGUMENT;
    out[0] = '\0';
    for (p = in; *p && o + 1 < cap; ) {
        if (*p == '%') {
            const char *end = strchr(p + 1, '%');
            if (end && end > p + 1) {
                const char *val = lookup_var(p + 1, (size_t)(end - (p + 1)));
                if (val) {
                    size_t vl = strlen(val);
                    if (o + vl >= cap)
                        vl = cap - 1 - o;
                    memcpy(out + o, val, vl);
                    o += vl;
                    p = end + 1;
                    continue;
                }
            }
        }
        out[o++] = *p++;
    }
    out[o] = '\0';
    return CELLAR_OK;
}
