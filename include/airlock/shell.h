/*
 * shell.h — Windows shell environment compatibility.
 *
 * Maps the well-known Windows environment variables (%APPDATA%, %WINDIR%,
 * …) onto directories inside an application prefix so programs see a
 * familiar layout without touching the host home directory.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_SHELL_H
#define AIRLOCK_SHELL_H

#include <stddef.h>

#include "airlock.h"

typedef enum airlock_shell_var {
    AIRLOCK_ENV_APPDATA = 0,
    AIRLOCK_ENV_LOCALAPPDATA,
    AIRLOCK_ENV_PROGRAMDATA,
    AIRLOCK_ENV_TEMP,
    AIRLOCK_ENV_USERPROFILE,
    AIRLOCK_ENV_WINDIR,
    AIRLOCK_ENV_SYSTEMROOT,
    AIRLOCK_ENV_PROGRAMFILES,
    AIRLOCK_ENV_HOMEDRIVE,
    AIRLOCK_ENV_HOMEPATH,
    AIRLOCK_ENV_COUNT
} airlock_shell_var_t;

/* Bind shell paths to a bottle (`<prefix>/<name>`). Creates nothing yet. */
airlock_status_t airlock_shell_init(const char *bottle);

/* Create the drive_c tree (windows, users, Program Files, …). */
airlock_status_t airlock_shell_ensure_dirs(void);

const char *airlock_shell_get(airlock_shell_var_t v);
const char *airlock_shell_name(airlock_shell_var_t v); /* "APPDATA", … */

/* Expand %VAR% / %var% references. Unknown vars are left verbatim. */
airlock_status_t airlock_shell_expand(const char *in, char *out, size_t cap);

/* The bottle path last passed to airlock_shell_init, or empty. */
const char *airlock_shell_bottle(void);

#endif /* AIRLOCK_SHELL_H */
