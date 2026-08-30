/*
 * db.c — persistent application compatibility database.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cellar/cellar.h"
#include "cellar/compat.h"
#include "cellar/db.h"

const char *cellar_rating_name(cellar_compat_rating_t r)
{
    switch (r) {
    case CELLAR_RATING_HIGH:   return "HIGH";
    case CELLAR_RATING_MEDIUM: return "MEDIUM";
    case CELLAR_RATING_LOW:    return "LOW";
    default:                   return "UNKNOWN";
    }
}

void cellar_db_default_path(char *dst, size_t n)
{
    cellar_path_join(dst, n, cellar_prefix_dir(), "compat.db");
}

cellar_status_t cellar_db_open(cellar_db_t *db, const char *path)
{
    FILE *f;
    char line[512];
    cellar_db_entry_t cur;
    int have = 0;

    if (!db || !path || !*path)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memset(db, 0, sizeof *db);
    snprintf(db->path, sizeof db->path, "%s", path);

    f = fopen(path, "r");
    if (!f)
        return CELLAR_OK; /* empty database */

    memset(&cur, 0, sizeof cur);
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (line[0] == '#' || line[0] == '\0')
            continue;
        if (line[0] == '[') {
            char *end = strchr(line, ']');
            if (have)
                cellar_db_put(db, &cur);
            memset(&cur, 0, sizeof cur);
            if (end) *end = '\0';
            cellar_strlcpy(cur.application, sizeof cur.application, line + 1);
            have = 1;
            continue;
        }
        if (!have)
            continue;
        {
            char *eq = strchr(line, '=');
            const char *k, *v;
            if (!eq)
                continue;
            *eq = '\0';
            k = line;
            v = eq + 1;
            if (strcmp(k, "architecture") == 0)
                snprintf(cur.architecture, sizeof cur.architecture, "%s", v);
            else if (strcmp(k, "graphics") == 0)
                snprintf(cur.graphics, sizeof cur.graphics, "%s", v);
            else if (strcmp(k, "audio") == 0)
                snprintf(cur.audio, sizeof cur.audio, "%s", v);
            else if (strcmp(k, "input") == 0)
                snprintf(cur.input, sizeof cur.input, "%s", v);
            else if (strcmp(k, "dotnet") == 0)
                cur.needs_dotnet = atoi(v);
            else if (strcmp(k, "vcruntime") == 0)
                cur.needs_vcruntime = atoi(v);
            else if (strcmp(k, "rating") == 0)
                cur.rating = (cellar_compat_rating_t)atoi(v);
            else if (strcmp(k, "issue") == 0 && cur.issue_count < 8) {
                snprintf(cur.issues[cur.issue_count].text,
                         sizeof cur.issues[0].text, "%s", v);
                cur.issue_count++;
            }
        }
    }
    if (have)
        cellar_db_put(db, &cur);
    fclose(f);
    return CELLAR_OK;
}

void cellar_db_close(cellar_db_t *db)
{
    if (!db)
        return;
    free(db->entries);
    memset(db, 0, sizeof *db);
}

cellar_status_t cellar_db_put(cellar_db_t *db, const cellar_db_entry_t *e)
{
    size_t i;
    if (!db || !e || !e->application[0])
        return CELLAR_ERR_INVALID_ARGUMENT;
    for (i = 0; i < db->count; i++) {
        if (strcasecmp(db->entries[i].application, e->application) == 0) {
            db->entries[i] = *e;
            return CELLAR_OK;
        }
    }
    if (db->count == db->cap) {
        size_t ncap = db->cap ? db->cap * 2 : 8;
        cellar_db_entry_t *n = realloc(db->entries, ncap * sizeof *n);
        if (!n)
            return CELLAR_ERR_OUT_OF_MEMORY;
        db->entries = n;
        db->cap = ncap;
    }
    db->entries[db->count++] = *e;
    return CELLAR_OK;
}

const cellar_db_entry_t *cellar_db_find(const cellar_db_t *db, const char *app)
{
    size_t i;
    if (!db || !app)
        return NULL;
    for (i = 0; i < db->count; i++)
        if (strcasecmp(db->entries[i].application, app) == 0)
            return &db->entries[i];
    return NULL;
}

size_t cellar_db_count(const cellar_db_t *db)
{
    return db ? db->count : 0;
}

const cellar_db_entry_t *cellar_db_at(const cellar_db_t *db, size_t i)
{
    if (!db || i >= db->count)
        return NULL;
    return &db->entries[i];
}

