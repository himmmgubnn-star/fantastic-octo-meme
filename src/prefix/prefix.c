/*
 * prefix.c — named Windows environment (bottle) manager.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <unistd.h>

#include "airlock/airlock.h"
#include "airlock/compat.h"
#include "airlock/platform.h"
#include "airlock/prefix.h"
#include "airlock/shell.h"

void airlock_prefix_path(char *dst, size_t n, const char *root, const char *name)
{
    airlock_path_join(dst, n, root ? root : ".", name ? name : "");
}

static int valid_name(const char *name)
{
    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    return 1;
}

/*
 * Validate one archive member path before anything is written to disk.
 *
 * Rejects absolute paths (POSIX `/...`, UNC `\\host\share`, or `C:\...`) and
 * any path containing a ".." component, so a hostile or corrupt archive cannot
 * write outside the prefix directory. A ".." that is merely part of a longer
 * name (e.g. "save..bak") is a legitimate filename and is accepted.
 *
 * Returns 1 when the path is safe to extract under the prefix root.
 */
static int archive_member_is_safe(const char *rel)
{
    const char *p;

    if (!rel || !*rel)
        return 0;
    if (rel[0] == '/' || rel[0] == '\\')
        return 0; /* POSIX absolute, or UNC \\host\share */
    if (((rel[0] >= 'A' && rel[0] <= 'Z') || (rel[0] >= 'a' && rel[0] <= 'z')) &&
        rel[1] == ':')
        return 0; /* C:\... drive-absolute */

    for (p = rel; *p; ) {
        const char *sep = strpbrk(p, "/\\");
        size_t len = sep ? (size_t)(sep - p) : strlen(p);

        if (len == 2 && p[0] == '.' && p[1] == '.')
            return 0; /* ".." component */
        if (!sep)
            break;
        p = sep + 1;
    }
    return 1;
}

static void read_conf(const char *bottle, airlock_prefix_info_t *out);

static airlock_status_t write_conf_full(const char *bottle,
                                       airlock_version_mode_t mode,
                                       const char *gfx, const char *audio,
                                       const char *arch, const char *runner)
{
    char path[640];
    FILE *f;
    airlock_path_join(path, sizeof path, bottle, "prefix.conf");
    f = fopen(path, "w");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    fprintf(f, "version_mode=%d\n", (int)mode);
    fprintf(f, "gfx=%s\n", gfx ? gfx : "Vulkan");
    fprintf(f, "audio=%s\n", audio ? audio : "ALSA");
    fprintf(f, "arch=%s\n", arch ? arch : "win64");
    fprintf(f, "runner=%s\n", runner ? runner : "stable");
    fclose(f);
    return AIRLOCK_OK;
}

static airlock_status_t write_conf(const char *bottle, airlock_version_mode_t mode,
                                  const char *gfx, const char *audio)
{
    airlock_prefix_info_t info;
    read_conf(bottle, &info);
    return write_conf_full(bottle, mode, gfx, audio, info.arch, info.runner);
}

static void read_conf(const char *bottle, airlock_prefix_info_t *out)
{
    char path[640], buf[1024];
    FILE *f;
    size_t n;
    airlock_path_join(path, sizeof path, bottle, "prefix.conf");
    f = fopen(path, "r");
    if (!f)
        return;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    {
        const char *p = strstr(buf, "version_mode=");
        if (p) out->version_mode = (airlock_version_mode_t)atoi(p + 13);
        p = strstr(buf, "gfx=");
        if (p) {
            const char *e = strchr(p + 4, '\n');
            size_t L = e ? (size_t)(e - (p + 4)) : strlen(p + 4);
            if (L >= sizeof out->gfx) L = sizeof out->gfx - 1;
            memcpy(out->gfx, p + 4, L);
            out->gfx[L] = '\0';
        }
        p = strstr(buf, "audio=");
        if (p) {
            const char *e = strchr(p + 6, '\n');
            size_t L = e ? (size_t)(e - (p + 6)) : strlen(p + 6);
            if (L >= sizeof out->audio) L = sizeof out->audio - 1;
            memcpy(out->audio, p + 6, L);
            out->audio[L] = '\0';
        }
        p = strstr(buf, "arch=");
        if (p) {
            const char *e = strchr(p + 5, '\n');
            size_t L = e ? (size_t)(e - (p + 5)) : strlen(p + 5);
            if (L >= sizeof out->arch) L = sizeof out->arch - 1;
            memcpy(out->arch, p + 5, L);
            out->arch[L] = '\0';
        } else {
            airlock_strlcpy(out->arch, sizeof out->arch, "win64");
        }
        p = strstr(buf, "runner=");
        if (p) {
            const char *e = strchr(p + 7, '\n');
            size_t L = e ? (size_t)(e - (p + 7)) : strlen(p + 7);
            if (L >= sizeof out->runner) L = sizeof out->runner - 1;
            memcpy(out->runner, p + 7, L);
            out->runner[L] = '\0';
        } else {
            airlock_strlcpy(out->runner, sizeof out->runner, "stable");
        }
    }
}

