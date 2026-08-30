/*
 * shell.h — Windows shell environment compatibility.
 *
 * Maps the well-known Windows environment variables (%APPDATA%, %WINDIR%,
 * …) onto directories inside an application prefix so programs see a
 * familiar layout without touching the host home directory.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_SHELL_H
#define CELLAR_SHELL_H

#include <stddef.h>

#include "cellar.h"

typedef enum cellar_shell_var {
    CELLAR_ENV_APPDATA = 0,
    CELLAR_ENV_LOCALAPPDATA,
    CELLAR_ENV_PROGRAMDATA,
    CELLAR_ENV_TEMP,
    CELLAR_ENV_USERPROFILE,
    CELLAR_ENV_WINDIR,
    CELLAR_ENV_SYSTEMROOT,
    CELLAR_ENV_PROGRAMFILES,
    CELLAR_ENV_HOMEDRIVE,
    CELLAR_ENV_HOMEPATH,
    CELLAR_ENV_COUNT
} cellar_shell_var_t;

/* Bind shell paths to a bottle (`<prefix>/<name>`). Creates nothing yet. */
cellar_status_t cellar_shell_init(const char *bottle);

/* Create the drive_c tree (windows, users, Program Files, …). */
cellar_status_t cellar_shell_ensure_dirs(void);

const char *cellar_shell_get(cellar_shell_var_t v);
const char *cellar_shell_name(cellar_shell_var_t v); /* "APPDATA", … */

/* Expand %VAR% / %var% references. Unknown vars are left verbatim. */
cellar_status_t cellar_shell_expand(const char *in, char *out, size_t cap);

/* The bottle path last passed to cellar_shell_init, or empty. */
const char *cellar_shell_bottle(void);

#endif /* CELLAR_SHELL_H */
