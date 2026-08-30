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
#ifndef AIRLOCK_DB_H
#define AIRLOCK_DB_H

#include <stddef.h>

#include "airlock.h"
#include "compat.h"
#include "inspect.h"

typedef enum airlock_compat_rating {
    AIRLOCK_RATING_UNKNOWN = 0,
    AIRLOCK_RATING_LOW,
    AIRLOCK_RATING_MEDIUM,
    AIRLOCK_RATING_HIGH
} airlock_compat_rating_t;

typedef struct airlock_db_issue {
    char text[160];
} airlock_db_issue_t;

typedef struct airlock_db_entry {
    char application[128];
    char architecture[16];
    char graphics[32];
    char audio[32];
    char input[32];
    int  needs_dotnet;
    int  needs_vcruntime;
    airlock_compat_rating_t rating;
    airlock_db_issue_t issues[8];
    size_t issue_count;
} airlock_db_entry_t;

typedef struct airlock_db {
    airlock_db_entry_t *entries;
    size_t count;
    size_t cap;
    char   path[1024];
} airlock_db_t;

/* Open (or create empty) a database at `path`. */
airlock_status_t airlock_db_open(airlock_db_t *db, const char *path);

/* Free memory; does not write. */
void airlock_db_close(airlock_db_t *db);

/* Insert or replace an entry keyed by application name. */
airlock_status_t airlock_db_put(airlock_db_t *db, const airlock_db_entry_t *e);

const airlock_db_entry_t *airlock_db_find(const airlock_db_t *db, const char *app);
size_t airlock_db_count(const airlock_db_t *db);
const airlock_db_entry_t *airlock_db_at(const airlock_db_t *db, size_t i);

/* Persist to the path given at open. */
airlock_status_t airlock_db_save(const airlock_db_t *db);

/* Fill an entry from inspector + analyzer results. */
void airlock_db_from_analysis(airlock_db_entry_t *e,
                             const airlock_inspect_t *ins,
                             const airlock_analysis_t *a);

/* Boxed per-application report. */
void airlock_db_report(const airlock_db_entry_t *e);

const char *airlock_rating_name(airlock_compat_rating_t r);

/* Default database path: <prefix>/compat.db */
void airlock_db_default_path(char *dst, size_t n);

#endif /* AIRLOCK_DB_H */
