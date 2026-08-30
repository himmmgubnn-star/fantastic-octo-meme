/*
 * compat.h — application compatibility analysis, profiles, and version
 * behavior modes.
 *
 * The flagship piece of Airlock: before (or after) loading a Windows binary,
 * `airlock_compat_analyze()` inspects its imports, classifies them into
 * subsystems, scores each against the registered Win32 API database, and
 * reports concrete problems and recommendations. Per-application profiles
 * remember a working configuration across runs, and Windows-version behavior
 * modes reproduce behavioral differences (not just the reported version
 * number).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_COMPAT_H
#define AIRLOCK_COMPAT_H

#include <stdint.h>

#include "airlock.h"
#include "loader.h"

/* ---- Subsystem categories used for scoring ------------------------------- */
typedef enum airlock_compat_category {
    AIRLOCK_CAT_GRAPHICS = 0,
    AIRLOCK_CAT_AUDIO,
    AIRLOCK_CAT_INPUT,
    AIRLOCK_CAT_NETWORKING,
    AIRLOCK_CAT_FILESYSTEM,
    AIRLOCK_CAT_THREADING,
    AIRLOCK_CAT_SYSTEM,
    AIRLOCK_CAT_COUNT
} airlock_compat_category_t;

const char *airlock_compat_category_name(airlock_compat_category_t c);

/* ---- Issues & missing-API diagnostics ------------------------------------ */

typedef enum airlock_issue_level {
    AIRLOCK_ISSUE_INFO = 0,
    AIRLOCK_ISSUE_WARN,
    AIRLOCK_ISSUE_ERROR,
} airlock_issue_level_t;

#define AIRLOCK_MAX_ISSUES   16
#define AIRLOCK_MAX_MISSING  64

typedef struct airlock_compat_issue {
    airlock_issue_level_t level;
    char text[128];
} airlock_compat_issue_t;

/* A specific unresolvable import: the diagnosis behind "Compatibility
 * failure: module: example.dll, missing API: user32!ExampleFunction, called
 * by: game.exe". */
typedef struct airlock_missing_api {
    char module[64];
    char function[128];
    char called_by[256];
    char recommendation[192];
} airlock_missing_api_t;

/* ---- Scores --------------------------------------------------------------- */

typedef struct airlock_compat_score {
    airlock_compat_category_t category;
    uint32_t total_imports;     /* imports in this category                 */
    uint32_t supported_imports; /* ...that Airlock can satisfy               */
    int      percent;           /* 0..100, or -1 when N/A (no imports)      */
} airlock_compat_score_t;

/* ---- Windows version behavior modes -------------------------------------- */

typedef enum airlock_version_mode {
    AIRLOCK_WIN_7 = 0,
    AIRLOCK_WIN_81,
    AIRLOCK_WIN_10,
    AIRLOCK_WIN_11,
} airlock_version_mode_t;

/* Behavioral profile for a Windows version. This drives real behavioral
 * differences the compatibility layer reproduces — not just the number
 * GetVersionEx reports. */
typedef struct airlock_version_profile {
    airlock_version_mode_t mode;
    const char *name;
    uint16_t major;           /* e.g. 6/6/10/10 */
    uint16_t minor;           /* 1/3/0/0        */
    uint16_t build;           /* 7601/9600/19045/22631 */
    uint16_t sp_major;        /* service pack */
    uint16_t sp_minor;
    uint32_t product_type;    /* VER_NT_WORKSTATION (1) or SERVER (3) */
    /* Behavioral flags (concrete differences Airlock acts on): */
    int high_dpi_aware_by_default; /* Win7: no; Win10+: yes (PerMonitorV2)  */
    int prefer_utf8_codepage;      /* Win10 1903+: manifest-driven UTF-8    */
    int touch_input_available;     /* Win8.1+: touch/touchpad input         */
    int modern_threadpool;         /* Win8+: TpAllocWork-style threadpool   */
    int arm_translation;           /* Win11: x64/ARM64 translation          */
} airlock_version_profile_t;

/* Return the behavioral profile for a mode (static, never NULL). */
const airlock_version_profile_t *airlock_version_profile(airlock_version_mode_t m);

/* ---- Per-application profiles -------------------------------------------- */

/* Remembered, per-application configuration ("this game worked with this
 * setup"). Persisted under a prefix directory. */
typedef struct airlock_app_profile {
    char app_name[128];           /* basename of the exe, e.g. "game.exe"  */
    airlock_version_mode_t version_mode;
    char gfx_backend[32];         /* "Vulkan" / "OpenGL" / "Software"      */
    char audio_backend[32];       /* "WAV" / "ALSA" / "PipeWire" / "Null"  */
    char dll_overrides[512];      /* e.g. "d3d9=native,winmm=bundled"      */
    int  low_latency_sync;        /* 1 = prefer low-latency sync prims     */
    int  shader_cache_enabled;
    int  last_good;               /* 1 = this config produced a working run */
} airlock_app_profile_t;

/* Default prefix directory ($AIRLOCK_PREFIX, else ~/.airlock/prefixes). */
const char *airlock_prefix_dir(void);

/* Persist a profile to <prefix>/<app>/profile.conf. */
airlock_status_t airlock_profile_save(const char *prefix_dir,
                                    const airlock_app_profile_t *p);

/* Load a profile from <prefix>/<app>/profile.conf; returns AIRLOCK_OK and
 * fills `out` when found. */
airlock_status_t airlock_profile_load(const char *prefix_dir,
                                    const char *app_name,
                                    airlock_app_profile_t *out);

/* Remember a working configuration: writes a profile and marks last_good=1. */
airlock_status_t airlock_profile_mark_last_good(const char *prefix_dir,
                                              const airlock_app_profile_t *p);

/* ---- The analyzer --------------------------------------------------------- */

typedef struct airlock_analysis {
    char  called_by[256];        /* exe/dll responsible for the imports     */
    int   is_64bit;
    int   valid_pe;

    airlock_compat_score_t scores[AIRLOCK_CAT_COUNT];

    airlock_compat_issue_t issues[AIRLOCK_MAX_ISSUES];
    size_t issue_count;

    airlock_missing_api_t missing[AIRLOCK_MAX_MISSING];
    size_t missing_count;

    /* Detected technology per subsystem (longest-match DLL wins). */
    char detected_graphics[32];
    char detected_audio[32];
    char detected_input[32];
    char detected_networking[32];

    /* Recommended configuration. */
    char gfx_recommendation[32];
    char audio_recommendation[32];
    char input_recommendation[32];
    int  shader_cache_enabled;

    int overall_percent;         /* weighted average of scored categories  */
} airlock_analysis_t;

/* Analyze a loaded image against Airlock's API database. `called_by` is the
 * exe/dll name reported in missing-API diagnostics (may be NULL -> ""). */
airlock_status_t airlock_compat_analyze(const airlock_image_t *img,
                                      const char *called_by,
                                      airlock_analysis_t *out);

/* Render the analysis as the boxed "COMPATIBILITY ANALYSIS" report. */
void airlock_compat_report(const airlock_analysis_t *a);

#endif /* AIRLOCK_COMPAT_H */
