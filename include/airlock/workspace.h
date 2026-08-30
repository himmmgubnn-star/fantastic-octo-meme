/*
 * workspace.h — Airlock-style isolated application workspaces.
 *
 * Airlock gives you the Windows compatibility primitives (PE loader, Win32
 * layer, prefixes, profiles). This module is the *product layer* on top of
 * them: one isolated workspace per app, guided setup, an app library,
 * versioned compatibility profiles, one-click snapshots/rollback, launch
 * diagnostics, support bundles, permissions, controls, performance modes,
 * and shader-cache management — the features a UI like Airlock exposes.
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
#ifndef AIRLOCK_WORKSPACE_H
#define AIRLOCK_WORKSPACE_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"
#include "compat.h"

/* ---- Setup kinds ---------------------------------------------------------- */

typedef enum airlock_setup_kind {
    AIRLOCK_SETUP_EXE = 0,     /* install a Windows .exe setup                 */
    AIRLOCK_SETUP_MSI,         /* install a Windows .msi package               */
    AIRLOCK_SETUP_IMPORT,      /* import an existing prefix / portable bottle  */
    AIRLOCK_SETUP_PORTABLE,    /* add a portable Windows application           */
    AIRLOCK_SETUP_COUNT
} airlock_setup_kind_t;

const char *airlock_setup_kind_name(airlock_setup_kind_t k);

/* ---- Performance modes ---------------------------------------------------- */

typedef enum airlock_perf_mode {
    AIRLOCK_PERF_BALANCED = 0,
    AIRLOCK_PERF_BATTERY_SAVER,
    AIRLOCK_PERF_PERFORMANCE,
    AIRLOCK_PERF_CUSTOM,
    AIRLOCK_PERF_MODE_COUNT
} airlock_perf_mode_t;

const char *airlock_perf_mode_name(airlock_perf_mode_t m);

/* ---- Control schemes ------------------------------------------------------ */

typedef enum airlock_control_kind {
    AIRLOCK_CONTROL_NONE = 0,
    AIRLOCK_CONTROL_GAMEPAD,
    AIRLOCK_CONTROL_TOUCH,
    AIRLOCK_CONTROL_BOTH,
    AIRLOCK_CONTROL_KIND_COUNT
} airlock_control_kind_t;

const char *airlock_control_kind_name(airlock_control_kind_t k);

/* ---- Profile trust -------------------------------------------------------- */

typedef enum airlock_profile_trust {
    AIRLOCK_PROFILE_LOCAL = 0,
    AIRLOCK_PROFILE_OFFICIAL,
    AIRLOCK_PROFILE_COMMUNITY,
    AIRLOCK_PROFILE_EXPERIMENTAL,
    AIRLOCK_PROFILE_TRUST_COUNT
} airlock_profile_trust_t;

const char *airlock_profile_trust_name(airlock_profile_trust_t t);

/* ---- App permissions ------------------------------------------------------ */

#define AIRLOCK_PERM_NETWORK           (1u << 0)
#define AIRLOCK_PERM_SHARED_FILES      (1u << 1)
#define AIRLOCK_PERM_CAMERA            (1u << 2)
#define AIRLOCK_PERM_MIC               (1u << 3)
#define AIRLOCK_PERM_EXTERNAL_STORAGE  (1u << 4)

/* ---- Workspace record ----------------------------------------------------- */

typedef struct airlock_workspace {
    char                  name[64];
    char                  path[640];
    char                  id[32];
    airlock_setup_kind_t   setup;
    char                  setup_label[24];
    char                  source[512];   /* installer / imported prefix        */
    char                  executable[512]; /* launcher inside the workspace    */
    char                  architecture[16];
    char                  runner[64];
    airlock_version_mode_t windows_version;
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

    airlock_perf_mode_t    perf_mode;
    uint32_t              resolution_width;
    uint32_t              resolution_height;
    uint32_t              dpi;
    int                   virtual_desktop;

    uint32_t              permissions;
    char                  controls[128];
    int                   sandbox_enabled;
} airlock_workspace_t;

/* Root directory for workspaces (AIRLOCK_ROOT, else AIRLOCK_PREFIX, else the
 * default Airlock prefix directory). */
const char *airlock_workspace_root(void);

/* ---- Guided setup --------------------------------------------------------- */

/* Create a new isolated workspace. `source` is the installer/import target
 * (may be NULL for a blank workspace); `executable` is the launch file (may be
 * NULL — it can be set later and is auto-detected when `source` is an EXE). */
airlock_status_t airlock_workspace_install(const char *root, const char *name,
                                         airlock_setup_kind_t setup,
                                         const char *source,
                                         const char *executable,
                                         airlock_workspace_t *out);

/* ---- App library ---------------------------------------------------------- */

airlock_status_t airlock_workspace_load(const char *root, const char *name,
                                      airlock_workspace_t *out);
airlock_status_t airlock_workspace_save(const airlock_workspace_t *w);
airlock_status_t airlock_workspace_remove(const char *root, const char *name);
size_t airlock_workspace_list(const char *root, airlock_workspace_t *out,
                             size_t cap);
const airlock_workspace_t *airlock_workspace_find(const char *root,
                                                const char *name);

/* Update one string/int field and persist it. */
airlock_status_t airlock_workspace_set(const char *root, const char *name,
                                     const char *key, const char *value);

