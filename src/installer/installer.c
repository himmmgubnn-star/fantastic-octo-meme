/*
 * installer.c — installer side-effect journal.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/installer.h"
#include "airlock/shell.h"

airlock_status_t airlock_install_begin(airlock_package_t *pkg, const char *name,
                                     const char *version, const char *publisher)
{
    if (!pkg || !name || !*name)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(pkg, 0, sizeof *pkg);
    snprintf(pkg->name, sizeof pkg->name, "%s", name);
    snprintf(pkg->version, sizeof pkg->version, "%s", version ? version : "1.0");
    snprintf(pkg->publisher, sizeof pkg->publisher, "%s",
             publisher ? publisher : "");
    return AIRLOCK_OK;
}

airlock_status_t airlock_install_add(airlock_package_t *pkg,
                                   airlock_install_kind_t kind,
                                   const char *key, const char *value)
{
    airlock_install_action_t *a;
    if (!pkg || !key)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (pkg->action_count >= 64)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    a = &pkg->actions[pkg->action_count++];
    a->kind = kind;
    snprintf(a->key, sizeof a->key, "%s", key);
    snprintf(a->value, sizeof a->value, "%s", value ? value : "");
    return AIRLOCK_OK;
}

static const char *kind_name(airlock_install_kind_t k)
{
    switch (k) {
    case AIRLOCK_INST_REGISTRY: return "registry";
    case AIRLOCK_INST_SHORTCUT: return "shortcut";
    case AIRLOCK_INST_ENV:      return "env";
    case AIRLOCK_INST_FILE:     return "file";
    default:                   return "other";
    }
}

static airlock_install_kind_t kind_parse(const char *s)
{
    if (strcmp(s, "registry") == 0) return AIRLOCK_INST_REGISTRY;
    if (strcmp(s, "shortcut") == 0) return AIRLOCK_INST_SHORTCUT;
    if (strcmp(s, "env") == 0)      return AIRLOCK_INST_ENV;
    return AIRLOCK_INST_FILE;
}

airlock_status_t airlock_install_commit(const char *bottle, airlock_package_t *pkg)
{
    char dir[700], path[800];
    FILE *f;
    size_t i;
    if (!bottle || !pkg)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    snprintf(dir, sizeof dir, "%s/uninstall", bottle);
    airlock_mkdir_p(dir);
    airlock_path_join(path, sizeof path, dir, pkg->name);
    {
        size_t L = strlen(path);
        if (L + 4 < sizeof path)
            memcpy(path + L, ".inf", 5);
    }
    f = fopen(path, "w");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    fprintf(f, "name=%s\nversion=%s\npublisher=%s\n",
            pkg->name, pkg->version, pkg->publisher);
    for (i = 0; i < pkg->action_count; i++)
        fprintf(f, "action=%s|%s|%s\n", kind_name(pkg->actions[i].kind),
                pkg->actions[i].key, pkg->actions[i].value);
    fclose(f);

    /* Apply file / shortcut / env actions into the bottle. */
    for (i = 0; i < pkg->action_count; i++) {
        const airlock_install_action_t *a = &pkg->actions[i];
        if (a->kind == AIRLOCK_INST_FILE && a->key[0] == '/') {
            /* absolute host path — skip, stay inside the bottle */
            continue;
        }
        if (a->kind == AIRLOCK_INST_FILE) {
            char dest[1024], dest_dir[1024];
            char *slash;
            FILE *out;
            snprintf(dest, sizeof dest, "%s/%s", bottle, a->key);
            snprintf(dest_dir, sizeof dest_dir, "%s", dest);
            slash = strrchr(dest_dir, '/');
            if (slash) {
                *slash = '\0';
                airlock_mkdir_p(dest_dir);
            }
            out = fopen(dest, "w");
            if (out) {
                fputs(a->value, out);
                fclose(out);
            }
        }
        if (a->kind == AIRLOCK_INST_SHORTCUT) {
            char desk[800];
            snprintf(desk, sizeof desk, "%s/drive_c/users/user/Desktop/%s.lnk",
                     bottle, a->key);
            {
                FILE *lnk = fopen(desk, "w");
                if (lnk) {
                    fprintf(lnk, "shortcut to %s\n", a->value);
                    fclose(lnk);
                }
            }
        }
    }
    pkg->installed = 1;
    return AIRLOCK_OK;
}

airlock_status_t airlock_install_load(const char *bottle, const char *name,
                                    airlock_package_t *out)
{
    char path[800], line[512];
    FILE *f;
    if (!bottle || !name || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    snprintf(path, sizeof path, "%s/uninstall/%s.inf", bottle, name);
    f = fopen(path, "r");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    snprintf(out->name, sizeof out->name, "%s", name);
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        char *eq;
        if (nl) *nl = '\0';
        eq = strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        if (strcmp(line, "version") == 0)
            snprintf(out->version, sizeof out->version, "%s", eq + 1);
        else if (strcmp(line, "publisher") == 0)
            snprintf(out->publisher, sizeof out->publisher, "%s", eq + 1);
        else if (strcmp(line, "action") == 0) {
            char kind[32], key[128], val[256];
            kind[0] = key[0] = val[0] = '\0';
            sscanf(eq + 1, "%31[^|]|%127[^|]|%255[^\n]", kind, key, val);
            airlock_install_add(out, kind_parse(kind), key, val);
        }
    }
    fclose(f);
    out->installed = 1;
    return AIRLOCK_OK;
}

airlock_status_t airlock_install_uninstall(const char *bottle, const char *name)
{
    airlock_package_t pkg;
    char path[800];
    size_t i;
    if (airlock_install_load(bottle, name, &pkg) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    for (i = 0; i < pkg.action_count; i++) {
        const airlock_install_action_t *a = &pkg.actions[i];
        if (a->kind == AIRLOCK_INST_FILE) {
            char dest[1024];
            snprintf(dest, sizeof dest, "%s/%s", bottle, a->key);
            remove(dest);
        }
        if (a->kind == AIRLOCK_INST_SHORTCUT) {
            char desk[800];
            snprintf(desk, sizeof desk, "%s/drive_c/users/user/Desktop/%s.lnk",
                     bottle, a->key);
            remove(desk);
        }
    }
    snprintf(path, sizeof path, "%s/uninstall/%s.inf", bottle, name);
    remove(path);
    return AIRLOCK_OK;
}
