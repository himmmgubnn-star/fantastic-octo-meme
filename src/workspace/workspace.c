/*
 * workspace.c — Airlock-style isolated application workspaces.
 *
 * This is the product layer: one workspace per app, guided setup, an app
 * library, versioned profiles, snapshots/rollback, launch doctor, support
 * bundles, permissions, controls, performance modes, and shader cache
 * management. It builds on the lower-level prefix/profile/inspector modules.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L

#include <ctype.h>
#include <dirent.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <sys/statvfs.h>
#include <sys/types.h>
#include <sys/utsname.h>
#include <time.h>
#include <unistd.h>

#include "airlock/airlock.h"
#include "airlock/compat.h"
#include "airlock/db.h"
#include "airlock/desktop.h"
#include "airlock/device.h"
#include "airlock/inspect.h"
#include "airlock/loader.h"
#include "airlock/plugin.h"
#include "airlock/prefix.h"
#include "airlock/runtime.h"
#include "airlock/workspace.h"

/* ---- helpers -------------------------------------------------------------- */

static int valid_name(const char *name)
{
    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    return 1;
}

static void ws_dir(char *dst, size_t n, const char *root, const char *name)
{
    airlock_path_join(dst, n, root ? root : ".", name ? name : "");
}

static int file_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISREG(st.st_mode);
}

static int dir_exists(const char *path)
{
    struct stat st;
    return path && stat(path, &st) == 0 && S_ISDIR(st.st_mode);
}

static int read_text(const char *path, char *buf, size_t cap)
{
    FILE *f;
    size_t n;
    if (!path || !buf || cap == 0)
        return 0;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return 1;
}

static int write_text(const char *path, const char *text)
{
    FILE *f;
    size_t len;
    if (!path || !text)
        return 0;
    f = fopen(path, "wb");
    if (!f)
        return 0;
    len = strlen(text);
    if (len)
        fwrite(text, 1, len, f);
    fclose(f);
    return 1;
}

static const char *find_field(const char *conf, const char *key,
                              char *dst, size_t dstn)
{
    const char *p = conf;
    size_t klen = strlen(key);
    while (p && *p) {
        if (strncmp(p, key, klen) == 0 && p[klen] == '=') {
            const char *v = p + klen + 1;
            const char *e = v;
            size_t n;
            while (*e && *e != '\n')
                e++;
            n = (size_t)(e - v);
            if (n >= dstn)
                n = dstn - 1;
            memcpy(dst, v, n);
            dst[n] = '\0';
            return dst;
        }
        p = strchr(p, '\n');
        if (p)
            p++;
    }
    return NULL;
}

static int field_int(const char *conf, const char *key, int def)
{
    char b[64];
    return find_field(conf, key, b, sizeof b) ? (int)strtol(b, NULL, 0) : def;
}

static uint64_t field_u64(const char *conf, const char *key, uint64_t def)
{
    char b[64];
    return find_field(conf, key, b, sizeof b) ?
           (uint64_t)strtoull(b, NULL, 0) : def;
}

static void set_field(char *dst, size_t cap, const char *val)
{
    airlock_strlcpy(dst, cap, val ? val : "");
}

/* FNV-1a 64-bit file digest. */
static uint64_t file_hash(const char *path)
{
    uint64_t h = 1469598103934665603ULL;
    unsigned char buf[4096];
    FILE *f;
    size_t n;
    size_t i;
    if (!path)
        return 0;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    while ((n = fread(buf, 1, sizeof buf, f)) > 0) {
        for (i = 0; i < n; i++) {
            h ^= buf[i];
            h *= 1099511628211ULL;
        }
    }
    fclose(f);
    return h;
}

static void hash_hex(char *out, size_t cap, uint64_t h)
{
    snprintf(out, cap, "%016llx", (unsigned long long)h);
}

static uint64_t dir_size_rec(const char *path)
{
    DIR *d;
    struct dirent *ent;
    uint64_t total = 0;
    d = opendir(path);
    if (!d)
        return 0;
    while ((ent = readdir(d)) != NULL) {
        char child[1024];
        struct stat st;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
        if (lstat(child, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode)) {
            total += dir_size_rec(child);
        } else if (S_ISREG(st.st_mode)) {
            if (st.st_size > 0)
                total += (uint64_t)st.st_size;
        }
    }
    closedir(d);
    return total;
}

static int copy_file(const char *src, const char *dst)
{
    FILE *in, *out;
    unsigned char buf[8192];
    size_t n;
    char dir[1024];
    char *slash;
    if (!src || !dst)
        return 0;
    snprintf(dir, sizeof dir, "%s", dst);
    slash = strrchr(dir, '/');
    if (slash) {
        *slash = '\0';
        if (airlock_mkdir_p(dir) != 0)
            return 0;
    }
    in = fopen(src, "rb");
    if (!in)
        return 0;
    out = fopen(dst, "wb");
    if (!out) {
        fclose(in);
        return 0;
    }
    while ((n = fread(buf, 1, sizeof buf, in)) > 0)
        fwrite(buf, 1, n, out);
    fclose(in);
    fclose(out);
    return 1;
}

static airlock_version_mode_t win_mode_from_name(const char *s)
{
    if (!s || !*s)
        return AIRLOCK_WIN_10;
    if (strcasecmp(s, "win7") == 0 || strcasecmp(s, "windows 7") == 0)
        return AIRLOCK_WIN_7;
    if (strcasecmp(s, "win81") == 0 || strcasecmp(s, "windows 8.1") == 0)
        return AIRLOCK_WIN_81;
    if (strcasecmp(s, "win11") == 0 || strcasecmp(s, "windows 11") == 0)
        return AIRLOCK_WIN_11;
    return AIRLOCK_WIN_10;
}

static const char *win_mode_name(airlock_version_mode_t m)
{
    switch (m) {
    case AIRLOCK_WIN_7:  return "win7";
    case AIRLOCK_WIN_81: return "win81";
    case AIRLOCK_WIN_11: return "win11";
    default:            return "win10";
    }
}

static const char *arch_from_machine(uint16_t m)
{
    switch (m) {
    case 0x014c: return "x86";
    case 0x8664: return "x86-64";
    case 0x01c0: return "arm";
    case 0xaa64: return "arm64";
    default:     return "unknown";
    }
}

static const char *trust_name_from_profile(airlock_profile_trust_t t)
{
    return airlock_profile_trust_name(t);
}

static int contains_ci(const char *hay, const char *needle)
{
    size_t hl, nl, i, j;
    if (!hay || !needle)
        return 0;
    hl = strlen(hay);
    nl = strlen(needle);
    if (nl == 0 || nl > hl)
        return 0;
    for (i = 0; i + nl <= hl; i++) {
        for (j = 0; j < nl; j++) {
            if (tolower((unsigned char)hay[i + j]) !=
                tolower((unsigned char)needle[j]))
                break;
        }
        if (j == nl)
            return 1;
    }
    return 0;
}

/* ---- public name helpers -------------------------------------------------- */

const char *airlock_setup_kind_name(airlock_setup_kind_t k)
{
    switch (k) {
    case AIRLOCK_SETUP_MSI:     return "msi";
    case AIRLOCK_SETUP_IMPORT:  return "import";
    case AIRLOCK_SETUP_PORTABLE:return "portable";
    case AIRLOCK_SETUP_EXE:
    default:                   return "exe";
    }
}

const char *airlock_perf_mode_name(airlock_perf_mode_t m)
{
    switch (m) {
    case AIRLOCK_PERF_BATTERY_SAVER: return "battery-saver";
    case AIRLOCK_PERF_PERFORMANCE:   return "performance";
    case AIRLOCK_PERF_CUSTOM:        return "custom";
    default:                        return "balanced";
    }
}

const char *airlock_control_kind_name(airlock_control_kind_t k)
{
    switch (k) {
    case AIRLOCK_CONTROL_GAMEPAD: return "gamepad";
    case AIRLOCK_CONTROL_TOUCH:   return "touch";
    case AIRLOCK_CONTROL_BOTH:    return "gamepad+touch";
    default:                     return "none";
    }
}

