/*
 * workspace.h — Winaltor-style isolated application workspaces.
 *
 * Cellar gives you the Windows compatibility primitives (PE loader, Win32
 * layer, prefixes, profiles). This module is the *product layer* on top of
 * them: one isolated workspace per app, guided setup, an app library,
 * versioned compatibility profiles, one-click snapshots/rollback, launch
 * diagnostics, support bundles, permissions, controls, performance modes,
 * and shader-cache management — the features a UI like Winaltor exposes.
 *
 * Layout:
 *
 *   <root>/<name>/                 workspace == isolated prefix
 *     app.conf                     workspace metadata
 *     prefix.conf                  existing prefix settings
 *     drive_c/                     Windows filesystem
 *     profiles/<label>.profile     versioned/exportable compatibility profile
 *     profiles/current.profile     active profile
 *     snapshots/<label>/           rollback point (config snapshot)
 *     shadercache/                 persistent shader cache
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_WORKSPACE_H
#define CELLAR_WORKSPACE_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"
#include "compat.h"

/* ---- Setup kinds ---------------------------------------------------------- */

typedef enum cellar_setup_kind {
    CELLAR_SETUP_EXE = 0,     /* install a Windows .exe setup                 */
    CELLAR_SETUP_MSI,         /* install a Windows .msi package               */
    CELLAR_SETUP_IMPORT,      /* import an existing prefix / portable bottle  */
    CELLAR_SETUP_PORTABLE,    /* add a portable Windows application           */
    CELLAR_SETUP_COUNT
} cellar_setup_kind_t;

const char *cellar_setup_kind_name(cellar_setup_kind_t k);

/* ---- Performance modes ---------------------------------------------------- */

typedef enum cellar_perf_mode {
    CELLAR_PERF_BALANCED = 0,
    CELLAR_PERF_BATTERY_SAVER,
    CELLAR_PERF_PERFORMANCE,
    CELLAR_PERF_CUSTOM,
    CELLAR_PERF_MODE_COUNT
} cellar_perf_mode_t;

const char *cellar_perf_mode_name(cellar_perf_mode_t m);

/* ---- Control schemes ------------------------------------------------------ */

typedef enum cellar_control_kind {
    CELLAR_CONTROL_NONE = 0,
    CELLAR_CONTROL_GAMEPAD,
    CELLAR_CONTROL_TOUCH,
    CELLAR_CONTROL_BOTH,
    CELLAR_CONTROL_KIND_COUNT
} cellar_control_kind_t;

const char *cellar_control_kind_name(cellar_control_kind_t k);

/* ---- Profile trust -------------------------------------------------------- */

typedef enum cellar_profile_trust {
    CELLAR_PROFILE_LOCAL = 0,
    CELLAR_PROFILE_OFFICIAL,
    CELLAR_PROFILE_COMMUNITY,
    CELLAR_PROFILE_EXPERIMENTAL,
    CELLAR_PROFILE_TRUST_COUNT
} cellar_profile_trust_t;

const char *cellar_profile_trust_name(cellar_profile_trust_t t);

/* ---- App permissions ------------------------------------------------------ */

#define CELLAR_PERM_NETWORK           (1u << 0)
#define CELLAR_PERM_SHARED_FILES      (1u << 1)
#define CELLAR_PERM_CAMERA            (1u << 2)
#define CELLAR_PERM_MIC               (1u << 3)
#define CELLAR_PERM_EXTERNAL_STORAGE  (1u << 4)

/* ---- Workspace record ----------------------------------------------------- */

typedef struct cellar_workspace {
    char                  name[64];
    char                  path[640];
    char                  id[32];
    cellar_setup_kind_t   setup;
    char                  setup_label[24];
    char                  source[512];   /* installer / imported prefix        */
    char                  executable[512]; /* launcher inside the workspace    */
    char                  architecture[16];
    char                  runner[64];
    cellar_version_mode_t windows_version;
    char                  gfx_backend[32];
    char                  audio_backend[32];
    char                  dll_overrides[512];
    char                  dependencies[256]; /* comma-separated runtime names  */
    char                  tags[256];
    char                  installed_at[40];
    char                  last_launch[40];
    char                  exe_hash[40];
    char                  compat_rating[16];

    int                   favorite;
    int                   has_shortcut;
    uint64_t              install_size;

    cellar_perf_mode_t    perf_mode;
    uint32_t              resolution_width;
    uint32_t              resolution_height;
    uint32_t              dpi;
    int                   virtual_desktop;

    uint32_t              permissions;
    char                  controls[128];
    int                   sandbox_enabled;
} cellar_workspace_t;

/* Root directory for workspaces (WINALTOR_ROOT, else CELLAR_PREFIX, else the
 * default Cellar prefix directory). */
const char *cellar_workspace_root(void);

/* ---- Guided setup --------------------------------------------------------- */

/* Create a new isolated workspace. `source` is the installer/import target
 * (may be NULL for a blank workspace); `executable` is the launch file (may be
 * NULL — it can be set later and is auto-detected when `source` is an EXE). */
cellar_status_t cellar_workspace_install(const char *root, const char *name,
                                         cellar_setup_kind_t setup,
                                         const char *source,
                                         const char *executable,
                                         cellar_workspace_t *out);

/* ---- App library ---------------------------------------------------------- */

cellar_status_t cellar_workspace_load(const char *root, const char *name,
                                      cellar_workspace_t *out);
cellar_status_t cellar_workspace_save(const cellar_workspace_t *w);
cellar_status_t cellar_workspace_remove(const char *root, const char *name);
size_t cellar_workspace_list(const char *root, cellar_workspace_t *out,
                             size_t cap);