cellar_status_t cellar_db_save(const cellar_db_t *db)
{
    FILE *f;
    size_t i, j;
    if (!db || !db->path[0])
        return CELLAR_ERR_INVALID_ARGUMENT;
    {
        char dir[1024];
        char *slash;
        snprintf(dir, sizeof dir, "%s", db->path);
        slash = strrchr(dir, '/');
        if (slash) {
            *slash = '\0';
            cellar_mkdir_p(dir);
        }
    }
    f = fopen(db->path, "w");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fprintf(f, "# cellar-compat-db 1\n");
    for (i = 0; i < db->count; i++) {
        const cellar_db_entry_t *e = &db->entries[i];
        fprintf(f, "[%s]\n", e->application);
        fprintf(f, "architecture=%s\n", e->architecture);
        fprintf(f, "graphics=%s\n", e->graphics);
        fprintf(f, "audio=%s\n", e->audio);
        fprintf(f, "input=%s\n", e->input);
        fprintf(f, "dotnet=%d\n", e->needs_dotnet);
        fprintf(f, "vcruntime=%d\n", e->needs_vcruntime);
        fprintf(f, "rating=%d\n", (int)e->rating);
        for (j = 0; j < e->issue_count; j++)
            fprintf(f, "issue=%s\n", e->issues[j].text);
        fprintf(f, "\n");
    }
    fclose(f);
    return CELLAR_OK;
}

void cellar_db_from_analysis(cellar_db_entry_t *e,
                             const cellar_inspect_t *ins,
                             const cellar_analysis_t *a)
{
    size_t i;
    int pct;
    if (!e)
        return;
    memset(e, 0, sizeof *e);
    if (ins) {
        snprintf(e->application, sizeof e->application, "%s", ins->basename);
        snprintf(e->architecture, sizeof e->architecture, "%s",
                 ins->machine == 0x8664 ? "x64" :
                 ins->machine == 0xaa64 ? "ARM64" :
                 ins->is_64bit ? "x64" : "x86");
        snprintf(e->graphics, sizeof e->graphics, "%s", ins->graphics);
        snprintf(e->audio, sizeof e->audio, "%s", ins->audio);
        snprintf(e->input, sizeof e->input, "%s", ins->input);
        e->needs_dotnet = ins->needs_dotnet;
        e->needs_vcruntime = ins->needs_vcruntime;
    }
    if (a) {
        if (!e->graphics[0])
            snprintf(e->graphics, sizeof e->graphics, "%s", a->detected_graphics);
        if (!e->audio[0])
            snprintf(e->audio, sizeof e->audio, "%s", a->detected_audio);
        if (!e->input[0])
            snprintf(e->input, sizeof e->input, "%s", a->detected_input);
        if (!e->application[0])
            cellar_strlcpy(e->application, sizeof e->application, a->called_by);
        if (!e->architecture[0])
            snprintf(e->architecture, sizeof e->architecture, "%s",
                     a->is_64bit ? "x64" : "x86");
        pct = a->overall_percent;
        if (pct >= 90)
            e->rating = CELLAR_RATING_HIGH;
        else if (pct >= 60)
            e->rating = CELLAR_RATING_MEDIUM;
        else
            e->rating = CELLAR_RATING_LOW;
        for (i = 0; i < a->issue_count && e->issue_count < 8; i++) {
            if (a->issues[i].level == CELLAR_ISSUE_INFO)
                continue;
            snprintf(e->issues[e->issue_count].text,
                     sizeof e->issues[0].text, "%s", a->issues[i].text);
            e->issue_count++;
        }
    }
}

void cellar_db_report(const cellar_db_entry_t *e)
{
    size_t i;
    if (!e)
        return;
    printf("Application: %s\n", e->application);
    printf("Architecture: %s\n", e->architecture[0] ? e->architecture : "?");
    printf("\n");
    printf("Graphics: %s\n", e->graphics[0] ? e->graphics : "(none)");
    printf("Audio: %s\n", e->audio[0] ? e->audio : "(none)");
    printf("Input: %s\n", e->input[0] ? e->input : "(none)");
    printf(".NET: %s\n", e->needs_dotnet ? "Required" : "not required");
    printf("VC Runtime: %s\n", e->needs_vcruntime ? "Required" : "not required");
    printf("\n");
    printf("Compatibility: %s\n", cellar_rating_name(e->rating));
    printf("Known issues: %zu\n", e->issue_count);
    for (i = 0; i < e->issue_count; i++)
        printf("  - %s\n", e->issues[i].text);
}