const char *airlock_profile_trust_name(airlock_profile_trust_t t)
{
    switch (t) {
    case AIRLOCK_PROFILE_OFFICIAL:    return "official";
    case AIRLOCK_PROFILE_COMMUNITY:   return "community";
    case AIRLOCK_PROFILE_EXPERIMENTAL:return "experimental";
    default:                         return "local";
    }
}

const char *airlock_workspace_root(void)
{
    static char buf[1024];
    const char *env = getenv("AIRLOCK_ROOT");
    if (env && *env) {
        snprintf(buf, sizeof buf, "%s", env);
        return buf;
    }
    return airlock_prefix_dir();
}

/* ---- load / save ---------------------------------------------------------- */

airlock_status_t airlock_workspace_load(const char *root, const char *name,
                                      airlock_workspace_t *out)
{
    char ws[640], path[720];
    char buf[16384];
    if (!root || !valid_name(name) || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    ws_dir(ws, sizeof ws, root, name);
    airlock_path_join(path, sizeof path, ws, "app.conf");
    if (!read_text(path, buf, sizeof buf))
        return AIRLOCK_ERR_INVALID_ARGUMENT;

    memset(out, 0, sizeof *out);
    set_field(out->name, sizeof out->name, name);
    set_field(out->path, sizeof out->path, ws);
    find_field(buf, "id", out->id, sizeof out->id);
    out->setup = (airlock_setup_kind_t)field_int(buf, "setup", (int)AIRLOCK_SETUP_EXE);
    find_field(buf, "setup_label", out->setup_label, sizeof out->setup_label);
    find_field(buf, "source", out->source, sizeof out->source);
    find_field(buf, "executable", out->executable, sizeof out->executable);
    find_field(buf, "architecture", out->architecture, sizeof out->architecture);
    find_field(buf, "runner", out->runner, sizeof out->runner);
    out->windows_version = (airlock_version_mode_t)field_int(buf, "windows_version",
                                                            (int)AIRLOCK_WIN_10);
    find_field(buf, "gfx_backend", out->gfx_backend, sizeof out->gfx_backend);
    find_field(buf, "audio_backend", out->audio_backend, sizeof out->audio_backend);
    find_field(buf, "dll_overrides", out->dll_overrides, sizeof out->dll_overrides);
    find_field(buf, "dependencies", out->dependencies, sizeof out->dependencies);
    find_field(buf, "tags", out->tags, sizeof out->tags);
    find_field(buf, "installed_at", out->installed_at, sizeof out->installed_at);
    find_field(buf, "last_launch", out->last_launch, sizeof out->last_launch);
    find_field(buf, "exe_hash", out->exe_hash, sizeof out->exe_hash);
    find_field(buf, "compat_rating", out->compat_rating, sizeof out->compat_rating);
    out->favorite = field_int(buf, "favorite", 0);
    out->has_shortcut = field_int(buf, "has_shortcut", 0);
    out->install_size = field_u64(buf, "install_size", 0);
    out->perf_mode = (airlock_perf_mode_t)field_int(buf, "perf_mode",
                                                   (int)AIRLOCK_PERF_BALANCED);
    out->resolution_width = (uint32_t)field_int(buf, "resolution_width", 1280);
    out->resolution_height = (uint32_t)field_int(buf, "resolution_height", 720);
    out->dpi = (uint32_t)field_int(buf, "dpi", 96);
    out->virtual_desktop = field_int(buf, "virtual_desktop", 0);
    out->permissions = (uint32_t)field_u64(buf, "permissions", 0);
    find_field(buf, "controls", out->controls, sizeof out->controls);
    out->sandbox_enabled = field_int(buf, "sandbox_enabled", 1);
    return AIRLOCK_OK;
}

airlock_status_t airlock_workspace_save(const airlock_workspace_t *w)
{
    char path[720];
    FILE *f;
    if (!w || !w->name[0])
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (!w->path[0]) {
        char root[1024];
        snprintf(root, sizeof root, "%s", airlock_workspace_root());
        ws_dir((char *)w->path, sizeof w->path, root, w->name);
    }
    airlock_mkdir_p(w->path);
    airlock_path_join(path, sizeof path, w->path, "app.conf");
    f = fopen(path, "w");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    fprintf(f, "# airlock workspace %s\n", w->name);
    fprintf(f, "name=%s\n", w->name);
    fprintf(f, "path=%s\n", w->path);
    fprintf(f, "id=%s\n", w->id);
    fprintf(f, "setup=%d\n", (int)w->setup);
    fprintf(f, "setup_label=%s\n",
            w->setup_label[0] ? w->setup_label : airlock_setup_kind_name(w->setup));
    fprintf(f, "source=%s\n", w->source);
    fprintf(f, "executable=%s\n", w->executable);
    fprintf(f, "architecture=%s\n", w->architecture);
    fprintf(f, "runner=%s\n", w->runner);
    fprintf(f, "windows_version=%d\n", (int)w->windows_version);
    fprintf(f, "gfx_backend=%s\n", w->gfx_backend);
    fprintf(f, "audio_backend=%s\n", w->audio_backend);
    fprintf(f, "dll_overrides=%s\n", w->dll_overrides);
    fprintf(f, "dependencies=%s\n", w->dependencies);
    fprintf(f, "tags=%s\n", w->tags);
    fprintf(f, "installed_at=%s\n", w->installed_at);
    fprintf(f, "last_launch=%s\n", w->last_launch);
    fprintf(f, "exe_hash=%s\n", w->exe_hash);
    fprintf(f, "compat_rating=%s\n", w->compat_rating);
    fprintf(f, "favorite=%d\n", w->favorite);
    fprintf(f, "has_shortcut=%d\n", w->has_shortcut);
    fprintf(f, "install_size=%llu\n", (unsigned long long)w->install_size);
    fprintf(f, "perf_mode=%d\n", (int)w->perf_mode);
    fprintf(f, "resolution_width=%u\n", w->resolution_width);
    fprintf(f, "resolution_height=%u\n", w->resolution_height);
    fprintf(f, "dpi=%u\n", w->dpi);
    fprintf(f, "virtual_desktop=%d\n", w->virtual_desktop);
    fprintf(f, "permissions=%u\n", w->permissions);
    fprintf(f, "controls=%s\n", w->controls);
    fprintf(f, "sandbox_enabled=%d\n", w->sandbox_enabled);
    fclose(f);
    return AIRLOCK_OK;
}

size_t airlock_workspace_list(const char *root, airlock_workspace_t *out,
                             size_t cap)
{
    DIR *d;
    struct dirent *ent;
    size_t n = 0;
    if (!root || !out || cap == 0)
        return 0;
    d = opendir(root);
    if (!d)
        return 0;
    while ((ent = readdir(d)) != NULL && n < cap) {
        char child[720], app[784];
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        snprintf(child, sizeof child, "%s/%s", root, ent->d_name);
        if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        airlock_path_join(app, sizeof app, child, "app.conf");
        if (!file_exists(app))
            continue;
        if (airlock_workspace_load(root, ent->d_name, &out[n]) == AIRLOCK_OK)
            n++;
    }
    closedir(d);
    return n;
}

const airlock_workspace_t *airlock_workspace_find(const char *root,
                                                const char *name)
{
    static airlock_workspace_t tmp;
    if (airlock_workspace_load(root, name, &tmp) != AIRLOCK_OK)
        return NULL;
    return &tmp;
}

airlock_status_t airlock_workspace_remove(const char *root, const char *name)
{
    airlock_workspace_t w;
    if (!root || !valid_name(name))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    return airlock_prefix_delete(root, name);
}

airlock_status_t airlock_workspace_set(const char *root, const char *name,
                                     const char *key, const char *value)
{
    airlock_workspace_t w;
    if (!key || !*key)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (strcmp(key, "name") == 0) {
        set_field(w.name, sizeof w.name, value);
    } else if (strcmp(key, "tags") == 0) {
        set_field(w.tags, sizeof w.tags, value);
    } else if (strcmp(key, "runner") == 0) {
        set_field(w.runner, sizeof w.runner, value);
    } else if (strcmp(key, "gfx") == 0 || strcmp(key, "gfx_backend") == 0) {
        set_field(w.gfx_backend, sizeof w.gfx_backend, value);
    } else if (strcmp(key, "audio") == 0 || strcmp(key, "audio_backend") == 0) {
        set_field(w.audio_backend, sizeof w.audio_backend, value);
    } else if (strcmp(key, "dll_overrides") == 0) {
        set_field(w.dll_overrides, sizeof w.dll_overrides, value);
    } else if (strcmp(key, "dependencies") == 0) {
        set_field(w.dependencies, sizeof w.dependencies, value);
    } else if (strcmp(key, "executable") == 0 || strcmp(key, "launch") == 0) {
        set_field(w.executable, sizeof w.executable, value);
    } else if (strcmp(key, "source") == 0) {
        set_field(w.source, sizeof w.source, value);
    } else if (strcmp(key, "architecture") == 0) {
        set_field(w.architecture, sizeof w.architecture, value);
    } else if (strcmp(key, "windows_version") == 0) {
        w.windows_version = win_mode_from_name(value);
    } else if (strcmp(key, "favorite") == 0) {
        w.favorite = value && atoi(value);
    } else if (strcmp(key, "sandbox") == 0) {
        w.sandbox_enabled = value && atoi(value);
    } else if (strcmp(key, "shortcut") == 0) {
        w.has_shortcut = value && atoi(value);
    } else {
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    return airlock_workspace_save(&w);
}

airlock_status_t airlock_workspace_set_permissions(const char *root,
                                                 const char *name,
                                                 uint32_t permissions)
{
    airlock_workspace_t w;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    w.permissions = permissions;
    return airlock_workspace_save(&w);
}

airlock_status_t airlock_workspace_set_perf_mode(const char *root,
                                               const char *name,
                                               airlock_perf_mode_t mode)
{
    airlock_workspace_t w;
    if (mode >= AIRLOCK_PERF_MODE_COUNT)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    w.perf_mode = mode;
    return airlock_workspace_save(&w);
}

airlock_status_t airlock_workspace_set_controls(const char *root,
                                              const char *name,
                                              const char *controls)
{
    airlock_workspace_t w;
    if (!controls)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    set_field(w.controls, sizeof w.controls, controls);
    return airlock_workspace_save(&w);
}

airlock_status_t airlock_workspace_set_resolution(const char *root,
                                                const char *name,
                                                uint32_t width,
                                                uint32_t height,
                                                uint32_t dpi,
                                                int virtual_desktop)
{
    airlock_workspace_t w;
    if (!width || !height || dpi == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    w.resolution_width = width;
    w.resolution_height = height;
    w.dpi = dpi;
    w.virtual_desktop = virtual_desktop ? 1 : 0;
    return airlock_workspace_save(&w);
}

uint64_t airlock_workspace_size(const char *root, const char *name)
{
    char ws[640];
    if (!root || !valid_name(name))
        return 0;
    ws_dir(ws, sizeof ws, root, name);
    return dir_size_rec(ws);
}

/* ---- install -------------------------------------------------------------- */

static void ws_defaults(airlock_workspace_t *w, const char *root,
                        const char *name, airlock_setup_kind_t setup)
{
    time_t t = time(NULL);
    struct tm *tmv = gmtime(&t);
    char stamp[40];
    memset(w, 0, sizeof *w);
    set_field(w->name, sizeof w->name, name);
    ws_dir(w->path, sizeof w->path, root, name);
    snprintf(w->id, sizeof w->id, "%08x", airlock_hash_str(name));
    w->setup = setup;
    set_field(w->setup_label, sizeof w->setup_label,
              airlock_setup_kind_name(setup));
    snprintf(w->architecture, sizeof w->architecture, "%s", "x86");
    snprintf(w->runner, sizeof w->runner, "%s", "airlock-wine-10.x");
    w->windows_version = AIRLOCK_WIN_10;
    set_field(w->gfx_backend, sizeof w->gfx_backend, "Vulkan");
    set_field(w->audio_backend, sizeof w->audio_backend, "ALSA");
    set_field(w->compat_rating, sizeof w->compat_rating, "UNKNOWN");
    w->perf_mode = AIRLOCK_PERF_BALANCED;
    w->resolution_width = 1280;
    w->resolution_height = 720;
    w->dpi = 96;
    w->permissions = AIRLOCK_PERM_NETWORK;
    set_field(w->controls, sizeof w->controls, "default");
    w->sandbox_enabled = 1;
    if (tmv) {
        snprintf(stamp, sizeof stamp, "%04d-%02d-%02dT%02d:%02d:%02dZ",
                 tmv->tm_year + 1900, tmv->tm_mon + 1, tmv->tm_mday,
                 tmv->tm_hour, tmv->tm_min, tmv->tm_sec);
        set_field(w->installed_at, sizeof w->installed_at, stamp);
    } else {
        set_field(w->installed_at, sizeof w->installed_at, "now");
    }
}

static void detect_source(airlock_workspace_t *w, const char *path)
{
    struct stat st;
    if (!path || stat(path, &st) != 0)
        return;
    if (S_ISREG(st.st_mode)) {
        w->install_size += (uint64_t)st.st_size;
        hash_hex(w->exe_hash, sizeof w->exe_hash, file_hash(path));
    }
    {
        airlock_image_t img;
        airlock_inspect_t ins;
        airlock_analysis_t a;
        if (airlock_image_load_file(path, AIRLOCK_LOAD_DEFAULT, &img) == AIRLOCK_OK) {
            if (airlock_inspect_image(&img, path, &ins) == AIRLOCK_OK) {
                set_field(w->architecture, sizeof w->architecture,
                          arch_from_machine(ins.machine));
                if (ins.graphics[0])
                    set_field(w->gfx_backend, sizeof w->gfx_backend, ins.graphics);
                if (ins.audio[0])
                    set_field(w->audio_backend, sizeof w->audio_backend, ins.audio);
                if (airlock_compat_analyze(&img, ins.basename, &a) == AIRLOCK_OK) {
                    int pct = a.overall_percent;
                    set_field(w->compat_rating, sizeof w->compat_rating,
                              pct >= 90 ? "HIGH" : pct >= 60 ? "MEDIUM" : "LOW");
                }
                if (ins.needs_vcruntime) {
                    if (w->dependencies[0])
                        airlock_strlcat(w->dependencies, sizeof w->dependencies, ",vcruntime");
                    else
                        set_field(w->dependencies, sizeof w->dependencies, "vcruntime");
                }
                if (ins.needs_dotnet) {
                    if (w->dependencies[0])
                        airlock_strlcat(w->dependencies, sizeof w->dependencies, ",dotnet");
                    else
                        set_field(w->dependencies, sizeof w->dependencies, "dotnet");
                }
            }
            airlock_image_unload(&img);
        }
    }
}

airlock_status_t airlock_workspace_install(const char *root, const char *name,
                                         airlock_setup_kind_t setup,
                                         const char *source,
                                         const char *executable,
                                         airlock_workspace_t *out)
{
    airlock_workspace_t w;
    struct stat st;
    if (!root || !valid_name(name) || setup >= AIRLOCK_SETUP_COUNT)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) == AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT; /* already exists */

    if (airlock_prefix_create(root, name) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;

    ws_defaults(&w, root, name, setup);
    set_field(w.source, sizeof w.source, source);
    set_field(w.executable, sizeof w.executable, executable);
    if (source && stat(source, &st) == 0) {
        detect_source(&w, source);
        if (setup == AIRLOCK_SETUP_PORTABLE && S_ISREG(st.st_mode)) {
            char dest[720], games[720];
            const char *base = strrchr(source, '/');
            base = base ? base + 1 : source;
            airlock_path_join(games, sizeof games, w.path, "drive_c/Games");
            if (airlock_mkdir_p(games) == 0) {
                snprintf(dest, sizeof dest, "%s/%s/%s", w.path,
                         "drive_c/Games", base);
                if (copy_file(source, dest))
                    set_field(w.executable, sizeof w.executable, dest);
            }
        }
    }
    if (!w.install_size)
        w.install_size = airlock_workspace_size(root, name);
    if (airlock_workspace_save(&w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;

    /* Create a shortcut beside the workspace so the app is easy to launch. */
    {
        char sdir[720], exec[1024];
        airlock_desktop_entry_t de;
        airlock_path_join(sdir, sizeof sdir, w.path, "shortcuts");
        memset(&de, 0, sizeof de);
        snprintf(de.name, sizeof de.name, "%s", name);
        snprintf(exec, sizeof exec, "airlock app run %s", name);
        airlock_strlcpy(de.exec, sizeof de.exec, exec);
        airlock_strlcpy(de.categories, sizeof de.categories, "Game;");
        if (airlock_desktop_write_shortcut(sdir, &de) == AIRLOCK_OK) {
            w.has_shortcut = 1;
            airlock_workspace_save(&w);
        }
    }
    if (out)
        *out = w;
    return AIRLOCK_OK;
}

/* ---- profile path resolution ---------------------------------------------- */

static void profile_path(char *dst, size_t n, const char *root,
                         const char *name, const char *label)
{
    char ws[640], dir[720];
    ws_dir(ws, sizeof ws, root, name);
    if (!label || !*label) {
        airlock_path_join(dst, n, ws, "profiles/current.profile");
        return;
    }
    if (strchr(label, '/')) {
        airlock_strlcpy(dst, n, label);
        return;
    }
    snprintf(dir, sizeof dir, "%s/profiles", ws);
    airlock_strlcpy(dst, n, dir);
    airlock_strlcat(dst, n, "/");
    airlock_strlcat(dst, n, label);
    airlock_strlcat(dst, n, ".profile");
}

static void profile_yaml_write(FILE *f, const airlock_profile_point_t *p)
{
    fprintf(f, "# Airlock compatibility profile\n");
    fprintf(f, "name: %s\n", p->app_name[0] ? p->app_name : p->label);
    fprintf(f, "label: %s\n", p->label);
    fprintf(f, "runner: %s\n", p->runner);
    fprintf(f, "architecture: %s\n", p->architecture);
    fprintf(f, "windows_version: %s\n", win_mode_name(p->windows_version));
    fprintf(f, "graphics.backend: %s\n", p->gfx_backend);
    fprintf(f, "audio.backend: %s\n", p->audio_backend);
    fprintf(f, "runtime: %s\n", p->runtime);
    fprintf(f, "dependencies: %s\n", p->dependencies);
    fprintf(f, "dll_overrides: %s\n", p->dll_overrides);
    fprintf(f, "windows.resolution: %s\n", p->resolution);
    fprintf(f, "windows.virtual_desktop: %d\n", p->virtual_desktop);
    fprintf(f, "launch.executable: %s\n", p->launch_executable);
    fprintf(f, "launch.arguments: %s\n", p->launch_args);
    fprintf(f, "trust: %s\n", trust_name_from_profile(p->trust));
    fprintf(f, "source: %s\n", p->source);
    fprintf(f, "exe_hash: %s\n", p->exe_hash);
    fprintf(f, "version: %u\n", p->version);
}

static airlock_status_t profile_parse(const char *text,
                                     airlock_profile_point_t *out)
{
    const char *p = text;
    if (!out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    while (p && *p) {
        char line[768];
        const char *e = strchr(p, '\n');
        char *eq;
        size_t len;
        const char *key, *val;
        if (!e)
            len = strlen(p);
        else
            len = (size_t)(e - p);
        if (len >= sizeof line)
            len = sizeof line - 1;
        memcpy(line, p, len);
        line[len] = '\0';
        p = e ? e + 1 : p + len;
        if (line[0] == '#')
            continue;
        if (line[0] == '\n' || line[0] == '\0')
            continue;
        eq = strchr(line, ':');
        if (!eq)
            continue;
        *eq = '\0';
        key = line;
        val = eq + 1;
        while (*val == ' ' || *val == '\t')
            val++;
        if (strcmp(key, "name") == 0) {
            set_field(out->app_name, sizeof out->app_name, val);
        } else if (strcmp(key, "label") == 0) {
            set_field(out->label, sizeof out->label, val);
        } else if (strcmp(key, "runner") == 0) {
            set_field(out->runner, sizeof out->runner, val);
        } else if (strcmp(key, "architecture") == 0) {
            set_field(out->architecture, sizeof out->architecture, val);
        } else if (strcmp(key, "windows_version") == 0) {
            out->windows_version = win_mode_from_name(val);
        } else if (strcmp(key, "graphics.backend") == 0) {
            set_field(out->gfx_backend, sizeof out->gfx_backend, val);
        } else if (strcmp(key, "audio.backend") == 0) {
            set_field(out->audio_backend, sizeof out->audio_backend, val);
        } else if (strcmp(key, "runtime") == 0) {
            set_field(out->runtime, sizeof out->runtime, val);
        } else if (strcmp(key, "dependencies") == 0) {
            set_field(out->dependencies, sizeof out->dependencies, val);
        } else if (strcmp(key, "dll_overrides") == 0) {
            set_field(out->dll_overrides, sizeof out->dll_overrides, val);
        } else if (strcmp(key, "windows.resolution") == 0) {
            set_field(out->resolution, sizeof out->resolution, val);
        } else if (strcmp(key, "windows.virtual_desktop") == 0) {
            out->virtual_desktop = atoi(val);
        } else if (strcmp(key, "launch.executable") == 0) {
            set_field(out->launch_executable, sizeof out->launch_executable, val);
        } else if (strcmp(key, "launch.arguments") == 0) {
            set_field(out->launch_args, sizeof out->launch_args, val);
        } else if (strcmp(key, "trust") == 0) {
            if (strcasecmp(val, "official") == 0)
                out->trust = AIRLOCK_PROFILE_OFFICIAL;
            else if (strcasecmp(val, "community") == 0)
                out->trust = AIRLOCK_PROFILE_COMMUNITY;
            else if (strcasecmp(val, "experimental") == 0)
                out->trust = AIRLOCK_PROFILE_EXPERIMENTAL;
            else
                out->trust = AIRLOCK_PROFILE_LOCAL;
        } else if (strcmp(key, "source") == 0) {
            set_field(out->source, sizeof out->source, val);
        } else if (strcmp(key, "exe_hash") == 0) {
            set_field(out->exe_hash, sizeof out->exe_hash, val);
        } else if (strcmp(key, "version") == 0) {
            out->version = (uint32_t)strtoul(val, NULL, 0);
        }
    }
    return AIRLOCK_OK;
}

airlock_status_t airlock_workspace_profile_save(const char *root,
                                              const char *name,
                                              const airlock_profile_point_t *p,
                                              char *path_out, size_t path_cap)
{
    char path[720], dir[720];
    FILE *f;
    char tmp[640];
    if (!root || !valid_name(name) || !p || !p->label[0])
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    ws_dir(tmp, sizeof tmp, root, name);
    snprintf(dir, sizeof dir, "%s/profiles", tmp);
    if (airlock_mkdir_p(dir) != 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    profile_path(path, sizeof path, root, name, p->label);
    {
        char parent[720];
        char *slash;
        snprintf(parent, sizeof parent, "%s", path);
        slash = strrchr(parent, '/');
        if (slash) {
            *slash = '\0';
            if (airlock_mkdir_p(parent) != 0)
                return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
    }
    f = fopen(path, "w");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    profile_yaml_write(f, p);
    fclose(f);
    if (path_out && path_cap)
        airlock_strlcpy(path_out, path_cap, path);
    return AIRLOCK_OK;
}

airlock_status_t airlock_workspace_profile_load(const char *root,
                                              const char *name,
                                              const char *label,
                                              airlock_profile_point_t *out)
{
    char path[720];
    char buf[8192];
    if (!root || !valid_name(name) || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    profile_path(path, sizeof path, root, name, label);
    if (!read_text(path, buf, sizeof buf))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    return profile_parse(buf, out);
}

airlock_status_t airlock_workspace_profile_current(const char *root,
                                                 const char *name,
                                                 airlock_profile_point_t *out)
{
    char path[720];
    if (!root || !valid_name(name))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    profile_path(path, sizeof path, root, name, NULL);
    if (!out)
        return file_exists(path) ? AIRLOCK_OK : AIRLOCK_ERR_INVALID_ARGUMENT;
    return airlock_workspace_profile_load(root, name, NULL, out);
}

airlock_status_t airlock_workspace_profile_apply(const char *root,
                                               const char *name,
                                               const airlock_profile_point_t *p)
{
    airlock_workspace_t w;
    airlock_profile_point_t cur;
    if (!p || airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (p->runner[0])
        set_field(w.runner, sizeof w.runner, p->runner);
    if (p->architecture[0])
        set_field(w.architecture, sizeof w.architecture, p->architecture);
    w.windows_version = p->windows_version;
    if (p->gfx_backend[0])
        set_field(w.gfx_backend, sizeof w.gfx_backend, p->gfx_backend);
    if (p->audio_backend[0])
        set_field(w.audio_backend, sizeof w.audio_backend, p->audio_backend);
    set_field(w.dll_overrides, sizeof w.dll_overrides, p->dll_overrides);
    set_field(w.dependencies, sizeof w.dependencies, p->dependencies);
    if (p->resolution[0]) {
        unsigned wd = 0, ht = 0;
        if (sscanf(p->resolution, "%ux%u", &wd, &ht) == 2) {
            w.resolution_width = (uint32_t)wd;
            w.resolution_height = (uint32_t)ht;
        }
    }
    w.virtual_desktop = p->virtual_desktop;
    if (p->exe_hash[0])
        set_field(w.exe_hash, sizeof w.exe_hash, p->exe_hash);
    if (airlock_workspace_save(&w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;

    if (airlock_workspace_profile_current(root, name, &cur) != AIRLOCK_OK)
        memset(&cur, 0, sizeof cur);
    if (cur.label[0] != p->label[0] || strcmp(cur.label, p->label) != 0) {
        airlock_profile_point_t tmp = *p;
        char dir[720], active[720];
        FILE *f;
        if (airlock_workspace_profile_save(root, name, &tmp, NULL, 0) != AIRLOCK_OK)
            return AIRLOCK_ERR_INVALID_ARGUMENT;
        /* Write current.profile as the active point. */
        ws_dir(dir, sizeof dir, root, name);
        airlock_strlcpy(active, sizeof active, dir);
        airlock_strlcat(active, sizeof active, "/profiles/current.profile");
        f = fopen(active, "w");
        if (f) {
            profile_yaml_write(f, &tmp);
            fclose(f);
        }
    }
    return AIRLOCK_OK;
}

size_t airlock_workspace_profile_list(const char *root, const char *name,
                                     char out[][64], size_t cap)
{
    char ws[640], path[720];
    DIR *d;
    struct dirent *ent;
    size_t n = 0;
    if (!root || !valid_name(name) || !out || cap == 0)
        return 0;
    ws_dir(ws, sizeof ws, root, name);
    airlock_path_join(path, sizeof path, ws, "profiles");
    d = opendir(path);
    if (!d)
        return 0;
    while ((ent = readdir(d)) != NULL && n < cap) {
        size_t L = strlen(ent->d_name);
        if (L > 8 && strcmp(ent->d_name + (L - 8), ".profile") == 0 &&
            strcmp(ent->d_name, "current.profile") != 0) {
            airlock_strlcpy(out[n], sizeof out[0], ent->d_name);
            out[n][L - 8] = '\0';
            n++;
        }
    }
    closedir(d);
    return n;
}

static int diff_append(char *out, size_t cap, int n, const char *line)
{
    size_t L = strlen(line);
    if (out && cap > 1 && (size_t)n + L < cap - 1) {
        strcat(out, line);
        n += (int)L;
    }
    return n;
}

static int profile_diff_points(const airlock_profile_point_t *a,
                               const airlock_profile_point_t *b,
                               char *out, size_t cap)
{
    int n = 0;
    char line[2048];
    if (out && cap)
        out[0] = '\0';
    if (strcmp(a->runner, b->runner) != 0) {
        snprintf(line, sizeof line, "  runner: %s -> %s\n", a->runner, b->runner);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->architecture, b->architecture) != 0) {
        snprintf(line, sizeof line, "  architecture: %s -> %s\n",
                 a->architecture, b->architecture);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->gfx_backend, b->gfx_backend) != 0) {
        snprintf(line, sizeof line, "  graphics.backend: %s -> %s\n",
                 a->gfx_backend, b->gfx_backend);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->audio_backend, b->audio_backend) != 0) {
        snprintf(line, sizeof line, "  audio.backend: %s -> %s\n",
                 a->audio_backend, b->audio_backend);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->runtime, b->runtime) != 0) {
        snprintf(line, sizeof line, "  runtime: %s -> %s\n", a->runtime, b->runtime);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->dependencies, b->dependencies) != 0) {
        snprintf(line, sizeof line, "  dependencies: %s -> %s\n",
                 a->dependencies, b->dependencies);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->dll_overrides, b->dll_overrides) != 0) {
        snprintf(line, sizeof line, "  dll_overrides: %s -> %s\n",
                 a->dll_overrides, b->dll_overrides);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->resolution, b->resolution) != 0) {
        snprintf(line, sizeof line, "  windows.resolution: %s -> %s\n",
                 a->resolution, b->resolution);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->launch_executable, b->launch_executable) != 0) {
        snprintf(line, sizeof line, "  launch.executable: %s -> %s\n",
                 a->launch_executable, b->launch_executable);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->launch_args, b->launch_args) != 0) {
        snprintf(line, sizeof line, "  launch.arguments: %s -> %s\n",
                 a->launch_args, b->launch_args);
        n = diff_append(out, cap, n, line);
    }
    if (strcmp(a->exe_hash, b->exe_hash) != 0) {
        snprintf(line, sizeof line, "  exe_hash: %s -> %s\n",
                 a->exe_hash, b->exe_hash);
        n = diff_append(out, cap, n, line);
    }
    if (a->windows_version != b->windows_version) {
        snprintf(line, sizeof line, "  windows_version: %s -> %s\n",
                 win_mode_name(a->windows_version),
                 win_mode_name(b->windows_version));
        n = diff_append(out, cap, n, line);
    }
    if (a->virtual_desktop != b->virtual_desktop) {
        snprintf(line, sizeof line, "  windows.virtual_desktop: %d -> %d\n",
                 a->virtual_desktop, b->virtual_desktop);
        n = diff_append(out, cap, n, line);
    }
    return n;
}

int airlock_workspace_profile_diff(const char *root, const char *name,
                                  const char *a, const char *b,
                                  char *out, size_t cap)
{
    airlock_profile_point_t pa, pb;
    if (out && cap)
        out[0] = '\0';
    if (airlock_workspace_profile_load(root, name, a, &pa) != AIRLOCK_OK ||
        airlock_workspace_profile_load(root, name, b, &pb) != AIRLOCK_OK)
        return -1;
    return profile_diff_points(&pa, &pb, out, cap);
}

airlock_status_t airlock_workspace_profile_export(const char *root,
                                                const char *name,
                                                const char *dest_path)
{
    airlock_profile_point_t p;
    FILE *f;
    if (!dest_path)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_profile_current(root, name, &p) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    f = fopen(dest_path, "w");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    profile_yaml_write(f, &p);
    fclose(f);
    return AIRLOCK_OK;
}

airlock_status_t airlock_workspace_profile_import(const char *root,
                                                const char *name,
                                                const char *src_path)
{
    airlock_profile_point_t p;
    char buf[8192];
    char label[64];
    if (!src_path || !read_text(src_path, buf, sizeof buf))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (profile_parse(buf, &p) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    {
        const char *base = strrchr(src_path, '/');
        base = base ? base + 1 : src_path;
        if (strlen(base) > 8 && strcmp(base + (strlen(base) - 8), ".profile") == 0) {
            airlock_strlcpy(label, sizeof label, base);
            label[strlen(base) - 8] = '\0';
        } else {
            const char *dot = strrchr(base, '.');
            if (dot && dot != base)
                snprintf(label, sizeof label, "%.*s", (int)(dot - base), base);
            else
                snprintf(label, sizeof label, "%s", base);
        }
    }
    set_field(p.label, sizeof p.label, label);
    p.trust = AIRLOCK_PROFILE_COMMUNITY;
    if (airlock_workspace_profile_save(root, name, &p, NULL, 0) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    return airlock_workspace_profile_apply(root, name, &p);
}

/* ---- snapshots ------------------------------------------------------------ */

static void snapshot_dir(char *dst, size_t n, const char *root,
                         const char *name, const char *label)
{
    char ws[640];
    ws_dir(ws, sizeof ws, root, name);
    snprintf(dst, n, "%s/snapshots/%s", ws, label);
}

airlock_status_t airlock_workspace_snapshot(const char *root, const char *name,
                                          const char *label)
{
    char ws[640], snap[720], file[800], dest[800];
    airlock_workspace_t w;
    const char *files[] = { "app.conf", "prefix.conf", "profiles/current.profile" };
    size_t i;
    if (!root || !valid_name(name) || !label || !*label ||
        strchr(label, '/'))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    ws_dir(ws, sizeof ws, root, name);
    snapshot_dir(snap, sizeof snap, root, name, label);
    if (airlock_mkdir_p(snap) != 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(file, sizeof file, "%s/%s", ws, files[i]);
        if (file_exists(file)) {
            snprintf(dest, sizeof dest, "%s/%s", snap, files[i]);
            if (!copy_file(file, dest))
                return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
    }
    /* Mark the snapshot with a human label. */
    snprintf(dest, sizeof dest, "%s/label.txt", snap);
    write_text(dest, label);
    return AIRLOCK_OK;
}

airlock_status_t airlock_workspace_rollback(const char *root, const char *name,
                                          const char *label)
{
    char snap[720], file[800], dest[800];
    airlock_workspace_t w;
    const char *files[] = { "app.conf", "prefix.conf", "profiles/current.profile" };
    size_t i;
    if (!root || !valid_name(name) || !label || !*label)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    snapshot_dir(snap, sizeof snap, root, name, label);
    if (!dir_exists(snap))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    for (i = 0; i < sizeof files / sizeof files[0]; i++) {
        snprintf(file, sizeof file, "%s/%s", snap, files[i]);
        if (file_exists(file)) {
            snprintf(dest, sizeof dest, "%s/%s", w.path, files[i]);
            if (!copy_file(file, dest))
                return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
    }
    return AIRLOCK_OK;
}

size_t airlock_workspace_snapshot_list(const char *root, const char *name,
                                      char out[][64], size_t cap)
{
    char snap[720];
    DIR *d;
    struct dirent *ent;
    size_t n = 0;
    if (!root || !valid_name(name) || !out || cap == 0)
        return 0;
    snapshot_dir(snap, sizeof snap, root, name, ".");
    if (airlock_mkdir_p(snap) != 0)
        return 0;
    d = opendir(snap);
    if (!d)
        return 0;
    while ((ent = readdir(d)) != NULL && n < cap) {
        if (ent->d_name[0] == '.')
            continue;
        airlock_strlcpy(out[n], sizeof out[0], ent->d_name);
        n++;
    }
    closedir(d);
    return n;
}

/* ---- repair --------------------------------------------------------------- */

airlock_status_t airlock_workspace_repair(const char *root, const char *name)
{
    char ws[640];
    char path[720];
    airlock_workspace_t w;
    const char *subdirs[] = {
        "drive_c", "drive_c/windows", "runtime", "uninstall",
        "profiles", "snapshots", "shortcuts"
    };
    size_t i;
    if (!root || !valid_name(name))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    ws_dir(ws, sizeof ws, root, name);
    if (airlock_mkdir_p(ws) != 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    for (i = 0; i < sizeof subdirs / sizeof subdirs[0]; i++) {
        snprintf(path, sizeof path, "%s/%s", ws, subdirs[i]);
        if (airlock_mkdir_p(path) != 0)
            return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK) {
        ws_defaults(&w, root, name, AIRLOCK_SETUP_EXE);
        w.install_size = airlock_workspace_size(root, name);
        if (airlock_workspace_save(&w) != AIRLOCK_OK)
            return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    airlock_path_join(path, sizeof path, ws, "prefix.conf");
    if (!file_exists(path)) {
        if (airlock_prefix_set_config(root, name, AIRLOCK_WIN_10,
                                     "Vulkan", "ALSA") != AIRLOCK_OK)
            return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    if (!w.has_shortcut) {
        char exec[1024];
        airlock_desktop_entry_t de;
        snprintf(exec, sizeof exec, "airlock app run %s", name);
        memset(&de, 0, sizeof de);
        set_field(de.name, sizeof de.name, name);
        set_field(de.exec, sizeof de.exec, exec);
        set_field(de.categories, sizeof de.categories, "Game;");
        airlock_path_join(path, sizeof path, ws, "shortcuts");
        if (airlock_desktop_write_shortcut(path, &de) == AIRLOCK_OK) {
            w.has_shortcut = 1;
            airlock_workspace_save(&w);
        }
    }
    return AIRLOCK_OK;
}

/* ---- doctor --------------------------------------------------------------- */

static void add_check(airlock_doctor_report_t *r, const char *name,
                      const char *detail, airlock_doctor_result_t result)
{
    if (!r || r->count >= sizeof r->checks / sizeof r->checks[0])
        return;
    airlock_strlcpy(r->checks[r->count].name, sizeof r->checks[0].name, name);
    airlock_strlcpy(r->checks[r->count].detail, sizeof r->checks[0].detail,
                   detail ? detail : "");
    r->checks[r->count].result = result;
    r->count++;
    if (result == AIRLOCK_DOCTOR_FAIL)
        r->ready = 0;
}

airlock_status_t airlock_workspace_doctor(const char *root, const char *name,
                                        airlock_doctor_report_t *out)
{
    airlock_workspace_t w;
    airlock_doctor_report_t r;
    struct statvfs sv;
    char path[720];
    if (!out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(&r, 0, sizeof r);
    r.ready = 1;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK) {
        add_check(&r, "workspace", "workspace does not exist", AIRLOCK_DOCTOR_FAIL);
        *out = r;
        return AIRLOCK_OK;
    }
    add_check(&r, "workspace", w.path, AIRLOCK_DOCTOR_OK);

    if (!w.executable[0] || !file_exists(w.executable)) {
        add_check(&r, "executable", "launch file missing or not set",
                  AIRLOCK_DOCTOR_FAIL);
    } else {
        add_check(&r, "executable", w.executable, AIRLOCK_DOCTOR_OK);
    }

    snprintf(path, sizeof path, "%s/drive_c", w.path);
    if (dir_exists(path))
        add_check(&r, "prefix", w.path, AIRLOCK_DOCTOR_OK);
    else
        add_check(&r, "prefix", "drive_c missing; run repair", AIRLOCK_DOCTOR_WARN);

    if (statvfs(w.path, &sv) == 0) {
        uint64_t free_bytes = (uint64_t)sv.f_bavail * (uint64_t)sv.f_frsize;
        if (free_bytes < 256ULL * 1024ULL * 1024ULL) {
            char detail[128];
            snprintf(detail, sizeof detail, "%llu MB free (recommend >=256)",
                     (unsigned long long)(free_bytes / (1024ULL * 1024ULL)));
            add_check(&r, "storage", detail, AIRLOCK_DOCTOR_WARN);
        } else {
            add_check(&r, "storage", "enough free space", AIRLOCK_DOCTOR_OK);
        }
    } else {
        add_check(&r, "storage", "could not query", AIRLOCK_DOCTOR_WARN);
    }

    if (strcmp(w.architecture, "x86-64") == 0 || strcmp(w.architecture, "x86") == 0 || strcmp(w.architecture, "arm64") == 0 || strcmp(w.architecture, "arm") == 0) {
        add_check(&r, "architecture", w.architecture, AIRLOCK_DOCTOR_OK);
    } else {
        add_check(&r, "architecture", w.architecture, AIRLOCK_DOCTOR_WARN);
    }

    if (strcmp(w.gfx_backend, "Vulkan") == 0) {
        const char *picked = airlock_backend_pick(AIRLOCK_BACKEND_GRAPHICS);
        if (strcmp(picked ? picked : "", "Vulkan") != 0) {
            add_check(&r, "renderer", "Vulkan requested but unavailable; fallback",
                      AIRLOCK_DOCTOR_WARN);
        } else {
            add_check(&r, "renderer", w.gfx_backend, AIRLOCK_DOCTOR_OK);
        }
    } else if (strcmp(w.gfx_backend, "Software") == 0) {
        add_check(&r, "renderer", "software fallback (expect low performance)",
                  AIRLOCK_DOCTOR_WARN);
    } else {
        add_check(&r, "renderer", w.gfx_backend, AIRLOCK_DOCTOR_OK);
    }

    if (w.dependencies[0]) {
        char tokens[16][32];
        size_t tn = 0;
        char *dup = strdup(w.dependencies);
        char *tok;
        for (tok = strtok(dup, ", "); tok && tn < 16; tok = strtok(NULL, ", ")) {
            snprintf(tokens[tn], sizeof tokens[0], "%s", tok);
            tn++;
        }
        free(dup);
        if (airlock_runtime_init(w.path) != AIRLOCK_OK) {
            add_check(&r, "dependencies", "cannot init runtime manager",
                      AIRLOCK_DOCTOR_WARN);
        } else if (tn == 0) {
            add_check(&r, "dependencies", "none required", AIRLOCK_DOCTOR_OK);
        } else {
            size_t k;
            int missing = 0;
            for (k = 0; k < tn; k++) {
                airlock_runtime_kind_t kind = airlock_runtime_parse(tokens[k]);
                if (!airlock_runtime_is_installed(kind))
                    missing++;
            }
            snprintf(path, sizeof path, "%zu required, %s", tn,
                     missing ? "some missing" : "all installed");
            add_check(&r, "dependencies", path,
                      missing ? AIRLOCK_DOCTOR_WARN : AIRLOCK_DOCTOR_OK);
        }
    } else {
        add_check(&r, "dependencies", "no extra runtimes declared", AIRLOCK_DOCTOR_OK);
    }

    if (w.exe_hash[0])
        add_check(&r, "hash", w.exe_hash, AIRLOCK_DOCTOR_OK);
    else
        add_check(&r, "hash", "not recorded", AIRLOCK_DOCTOR_WARN);

    if (airlock_workspace_profile_current(root, name, NULL) != AIRLOCK_OK)
        add_check(&r, "profile", "no saved profile; run profile apply",
                  AIRLOCK_DOCTOR_WARN);
    else
        add_check(&r, "profile", "active profile present", AIRLOCK_DOCTOR_OK);

    *out = r;
    return AIRLOCK_OK;
}

void airlock_doctor_report(const airlock_doctor_report_t *r)
{
    size_t i;
    const char *labels[4] = { "unknown", "ok", "warn", "fail" };
    if (!r)
        return;
    for (i = 0; i < r->count; i++) {
        const airlock_doctor_check_t *c = &r->checks[i];
        printf("  [%-6s] %-12s %s\n",
               labels[c->result > AIRLOCK_DOCTOR_FAIL ? 0 : (int)c->result],
               c->name, c->detail);
    }
    printf("  launch doctor verdict: %s\n",
           r->ready ? "READY" : "NOT READY");
}

/* ---- support bundle ------------------------------------------------------- */

static void sanitize_line(char *line, size_t n)
{
    const char *home = getenv("HOME");
    const char *user = getenv("USER");
    if (home && *home) {
        size_t hl = strlen(home);
        char *p;
        while ((p = strstr(line, home)) != NULL) {
            size_t tail = strlen(p + hl);
            if ((size_t)(p - line) + 6 <= n)
                memmove(p + 5, p + hl, tail + 1);
            memcpy(p, "$HOME", 5);
        }
    }
    if (user && *user) {
        char pat[128];
        size_t pl;
        char *p;
        snprintf(pat, sizeof pat, "/home/%s", user);
        pl = strlen(pat);
        while ((p = strstr(line, pat)) != NULL) {
            size_t tail = strlen(p + pl);
            if ((size_t)(p - line) + 6 <= n)
                memmove(p + 5, p + pl, tail + 1);
            memcpy(p, "$HOME", 5);
        }
    }
}

airlock_status_t airlock_workspace_support(const char *root, const char *name,
                                         const char *out_path)
{
    airlock_workspace_t w;
    airlock_doctor_report_t dr;
    airlock_profile_point_t p;
    char buf[16384];
    char tmp[16384];
    FILE *f;
    size_t i;
    if (!root || !valid_name(name) || !out_path)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_workspace_doctor(root, name, &dr);
    f = fopen(out_path, "w");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    fprintf(f, "# Airlock support bundle (%s)\n", w.name);
    fprintf(f, "airlock: %s\n", AIRLOCK_VERSION_STRING);
    fprintf(f, "device-report:\n");
    if (airlock_device_report(tmp, sizeof tmp) == AIRLOCK_OK) {
        char *save = NULL;
        char *tok = strtok_r(tmp, "\n", &save);
        while (tok) {
            fprintf(f, "  %s\n", tok);
            tok = strtok_r(NULL, "\n", &save);
        }
    }
    fprintf(f, "workspace:\n"
               "  name=%s\n  setup=%s\n  architecture=%s\n  runner=%s\n"
               "  gfx=%s\n  audio=%s\n  deps=%s\n  exe_hash=%s\n",
            w.name, w.setup_label, w.architecture, w.runner, w.gfx_backend,
            w.audio_backend, w.dependencies, w.exe_hash);
    fprintf(f, "doctor:\n");
    for (i = 0; i < dr.count; i++) {
        const char *labels[4] = { "unknown", "ok", "warn", "fail" };
        fprintf(f, "  %-6s %-12s %s\n",
                labels[dr.checks[i].result > AIRLOCK_DOCTOR_FAIL ? 0 :
                       (int)dr.checks[i].result],
                dr.checks[i].name, dr.checks[i].detail);
    }
    fprintf(f, "verdict: %s\n", dr.ready ? "ready" : "not-ready");
    if (airlock_workspace_profile_current(root, name, &p) == AIRLOCK_OK) {
        fprintf(f, "profile:\n  label=%s\n  version=%u\n  trust=%s\n",
                p.label, p.version, airlock_profile_trust_name(p.trust));
    }
    fprintf(f, "privacy: usernames and home paths redacted\n");
    fclose(f);

    /* Re-open and sanitize so the bundle never leaks host identity. */
    if (!read_text(out_path, tmp, sizeof tmp))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    snprintf(buf, sizeof buf, "%s", tmp);
    sanitize_line(buf, sizeof buf);
    write_text(out_path, buf);
    return AIRLOCK_OK;
}

/* ---- diagnose ------------------------------------------------------------- */

airlock_status_t airlock_workspace_diagnose(const char *log_path,
                                          char *buffer, size_t cap)
{
    FILE *f;
    char line[512];
    size_t off = 0;
    if (!log_path || !buffer || cap == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    buffer[0] = '\0';
    f = fopen(log_path, "r");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        int used = 0;
        if (nl)
            *nl = '\0';
        if (contains_ci(line, "visual c++") || contains_ci(line, "vcruntime") ||
            contains_ci(line, "msvcr")) {
            snprintf(buffer + off, cap - off,
                     "[vc-runtime] missing Visual C++ runtime: %s\n", line);
            used = 1;
        } else if (contains_ci(line, "override conflict") ||
                   contains_ci(line, "dll override")) {
            snprintf(buffer + off, cap - off,
                     "[dll-override] DLL override conflict: %s\n", line);
            used = 1;
        } else if (contains_ci(line, "vulkan") &&
                   (contains_ci(line, "fail") || contains_ci(line, "error") ||
                    contains_ci(line, "unavailable"))) {
            snprintf(buffer + off, cap - off,
                     "[gfx] Vulkan initialization failed: %s\n", line);
            used = 1;
        } else if (contains_ci(line, "d3d") || contains_ci(line, "directx")) {
            snprintf(buffer + off, cap - off,
                     "[gfx] DirectX component missing or old: %s\n", line);
            used = 1;
        } else if (contains_ci(line, "box64") || contains_ci(line, "box86")) {
            snprintf(buffer + off, cap - off,
                     "[box64] CPU translation note: %s\n", line);
            used = 1;
        } else if (strstr(line, "err:") || strstr(line, "err,")) {
            snprintf(buffer + off, cap - off, "[wine] raw error: %s\n", line);
            used = 1;
        }
        if (used)
            off += strlen(buffer + off);
        if (off >= cap - 256)
            break;
    }
    fclose(f);
    if (off == 0)
        snprintf(buffer, cap, "No obvious compatibility issues found in %s.\n",
                 log_path);
    return AIRLOCK_OK;
}

/* ---- permissions / safety ------------------------------------------------- */

void airlock_workspace_permissions_text(uint32_t permissions,
                                       char *buf, size_t cap)
{
    char line[512];
    line[0] = '\0';
    if (permissions & AIRLOCK_PERM_NETWORK)
        airlock_strlcat(line, sizeof line, "can use network; ");
    else
        airlock_strlcat(line, sizeof line, "cannot use network; ");
    if (permissions & AIRLOCK_PERM_SHARED_FILES)
        airlock_strlcat(line, sizeof line, "can read selected shared folders; ");
    else
        airlock_strlcat(line, sizeof line, "cannot access shared folders; ");
    if (permissions & AIRLOCK_PERM_CAMERA)
        airlock_strlcat(line, sizeof line, "can use camera; ");
    else
        airlock_strlcat(line, sizeof line, "cannot use camera; ");
    if (permissions & AIRLOCK_PERM_MIC)
        airlock_strlcat(line, sizeof line, "can use microphone; ");
    else
        airlock_strlcat(line, sizeof line, "cannot use microphone; ");
    if (permissions & AIRLOCK_PERM_EXTERNAL_STORAGE)
        airlock_strlcat(line, sizeof line, "can use external storage");
    else
        airlock_strlcat(line, sizeof line, "cannot use external storage");
    if (buf && cap)
        airlock_strlcpy(buf, cap, line);
}

airlock_status_t airlock_workspace_safety_report(const char *root,
                                               const char *name,
                                               char *buf, size_t cap)
{
    airlock_workspace_t w;
    char perms[512];
    if (!buf || cap == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_workspace_load(root, name, &w) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_workspace_permissions_text(w.permissions, perms, sizeof perms);
    snprintf(buf, cap,
             "Safety report for %s:\n"
             "  sandbox:                 %s\n"
             "  permissions:             %s\n"
             "  source:                  %s\n"
             "  runner:                  %s\n"
             "  dependencies:            %s\n"
             "  warning:                 running an unverified Windows\n"
             "                           executable is not made safe by the\n"
             "                           compatibility layer.\n",
             w.name,
             w.sandbox_enabled ? "enabled (host-opt-in access)" : "disabled",
             perms,
             w.source[0] ? w.source : "(not recorded)",
             w.runner,
             w.dependencies[0] ? w.dependencies : "(none)");
    return AIRLOCK_OK;
}

/* ---- device report -------------------------------------------------------- */

airlock_status_t airlock_device_report(char *buf, size_t cap)
{
    struct utsname u;
    long pages, pagesize;
    uint64_t mem_mb;
    airlock_backend_t backends[8];
    airlock_device_t devices[8];
    size_t n, i;
    if (!buf || cap == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (uname(&u) != 0) {
        snprintf(buf, cap, "unavailable");
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    pages = sysconf(_SC_PHYS_PAGES);
    pagesize = sysconf(_SC_PAGESIZE);
    mem_mb = (pages > 0 && pagesize > 0) ?
             (uint64_t)pages * (uint64_t)pagesize / (1024ULL * 1024ULL) : 0;
    snprintf(buf, cap,
             "OS:       %s %s  (%s)\n"
             "ABI:      %zu-bit\n"
             "RAM:      %llu MB\n"
             "Renderer: %s\n"
             "Audio:    %s\n",
             u.sysname, u.release, u.machine, sizeof(void *) * 8,
             (unsigned long long)mem_mb,
             airlock_backend_pick(AIRLOCK_BACKEND_GRAPHICS) ?
                airlock_backend_pick(AIRLOCK_BACKEND_GRAPHICS) : "unknown",
             airlock_backend_pick(AIRLOCK_BACKEND_AUDIO) ?
                airlock_backend_pick(AIRLOCK_BACKEND_AUDIO) : "unknown");
    /* Show render-path options. */
    n = airlock_backend_list(AIRLOCK_BACKEND_GRAPHICS, backends,
                            sizeof backends / sizeof backends[0]);
    if (n && strlen(buf) + 16 < cap) {
        airlock_strlcat(buf, cap, "Paths:    ");
        for (i = 0; i < n; i++) {
            char tmp[64];
            snprintf(tmp, sizeof tmp, "%s%s", i ? "," : "", backends[i].name);
            airlock_strlcat(buf, cap, tmp);
        }
        airlock_strlcat(buf, cap, "\n");
    }
    n = airlock_device_list(devices, sizeof devices / sizeof devices[0]);
    if (n && strlen(buf) + 16 < cap) {
        airlock_strlcat(buf, cap, "Devices:  ");
        for (i = 0; i < n; i++) {
            char tmp[96];
            snprintf(tmp, sizeof tmp, "%s%s", i ? "," : "", devices[i].name);
            airlock_strlcat(buf, cap, tmp);
        }
        airlock_strlcat(buf, cap, "\n");
    }
    return AIRLOCK_OK;
}

/* ---- shader cache --------------------------------------------------------- */

uint64_t airlock_workspace_shader_size(const char *root, const char *name)
{
    char ws[640], path[720];
    if (!root || !valid_name(name))
        return 0;
    ws_dir(ws, sizeof ws, root, name);
    airlock_path_join(path, sizeof path, ws, "shadercache");
    return dir_exists(path) ? dir_size_rec(path) : 0;
}

airlock_status_t airlock_workspace_shader_clear(const char *root,
                                              const char *name)
{
    char ws[640], path[720], bak[720];
    if (!root || !valid_name(name))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    ws_dir(ws, sizeof ws, root, name);
    airlock_path_join(path, sizeof path, ws, "shadercache");
    if (dir_exists(path)) {
        snprintf(bak, sizeof bak, "%s/shadercache.bak-%u", ws,
                 (unsigned)getpid());
        if (rename(path, bak) != 0)
            return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    if (airlock_mkdir_p(path) != 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    return AIRLOCK_OK;
}