airlock_status_t airlock_workspace_set_permissions(const char *root,
                                                 const char *name,
                                                 uint32_t permissions);
airlock_status_t airlock_workspace_set_perf_mode(const char *root,
                                               const char *name,
                                               airlock_perf_mode_t mode);
airlock_status_t airlock_workspace_set_controls(const char *root,
                                              const char *name,
                                              const char *controls);
airlock_status_t airlock_workspace_set_resolution(const char *root,
                                                const char *name,
                                                uint32_t width,
                                                uint32_t height,
                                                uint32_t dpi,
                                                int virtual_desktop);

uint64_t airlock_workspace_size(const char *root, const char *name);

/* ---- Compatibility profiles ---------------------------------------------- */

typedef struct airlock_profile_point {
    char                  label[64];
    char                  app_name[64];
    char                  runner[64];
    char                  architecture[16];
    airlock_version_mode_t windows_version;
    char                  gfx_backend[32];
    char                  audio_backend[32];
    char                  runtime[128];     /* box64/esync/fsync hints           */
    char                  dependencies[256];
    char                  dll_overrides[512];
    char                  resolution[32];
    int                   virtual_desktop;
    char                  launch_executable[512];
    char                  launch_args[256];
    airlock_profile_trust_t trust;
    char                  source[192];      /* community source / version         */
    char                  exe_hash[40];
    uint32_t              version;
} airlock_profile_point_t;

/* Save a versioned profile to <ws>/profiles/<label>.profile. Returns the
 * written path in `path_out` when provided. */
airlock_status_t airlock_workspace_profile_save(const char *root,
                                              const char *name,
                                              const airlock_profile_point_t *p,
                                              char *path_out, size_t path_cap);

/* Load a profile by label or path. */
airlock_status_t airlock_workspace_profile_load(const char *root,
                                              const char *name,
                                              const char *label,
                                              airlock_profile_point_t *out);

/* Load the active profile. */
airlock_status_t airlock_workspace_profile_current(const char *root,
                                                 const char *name,
                                                 airlock_profile_point_t *out);

/* Apply a profile point to the workspace and set it as current. */
airlock_status_t airlock_workspace_profile_apply(const char *root,
                                               const char *name,
                                               const airlock_profile_point_t *p);

/* List saved profile labels (returns count; fills `out` up to `cap`). */
size_t airlock_workspace_profile_list(const char *root, const char *name,
                                     char out[][64], size_t cap);

/* Compare two profile files (labels or absolute paths). Returns number of
 * differing lines; writes a human-readable diff into `out`. */
int airlock_workspace_profile_diff(const char *root, const char *name,
                                  const char *a, const char *b,
                                  char *out, size_t cap);

/* Export/import a single profile file. */
airlock_status_t airlock_workspace_profile_export(const char *root,
                                                const char *name,
                                                const char *dest_path);
airlock_status_t airlock_workspace_profile_import(const char *root,
                                                const char *name,
                                                const char *src_path);

/* ---- Snapshots / rollback -------------------------------------------------- */

airlock_status_t airlock_workspace_snapshot(const char *root, const char *name,
                                          const char *label);
airlock_status_t airlock_workspace_rollback(const char *root, const char *name,
                                          const char *label);
size_t airlock_workspace_snapshot_list(const char *root, const char *name,
                                      char out[][64], size_t cap);

/* ---- Repair ---------------------------------------------------------------- */

airlock_status_t airlock_workspace_repair(const char *root, const char *name);

/* ---- Launch doctor --------------------------------------------------------- */

typedef enum airlock_doctor_result {
    AIRLOCK_DOCTOR_UNKNOWN = 0,
    AIRLOCK_DOCTOR_OK,
    AIRLOCK_DOCTOR_WARN,
    AIRLOCK_DOCTOR_FAIL
} airlock_doctor_result_t;

typedef struct airlock_doctor_check {
    char                name[64];
    char                detail[192];
    airlock_doctor_result_t result;
} airlock_doctor_check_t;

typedef struct airlock_doctor_report {
    airlock_doctor_check_t checks[16];
    size_t                count;
    int                   ready;
} airlock_doctor_report_t;

airlock_status_t airlock_workspace_doctor(const char *root, const char *name,
                                        airlock_doctor_report_t *out);
void airlock_doctor_report(const airlock_doctor_report_t *r);

/* ---- Diagnostics, support, safety ----------------------------------------- */

/* Classify raw Wine/Box64 log lines into readable explanations. */
airlock_status_t airlock_workspace_diagnose(const char *log_path,
                                          char *buffer, size_t cap);

/* Write a sanitized support bundle. */
airlock_status_t airlock_workspace_support(const char *root, const char *name,
                                         const char *out_path);

/* Human-readable permission dashboard. */
void airlock_workspace_permissions_text(uint32_t permissions,
                                       char *buf, size_t cap);

airlock_status_t airlock_workspace_safety_report(const char *root,
                                               const char *name,
                                               char *buf, size_t cap);

/* ---- Device / platform report --------------------------------------------- */

airlock_status_t airlock_device_report(char *buf, size_t cap);

/* ---- Shader cache ---------------------------------------------------------- */

uint64_t airlock_workspace_shader_size(const char *root, const char *name);
airlock_status_t airlock_workspace_shader_clear(const char *root,
                                              const char *name);

#endif /* AIRLOCK_WORKSPACE_H */
