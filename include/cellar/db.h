/*
 * db.h — Windows application compatibility database.
 *
 * A persistent catalog of known application requirements (architecture,
 * graphics/audio/input, .NET / VC runtime) plus a HIGH/MEDIUM/LOW rating
 * and a list of known issues. Entries are typically produced by the
 * inspector + analyzer and remembered under the prefix as `compat.db`.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_DB_H
#define CELLAR_DB_H

#include <stddef.h>

#include "cellar.h"
#include "compat.h"
#include "inspect.h"

typedef enum cellar_compat_rating {
    CELLAR_RATING_UNKNOWN = 0,
    CELLAR_RATING_LOW,
    CELLAR_RATING_MEDIUM,
    CELLAR_RATING_HIGH
} cellar_compat_rating_t;

typedef struct cellar_db_issue {
    char text[160];
} cellar_db_issue_t;

typedef struct cellar_db_entry {
    char application[128];
    char architecture[16];
    char graphics[32];
    char audio[32];
    char input[32];
    int  needs_dotnet;
    int  needs_vcruntime;
    cellar_compat_rating_t rating;
    cellar_db_issue_t issues[8];
    size_t issue_count;
} cellar_db_entry_t;

typedef struct cellar_db {
    cellar_db_entry_t *entries;
    size_t count;
    size_t cap;
    char   path[1024];
} cellar_db_t;

/* Open (or create empty) a database at `path`. */
cellar_status_t cellar_db_open(cellar_db_t *db, const char *path);

/* Free memory; does not write. */
void cellar_db_close(cellar_db_t *db);

/* Insert or replace an entry keyed by application name. */
cellar_status_t cellar_db_put(cellar_db_t *db, const cellar_db_entry_t *e);

const cellar_db_entry_t *cellar_db_find(const cellar_db_t *db, const char *app);
size_t cellar_db_count(const cellar_db_t *db);
const cellar_db_entry_t *cellar_db_at(const cellar_db_t *db, size_t i);

/* Persist to the path given at open. */
cellar_status_t cellar_db_save(const cellar_db_t *db);

/* Fill an entry from inspector + analyzer results. */
void cellar_db_from_analysis(cellar_db_entry_t *e,
                             const cellar_inspect_t *ins,
                             const cellar_analysis_t *a);

/* Boxed per-application report. */
void cellar_db_report(const cellar_db_entry_t *e);

const char *cellar_rating_name(cellar_compat_rating_t r);

/* Default database path: <prefix>/compat.db */
void cellar_db_default_path(char *dst, size_t n);

#endif /* CELLAR_DB_H */