const cellar_workspace_t *cellar_workspace_find(const char *root,
                                                const char *name);

/* Update one string/int field and persist it. */
cellar_status_t cellar_workspace_set(const char *root, const char *name,
                                     const char *key, const char *value);

cellar_status_t cellar_workspace_set_permissions(const char *root,
                                                 const char *name,
                                                 uint32_t permissions);
cellar_status_t cellar_workspace_set_perf_mode(const char *root,
                                               const char *name,
                                               cellar_perf_mode_t mode);
cellar_status_t cellar_workspace_set_controls(const char *root,
                                              const char *name,
                                              const char *controls);
cellar_status_t cellar_workspace_set_resolution(const char *root,
                                                const char *name,
                                                uint32_t width,
                                                uint32_t height,
                                                uint32_t dpi,
                                                int virtual_desktop);

uint64_t cellar_workspace_size(const char *root, const char *name);

/* ---- Compatibility profiles ---------------------------------------------- */

typedef struct cellar_profile_point {
    char                  label[64];
    char                  app_name[64];
    char                  runner[64];
    char                  architecture[16];
    cellar_version_mode_t windows_version;
    char                  gfx_backend[32];
    char                  audio_backend[32];
    char                  runtime[128];     /* box64/esync/fsync hints           */
    char                  dependencies[256];
    char                  dll_overrides[512];
    char                  resolution[32];
    int                   virtual_desktop;
    char                  launch_executable[512];
    char                  launch_args[256];
    cellar_profile_trust_t trust;
    char                  source[192];      /* community source / version         */
    char                  exe_hash[40];
    uint32_t              version;
} cellar_profile_point_t;

/* Save a versioned profile to <ws>/profiles/<label>.profile. Returns the
 * written path in `path_out` when provided. */
cellar_status_t cellar_workspace_profile_save(const char *root,
                                              const char *name,
                                              const cellar_profile_point_t *p,
                                              char *path_out, size_t path_cap);

/* Load a profile by label or path. */
cellar_status_t cellar_workspace_profile_load(const char *root,
                                              const char *name,
                                              const char *label,
                                              cellar_profile_point_t *out);

/* Load the active profile. */
cellar_status_t cellar_workspace_profile_current(const char *root,
                                                 const char *name,
                                                 cellar_profile_point_t *out);

/* Apply a profile point to the workspace and set it as current. */
cellar_status_t cellar_workspace_profile_apply(const char *root,
                                               const char *name,
                                               const cellar_profile_point_t *p);

/* List saved profile labels (returns count; fills `out` up to `cap`). */
size_t cellar_workspace_profile_list(const char *root, const char *name,
                                     char out[][64], size_t cap);

/* Compare two profile files (labels or absolute paths). Returns number of
 * differing lines; writes a human-readable diff into `out`. */
int cellar_workspace_profile_diff(const char *root, const char *name,
                                  const char *a, const char *b,
                                  char *out, size_t cap);

/* Export/import a single profile file. */
cellar_status_t cellar_workspace_profile_export(const char *root,
                                                const char *name,
                                                const char *dest_path);
cellar_status_t cellar_workspace_profile_import(const char *root,
                                                const char *name,
                                                const char *src_path);

/* ---- Snapshots / rollback -------------------------------------------------- */

cellar_status_t cellar_workspace_snapshot(const char *root, const char *name,
                                          const char *label);
cellar_status_t cellar_workspace_rollback(const char *root, const char *name,
                                          const char *label);
size_t cellar_workspace_snapshot_list(const char *root, const char *name,
                                      char out[][64], size_t cap);

/* ---- Repair ---------------------------------------------------------------- */

cellar_status_t cellar_workspace_repair(const char *root, const char *name);

/* ---- Launch doctor --------------------------------------------------------- */

typedef enum cellar_doctor_result {
    CELLAR_DOCTOR_UNKNOWN = 0,
    CELLAR_DOCTOR_OK,
    CELLAR_DOCTOR_WARN,
    CELLAR_DOCTOR_FAIL
} cellar_doctor_result_t;

typedef struct cellar_doctor_check {
    char                name[64];
    char                detail[192];
    cellar_doctor_result_t result;
} cellar_doctor_check_t;

typedef struct cellar_doctor_report {
    cellar_doctor_check_t checks[16];
    size_t                count;
    int                   ready;
} cellar_doctor_report_t;

cellar_status_t cellar_workspace_doctor(const char *root, const char *name,
                                        cellar_doctor_report_t *out);
void cellar_doctor_report(const cellar_doctor_report_t *r);

/* ---- Diagnostics, support, safety ----------------------------------------- */

/* Classify raw Wine/Box64 log lines into readable explanations. */
cellar_status_t cellar_workspace_diagnose(const char *log_path,
                                          char *buffer, size_t cap);

/* Write a sanitized support bundle. */
cellar_status_t cellar_workspace_support(const char *root, const char *name,
                                         const char *out_path);

/* Human-readable permission dashboard. */
void cellar_workspace_permissions_text(uint32_t permissions,
                                       char *buf, size_t cap);

cellar_status_t cellar_workspace_safety_report(const char *root,
                                               const char *name,
                                               char *buf, size_t cap);

/* ---- Device / platform report --------------------------------------------- */

cellar_status_t cellar_device_report(char *buf, size_t cap);

/* ---- Shader cache ---------------------------------------------------------- */

uint64_t cellar_workspace_shader_size(const char *root, const char *name);
cellar_status_t cellar_workspace_shader_clear(const char *root,
                                              const char *name);

#endif /* CELLAR_WORKSPACE_H */
