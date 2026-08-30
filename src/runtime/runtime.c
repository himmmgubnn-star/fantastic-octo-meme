/*
 * runtime.c — Windows runtime dependency manager.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cellar/cellar.h"
#include "cellar/runtime.h"

typedef struct rt_meta {
    cellar_runtime_kind_t kind;
    const char *name;
    const char *version;
    const char *dir;
} rt_meta_t;

static const rt_meta_t k_meta[CELLAR_RT_COUNT] = {
    { CELLAR_RT_VCRUNTIME,  "Visual C++ Runtime",    "14.40", "vcruntime" },
    { CELLAR_RT_DOTNET,     ".NET Runtime",          "8.0",   "dotnet" },
    { CELLAR_RT_DIRECTX,    "DirectX components",    "9.0c",  "directx" },
    { CELLAR_RT_FONTS,      "Windows fonts",         "1.0",   "fonts" },
    { CELLAR_RT_SYSTEMLIBS, "Common system libraries","1.0",  "systemlibs" },
};

static char g_bottle[512];
static int g_inited;

cellar_status_t cellar_runtime_init(const char *bottle)
{
    if (!bottle || !*bottle)
        return CELLAR_ERR_INVALID_ARGUMENT;
    snprintf(g_bottle, sizeof g_bottle, "%s", bottle);
    g_inited = 1;
    return CELLAR_OK;
}

cellar_runtime_kind_t cellar_runtime_parse(const char *name)
{
    if (!name) return CELLAR_RT_COUNT;
    if (strcasecmp(name, "vcruntime") == 0 || strcasecmp(name, "vc") == 0 ||
        strcasecmp(name, "vcredist") == 0)
        return CELLAR_RT_VCRUNTIME;
    if (strcasecmp(name, "dotnet") == 0 || strcasecmp(name, "net") == 0)
        return CELLAR_RT_DOTNET;
    if (strcasecmp(name, "directx") == 0 || strcasecmp(name, "dx") == 0)
        return CELLAR_RT_DIRECTX;
    if (strcasecmp(name, "fonts") == 0)
        return CELLAR_RT_FONTS;
    if (strcasecmp(name, "systemlibs") == 0 || strcasecmp(name, "system") == 0)
        return CELLAR_RT_SYSTEMLIBS;
    return CELLAR_RT_COUNT;
}

const char *cellar_runtime_kind_name(cellar_runtime_kind_t k)
{
    if (k >= CELLAR_RT_COUNT) return "?";
    return k_meta[k].name;
}

static void marker_path(cellar_runtime_kind_t k, char *dst, size_t n)
{
    char dir[700];
    snprintf(dir, sizeof dir, "%s/runtime/%s", g_bottle, k_meta[k].dir);
    cellar_path_join(dst, n, dir, "INSTALLED");
}

int cellar_runtime_is_installed(cellar_runtime_kind_t kind)
{
    char path[800];
    FILE *f;
    if (!g_inited || kind >= CELLAR_RT_COUNT)
        return 0;
    marker_path(kind, path, sizeof path);
    f = fopen(path, "r");
    if (!f)
        return 0;
    fclose(f);
    return 1;
}

cellar_status_t cellar_runtime_install(cellar_runtime_kind_t kind)
{
    char dir[700], path[800];
    FILE *f;
    if (!g_inited || kind >= CELLAR_RT_COUNT)
        return CELLAR_ERR_INVALID_ARGUMENT;
    snprintf(dir, sizeof dir, "%s/runtime/%s", g_bottle, k_meta[kind].dir);
    if (cellar_mkdir_p(dir) != 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    marker_path(kind, path, sizeof path);
    f = fopen(path, "w");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fprintf(f, "name=%s\nversion=%s\n", k_meta[kind].name, k_meta[kind].version);
    fclose(f);
    /* Layout extras so installation is more than a flag. */
    if (kind == CELLAR_RT_FONTS) {
        char fonts[800];
        snprintf(fonts, sizeof fonts, "%s/drive_c/windows/fonts", g_bottle);
        cellar_mkdir_p(fonts);
    }
    if (kind == CELLAR_RT_VCRUNTIME) {
        char s32[800];
        snprintf(s32, sizeof s32, "%s/drive_c/windows/system32", g_bottle);
        cellar_mkdir_p(s32);
    }
    return CELLAR_OK;
}

cellar_status_t cellar_runtime_uninstall(cellar_runtime_kind_t kind)
{
    char path[800];
    if (!g_inited || kind >= CELLAR_RT_COUNT)
        return CELLAR_ERR_INVALID_ARGUMENT;
    marker_path(kind, path, sizeof path);
    remove(path);
    return CELLAR_OK;
}

size_t cellar_runtime_list(cellar_runtime_t *out, size_t cap)
{
    size_t i, n = 0;
    if (!out)
        return 0;
    for (i = 0; i < CELLAR_RT_COUNT && n < cap; i++) {
        out[n].kind = (cellar_runtime_kind_t)i;
        out[n].name = k_meta[i].name;
        out[n].version = k_meta[i].version;
        out[n].installed = cellar_runtime_is_installed((cellar_runtime_kind_t)i);
        {
            char rel[64];
            snprintf(rel, sizeof rel, "runtime/%s", k_meta[i].dir);
            cellar_path_join(out[n].install_path, sizeof out[n].install_path,
                             g_bottle[0] ? g_bottle : ".", rel);
        }
        n++;
    }
    return n;
}

void cellar_runtime_report(void)
{
    cellar_runtime_t list[CELLAR_RT_COUNT];
    size_t n, i;
    n = cellar_runtime_list(list, CELLAR_RT_COUNT);
    printf("Runtime Manager\n");
    printf("---------------\n");
    for (i = 0; i < n; i++)
        printf("%s %s  (%s)\n",
               list[i].installed ? "✓" : "·",
               list[i].name, list[i].version);
}
