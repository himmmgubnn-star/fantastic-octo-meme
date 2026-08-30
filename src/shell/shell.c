/*
 * shell.c — Windows shell environment variables mapped into a prefix.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/shell.h"

static char g_bottle[512];
static char g_paths[AIRLOCK_ENV_COUNT][512];
static int g_inited;

static const char *const k_names[AIRLOCK_ENV_COUNT] = {
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

airlock_status_t airlock_shell_init(const char *bottle)
{
    char drive[640];
    if (!bottle || !*bottle)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    snprintf(g_bottle, sizeof g_bottle, "%s", bottle);
    airlock_path_join(drive, sizeof drive, bottle, "drive_c");

    airlock_path_join(g_paths[AIRLOCK_ENV_WINDIR], sizeof g_paths[0],
                     drive, "windows");
    snprintf(g_paths[AIRLOCK_ENV_SYSTEMROOT], sizeof g_paths[0], "%s",
             g_paths[AIRLOCK_ENV_WINDIR]);
    airlock_path_join(g_paths[AIRLOCK_ENV_PROGRAMFILES], sizeof g_paths[0],
                     drive, "Program Files");
    airlock_path_join(g_paths[AIRLOCK_ENV_PROGRAMDATA], sizeof g_paths[0],
                     drive, "ProgramData");
    airlock_path_join(g_paths[AIRLOCK_ENV_USERPROFILE], sizeof g_paths[0],
                     drive, "users/user");
    airlock_path_join(g_paths[AIRLOCK_ENV_APPDATA], sizeof g_paths[0],
                     g_paths[AIRLOCK_ENV_USERPROFILE], "AppData/Roaming");
    airlock_path_join(g_paths[AIRLOCK_ENV_LOCALAPPDATA], sizeof g_paths[0],
                     g_paths[AIRLOCK_ENV_USERPROFILE], "AppData/Local");
    airlock_path_join(g_paths[AIRLOCK_ENV_TEMP], sizeof g_paths[0],
                     g_paths[AIRLOCK_ENV_LOCALAPPDATA], "Temp");
    snprintf(g_paths[AIRLOCK_ENV_HOMEDRIVE], sizeof g_paths[0], "C:");
    snprintf(g_paths[AIRLOCK_ENV_HOMEPATH], sizeof g_paths[0], "\\users\\user");
    g_inited = 1;
    return AIRLOCK_OK;
}

airlock_status_t airlock_shell_ensure_dirs(void)
{
    airlock_shell_var_t v;
    char sys32[640], fonts[640], desktop[640], dos[640];
    if (!g_inited)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    for (v = 0; v < AIRLOCK_ENV_COUNT; v++) {
        if (v == AIRLOCK_ENV_HOMEDRIVE || v == AIRLOCK_ENV_HOMEPATH)
            continue;
        if (airlock_mkdir_p(g_paths[v]) != 0)
            return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    airlock_path_join(sys32, sizeof sys32, g_paths[AIRLOCK_ENV_WINDIR], "system32");
    airlock_path_join(fonts, sizeof fonts, g_paths[AIRLOCK_ENV_WINDIR], "fonts");
    airlock_path_join(desktop, sizeof desktop, g_paths[AIRLOCK_ENV_USERPROFILE],
                     "Desktop");
    airlock_path_join(dos, sizeof dos, g_bottle, "dosdevices");
    airlock_mkdir_p(sys32);
    airlock_mkdir_p(fonts);
    airlock_mkdir_p(desktop);
    airlock_mkdir_p(dos);
    return AIRLOCK_OK;
}

const char *airlock_shell_get(airlock_shell_var_t v)
{
    if (!g_inited || v >= AIRLOCK_ENV_COUNT)
        return "";
    return g_paths[v];
}

const char *airlock_shell_name(airlock_shell_var_t v)
{
    if (v >= AIRLOCK_ENV_COUNT)
        return "";
    return k_names[v];
}

const char *airlock_shell_bottle(void)
{
    return g_bottle;
}

static const char *lookup_var(const char *name, size_t n)
{
    airlock_shell_var_t v;
    for (v = 0; v < AIRLOCK_ENV_COUNT; v++) {
        if (strlen(k_names[v]) == n && strncasecmp(k_names[v], name, n) == 0)
            return g_paths[v];
    }
    return NULL;
}

airlock_status_t airlock_shell_expand(const char *in, char *out, size_t cap)
{
    size_t o = 0;
    const char *p;
    if (!in || !out || cap == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (!g_inited)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
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
    return AIRLOCK_OK;
}
