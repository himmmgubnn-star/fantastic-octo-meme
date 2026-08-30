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
#include <sys/stat.h>
#include <unistd.h>

#include "cellar/cellar.h"
#include "cellar/compat.h"
#include "cellar/prefix.h"
#include "cellar/shell.h"

void cellar_prefix_path(char *dst, size_t n, const char *root, const char *name)
{
    cellar_path_join(dst, n, root ? root : ".", name ? name : "");
}

static int valid_name(const char *name)
{
    if (!name || !*name || strchr(name, '/') || strchr(name, '\\') ||
        strcmp(name, ".") == 0 || strcmp(name, "..") == 0)
        return 0;
    return 1;
}

static cellar_status_t write_conf(const char *bottle, cellar_version_mode_t mode,
                                  const char *gfx, const char *audio)
{
    char path[640];
    FILE *f;
    cellar_path_join(path, sizeof path, bottle, "prefix.conf");
    f = fopen(path, "w");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fprintf(f, "version_mode=%d\n", (int)mode);
    fprintf(f, "gfx=%s\n", gfx ? gfx : "Vulkan");
    fprintf(f, "audio=%s\n", audio ? audio : "ALSA");
    fclose(f);
    return CELLAR_OK;
}

static void read_conf(const char *bottle, cellar_prefix_info_t *out)
{
    char path[640], buf[1024];
    FILE *f;
    size_t n;
    cellar_path_join(path, sizeof path, bottle, "prefix.conf");
    f = fopen(path, "r");
    if (!f)
        return;
    n = fread(buf, 1, sizeof buf - 1, f);
    fclose(f);
    buf[n] = '\0';
    {
        const char *p = strstr(buf, "version_mode=");
        if (p) out->version_mode = (cellar_version_mode_t)atoi(p + 13);
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
    }
}

cellar_status_t cellar_prefix_create(const char *root, const char *name)
{
    char bottle[640];
    if (!root || !valid_name(name))
        return CELLAR_ERR_INVALID_ARGUMENT;
    cellar_mkdir_p(root);
    cellar_prefix_path(bottle, sizeof bottle, root, name);
    if (cellar_mkdir_p(bottle) != 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (cellar_shell_init(bottle) != CELLAR_OK)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (cellar_shell_ensure_dirs() != CELLAR_OK)
        return CELLAR_ERR_INVALID_ARGUMENT;
    {
        char rt[700], un[700];
        cellar_path_join(rt, sizeof rt, bottle, "runtime");
        cellar_path_join(un, sizeof un, bottle, "uninstall");
        cellar_mkdir_p(rt);
        cellar_mkdir_p(un);
    }
    return write_conf(bottle, CELLAR_WIN_10, "Vulkan", "ALSA");
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

cellar_status_t cellar_prefix_delete(const char *root, const char *name)
{
    char bottle[640];
    if (!root || !valid_name(name))
        return CELLAR_ERR_INVALID_ARGUMENT;
    cellar_prefix_path(bottle, sizeof bottle, root, name);
    rm_rf(bottle);
    return CELLAR_OK;
}

cellar_status_t cellar_prefix_info(const char *root, const char *name,
                                   cellar_prefix_info_t *out)
{
    char bottle[640];
    struct stat st;
    if (!root || !valid_name(name) || !out)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    snprintf(out->name, sizeof out->name, "%s", name);
    cellar_prefix_path(bottle, sizeof bottle, root, name);
    cellar_strlcpy(out->path, sizeof out->path, bottle);
    out->exists = (stat(bottle, &st) == 0 && S_ISDIR(st.st_mode));
    if (out->exists)
        read_conf(bottle, out);
    return CELLAR_OK;
}

cellar_status_t cellar_prefix_set_config(const char *root, const char *name,
                                         cellar_version_mode_t mode,
                                         const char *gfx, const char *audio)
{
    char bottle[640];
    cellar_prefix_info_t info;
    if (cellar_prefix_info(root, name, &info) != CELLAR_OK || !info.exists)
        return CELLAR_ERR_INVALID_ARGUMENT;
    cellar_prefix_path(bottle, sizeof bottle, root, name);
    return write_conf(bottle, mode, gfx, audio);
}

size_t cellar_prefix_list(const char *root, cellar_prefix_info_t *out, size_t cap)
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
        snprintf(child, sizeof child, "%s/%s", root, ent->d_name);
        if (stat(child, &st) != 0 || !S_ISDIR(st.st_mode))
            continue;
        cellar_prefix_info(root, ent->d_name, &out[n]);
        n++;
    }
    closedir(d);
    return n;
}

/* ---- backup / restore: "CBK1" stream of (path_len, path, data_len, data)  */

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
    cellar_path_join(abs, sizeof abs, bottle, rel);
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
        cellar_path_join(child_abs, sizeof child_abs, bottle, child_rel);
        if (lstat(child_abs, &st) != 0)
            continue;
        if (S_ISDIR(st.st_mode))
            backup_walk(out, bottle, child_rel);
        else if (S_ISREG(st.st_mode))
            backup_file(out, child_rel, child_abs);
    }
    closedir(d);
}

cellar_status_t cellar_prefix_backup(const char *root, const char *name,
                                     const char *archive_path)
{
    char bottle[640];
    FILE *f;
    if (!root || !valid_name(name) || !archive_path)
        return CELLAR_ERR_INVALID_ARGUMENT;
    cellar_prefix_path(bottle, sizeof bottle, root, name);
    f = fopen(archive_path, "wb");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fwrite("CBK1", 1, 4, f);
    backup_walk(f, bottle, "");
    wr_u32(f, 0); /* terminator */
    fclose(f);
    return CELLAR_OK;
}

cellar_status_t cellar_prefix_restore(const char *root, const char *name,
                                      const char *archive_path)
{
    char bottle[640], magic[4];
    FILE *f;
    if (!root || !valid_name(name) || !archive_path)
        return CELLAR_ERR_INVALID_ARGUMENT;
    f = fopen(archive_path, "rb");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (fread(magic, 1, 4, f) != 4 || memcmp(magic, "CBK1", 4) != 0) {
        fclose(f);
        return CELLAR_ERR_INVALID_ARGUMENT;
    }
    cellar_prefix_create(root, name);
    cellar_prefix_path(bottle, sizeof bottle, root, name);
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
            return CELLAR_ERR_INVALID_ARGUMENT;
        }
        if (fread(rel, 1, plen, f) != plen) {
            fclose(f);
            return CELLAR_ERR_INVALID_ARGUMENT;
        }
        rel[plen] = '\0';
        if (strstr(rel, "..")) {
            fclose(f);
            return CELLAR_ERR_INVALID_ARGUMENT;
        }
        if (!rd_u32(f, &dlen)) {
            fclose(f);
            return CELLAR_ERR_INVALID_ARGUMENT;
        }
        data = malloc(dlen ? dlen : 1);
        if (!data) {
            fclose(f);
            return CELLAR_ERR_OUT_OF_MEMORY;
        }
        if (dlen && fread(data, 1, dlen, f) != dlen) {
            free(data);
            fclose(f);
            return CELLAR_ERR_INVALID_ARGUMENT;
        }
        cellar_path_join(abs, sizeof abs, bottle, rel);
        snprintf(dir, sizeof dir, "%s", abs);
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            cellar_mkdir_p(dir);
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
    return CELLAR_OK;
}