airlock_status_t airlock_prefix_create_arch(const char *root, const char *name,
                                          const char *arch)
{
    char bottle[640];
    const char *a = (arch && (*arch == 'w' || *arch == 'W')) ? arch : "win64";
    if (!root || !valid_name(name))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_mkdir_p(root);
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    if (airlock_mkdir_p(bottle) != 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_shell_init(bottle) != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_shell_ensure_dirs() != AIRLOCK_OK)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    {
        char rt[700], un[700];
        airlock_path_join(rt, sizeof rt, bottle, "runtime");
        airlock_path_join(un, sizeof un, bottle, "uninstall");
        airlock_mkdir_p(rt);
        airlock_mkdir_p(un);
    }
    if (strncasecmp(a, "win32", 5) == 0)
        a = "win32";
    else
        a = "win64";
    return write_conf_full(bottle, AIRLOCK_WIN_10, "Vulkan", "ALSA", a,
                           "stable");
}

airlock_status_t airlock_prefix_create(const char *root, const char *name)
{
    return airlock_prefix_create_arch(root, name, "win64");
}

static int rm_rf(const char *path)
{
    struct stat st;
    DIR *d;
    struct dirent *ent;
    if (lstat(path, &st) != 0)
        return 0;
    if (S_ISDIR(st.st_mode) && !S_ISLNK(st.st_mode)) {
        d = opendir(path);
        if (d) {
            while ((ent = readdir(d)) != NULL) {
                char child[1024];
                if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
                    continue;
                snprintf(child, sizeof child, "%s/%s", path, ent->d_name);
                rm_rf(child);
            }
            closedir(d);
        }
        return rmdir(path);
    }
    return unlink(path);
}

airlock_status_t airlock_prefix_delete(const char *root, const char *name)
{
    char bottle[640];
    if (!root || !valid_name(name))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    rm_rf(bottle);
    return AIRLOCK_OK;
}

airlock_status_t airlock_prefix_info(const char *root, const char *name,
                                   airlock_prefix_info_t *out)
{
    char bottle[640];
    struct stat st;
    if (!root || !valid_name(name) || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", name);
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    airlock_strlcpy(out->path, sizeof out->path, bottle);
    out->exists = (stat(bottle, &st) == 0 && S_ISDIR(st.st_mode));
    if (out->exists)
        read_conf(bottle, out);
    return AIRLOCK_OK;
}

airlock_status_t airlock_prefix_set_config(const char *root, const char *name,
                                         airlock_version_mode_t mode,
                                         const char *gfx, const char *audio)
{
    char bottle[640];
    airlock_prefix_info_t info;
    if (airlock_prefix_info(root, name, &info) != AIRLOCK_OK || !info.exists)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    return write_conf(bottle, mode, gfx, audio);
}

size_t airlock_prefix_list(const char *root, airlock_prefix_info_t *out, size_t cap)
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
        char child[1024];
        struct stat st;
        if (ent->d_name[0] == '.')
            continue;
        /* The workspace store is reserved, not a container. */
        if (strcmp(ent->d_name, "workspaces") == 0)
            continue;
        snprintf(child, sizeof child, "%s/%s", root, ent->d_name);
        if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        airlock_prefix_info(root, ent->d_name, &out[n]);
        n++;
    }
    closedir(d);
    return n;
}

/* ---- backup / restore ------------------------------------------------------
 *
 * Airlock container archive, format "ALK1". A little-endian stream:
 *
 *     magic   4 bytes, "ALK1"
 *     record  u32 path_len | path_len bytes relative path | u32 data_len | data
 *     end     u32 path_len == 0
 *
 * Archives written before the Airlock rebrand used the magic "CBK1" with the
 * identical record layout. Restore still accepts "CBK1" so backups made by
 * older builds keep working; every new archive is written as "ALK1".
 */
#define AIRLOCK_ARCHIVE_MAGIC    "ALK1"
#define AIRLOCK_ARCHIVE_MAGIC_V0 "CBK1" /* pre-rebrand, accepted on read only */

static uint32_t wr_u32(FILE *f, uint32_t v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v);
    b[1] = (unsigned char)(v >> 8);
    b[2] = (unsigned char)(v >> 16);
    b[3] = (unsigned char)(v >> 24);
    return (uint32_t)fwrite(b, 1, 4, f);
}

static int rd_u32(FILE *f, uint32_t *v)
{
    unsigned char b[4];
    if (fread(b, 1, 4, f) != 4)
        return 0;
    *v = (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
         ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
    return 1;
}

static int backup_file(FILE *out, const char *rel, const char *abs)
{
    FILE *in;
    long len;
    char *buf;
    in = fopen(abs, "rb");
    if (!in)
        return 0;
    if (fseek(in, 0, SEEK_END) != 0) { fclose(in); return 0; }
    len = ftell(in);
    if (len < 0) { fclose(in); return 0; }
    rewind(in);
    buf = malloc((size_t)len ? (size_t)len : 1);
    if (!buf) { fclose(in); return 0; }
    if (len && fread(buf, 1, (size_t)len, in) != (size_t)len) {
        free(buf); fclose(in); return 0;
    }
    fclose(in);
    wr_u32(out, (uint32_t)strlen(rel));
    fwrite(rel, 1, strlen(rel), out);
    wr_u32(out, (uint32_t)len);
    if (len)
        fwrite(buf, 1, (size_t)len, out);
    free(buf);
    return 1;
}

static void backup_walk(FILE *out, const char *bottle, const char *rel)
{
    char abs[1024];
    DIR *d;
    struct dirent *ent;
    airlock_path_join(abs, sizeof abs, bottle, rel);
    d = opendir(abs);
    if (!d)
        return;
    while ((ent = readdir(d)) != NULL) {
        char child_rel[1024], child_abs[1024];
        struct stat st;
        if (strcmp(ent->d_name, ".") == 0 || strcmp(ent->d_name, "..") == 0)
            continue;
        if (rel[0])
            snprintf(child_rel, sizeof child_rel, "%s/%s", rel, ent->d_name);
        else
            snprintf(child_rel, sizeof child_rel, "%s", ent->d_name);
        airlock_path_join(child_abs, sizeof child_abs, bottle, child_rel);
        if (lstat(child_abs, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            backup_walk(out, bottle, child_rel);
        else if (S_ISREG(st.st_mode))
            backup_file(out, child_rel, child_abs);
    }
    closedir(d);
}

airlock_status_t airlock_prefix_backup(const char *root, const char *name,
                                     const char *archive_path)
{
    char bottle[640];
    FILE *f;
    if (!root || !valid_name(name) || !archive_path)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    f = fopen(archive_path, "wb");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    fwrite(AIRLOCK_ARCHIVE_MAGIC, 1, 4, f);
    backup_walk(f, bottle, "");
    wr_u32(f, 0); /* terminator */
    fclose(f);
    return AIRLOCK_OK;
}

airlock_status_t airlock_prefix_restore(const char *root, const char *name,
                                      const char *archive_path)
{
    char bottle[640], magic[4];
    FILE *f;
    if (!root || !valid_name(name) || !archive_path)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    f = fopen(archive_path, "rb");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (fread(magic, 1, 4, f) != 4 ||
        (memcmp(magic, AIRLOCK_ARCHIVE_MAGIC, 4) != 0 &&
         memcmp(magic, AIRLOCK_ARCHIVE_MAGIC_V0, 4) != 0)) {
        fclose(f);
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    airlock_prefix_create(root, name);
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    for (;;) {
        uint32_t plen = 0, dlen = 0;
        char rel[512], abs[1024], dir[1024];
        char *slash;
        char *data;
        FILE *out;
        if (!rd_u32(f, &plen) || plen == 0)
            break;
        if (plen >= sizeof rel) {
            fclose(f);
            return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
        if (fread(rel, 1, plen, f) != plen) {
            fclose(f);
            return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
        rel[plen] = '\0';
        if (!archive_member_is_safe(rel)) {
            fclose(f);
            return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
        if (!rd_u32(f, &dlen)) {
            fclose(f);
            return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
        data = malloc(dlen ? dlen : 1);
        if (!data) {
            fclose(f);
            return AIRLOCK_ERR_OUT_OF_MEMORY;
        }
        if (dlen && fread(data, 1, dlen, f) != dlen) {
            free(data);
            fclose(f);
            return AIRLOCK_ERR_INVALID_ARGUMENT;
        }
        airlock_path_join(abs, sizeof abs, bottle, rel);
        snprintf(dir, sizeof dir, "%s", abs);
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            airlock_mkdir_p(dir);
        }
        out = fopen(abs, "wb");
        if (out) {
            if (dlen)
                fwrite(data, 1, dlen, out);
            fclose(out);
        }
        free(data);
    }
    fclose(f);
    return AIRLOCK_OK;
}

airlock_status_t airlock_prefix_export(const char *root, const char *name,
                                     const char *archive_path)
{
    return airlock_prefix_backup(root, name, archive_path);
}

airlock_status_t airlock_prefix_import(const char *root, const char *name,
                                     const char *archive_path)
{
    return airlock_prefix_restore(root, name, archive_path);
}

airlock_status_t airlock_prefix_clone(const char *root, const char *src,
                                    const char *dst)
{
    char tmp[700];
    airlock_prefix_info_t info;
    airlock_status_t st;
    if (!root || !valid_name(src) || !valid_name(dst))
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_prefix_info(root, src, &info) != AIRLOCK_OK || !info.exists)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    snprintf(tmp, sizeof tmp, "%s/.clone-%s-%u.cbk", root, src,
             (unsigned)airlock_getpid());
    st = airlock_prefix_backup(root, src, tmp);
    if (st != AIRLOCK_OK)
        return st;
    st = airlock_prefix_restore(root, dst, tmp);
    unlink(tmp);
    return st;
}

/* ---- generic key=value settings on prefix.conf ------------------------- */

airlock_status_t airlock_prefix_get_setting(const char *root, const char *name,
                                          const char *key,
                                          char *out, size_t cap)
{
    airlock_prefix_info_t info;
    char bottle[640], path[640], buf[2048];
    FILE *f;
    size_t n;
    const char *p, *nl;
    size_t kl;
    if (!key || !out || cap == 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_prefix_info(root, name, &info) != AIRLOCK_OK || !info.exists)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    airlock_path_join(path, sizeof path, bottle, "prefix.conf");
    f = fopen(path, "r");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    kl = strlen(key);
    for (p = buf; *p; ) {
        nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
            const char *v = p + kl + 1;
            size_t vl = len - kl - 1;
            if (vl >= cap) vl = cap - 1;
            memcpy(out, v, vl);
            out[vl] = '\0';
            return AIRLOCK_OK;
        }
        if (!nl)
            break;
        p = nl + 1;
    }
    out[0] = '\0';
    return AIRLOCK_OK;
}

airlock_status_t airlock_prefix_set_setting(const char *root, const char *name,
                                          const char *key, const char *value)
{
    airlock_prefix_info_t info;
    char bottle[640], path[640], buf[4096], nline[520];
    FILE *f;
    size_t n, kl;
    int found = 0;
    if (!key || !*key || !value)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (airlock_prefix_info(root, name, &info) != AIRLOCK_OK || !info.exists)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    airlock_prefix_path(bottle, sizeof bottle, root, name);
    airlock_path_join(path, sizeof path, bottle, "prefix.conf");
    f = fopen(path, "r");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    kl = strlen(key);
    snprintf(nline, sizeof nline, "%s=%s\n", key, value);
    f = fopen(path, "w");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    /* Rebuild the file line by line, replacing the target key. */
    {
        char *p = buf, *nl;
        while (*p) {
            nl = strchr(p, '\n');
            size_t len = nl ? (size_t)(nl - p) : strlen(p);
            if (len > kl && strncmp(p, key, kl) == 0 && p[kl] == '=') {
                fputs(nline, f);
                found = 1;
            } else {
                fwrite(p, 1, len, f);
                fputc('\n', f);
            }
            if (!nl)
                break;
            p = nl + 1;
        }
    }
    if (!found)
        fputs(nline, f);
    fclose(f);
    return AIRLOCK_OK;
}
