/*
 * compat.c — the Application Compatibility Analyzer.
 *
 * Given a loaded PE, it classifies each import into a subsystem, scores each
 * subsystem against Airlock's registered API database, produces a per-category
 * percentage, records unresolvable imports as structured missing-API
 * diagnostics, and recommends a configuration. This is the engine behind
 * `airlock analyze game.exe`.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/compat.h"
#include "airlock/loader.h"
#include "airlock/win32.h"

/* ---- Category metadata ---------------------------------------------------- */

const char *airlock_compat_category_name(airlock_compat_category_t c)
{
    switch (c) {
    case AIRLOCK_CAT_GRAPHICS:   return "Graphics";
    case AIRLOCK_CAT_AUDIO:      return "Audio";
    case AIRLOCK_CAT_INPUT:      return "Input";
    case AIRLOCK_CAT_NETWORKING: return "Networking";
    case AIRLOCK_CAT_FILESYSTEM: return "Filesystem";
    case AIRLOCK_CAT_THREADING:  return "Threading";
    case AIRLOCK_CAT_SYSTEM:     return "Windows APIs";
    default:                    return "?";
    }
}

/* ---- DLL-prefix classification tables ------------------------------------- */

static const char *const k_gfx_dlls[] = {
    "d3d12", "d3d11", "d3d10", "d3d9", "d3d8", "d3dx", "dxgi", "ddraw",
    "opengl32", "glu32", "vulkan", "gdi32", "dwmapi", "d3dcompiler",
    "dwrite", "d2d1", NULL
};
static const char *const k_audio_dlls[] = {
    "winmm", "dsound", "xaudio2", "mmdevapi", "avrt", "msacm32", NULL
};
static const char *const k_input_dlls[] = {
    "dinput8", "dinput", "xinput", "wininput", NULL
};
static const char *const k_net_dlls[] = {
    "ws2_32", "wsock32", "wininet", "winhttp", "iphlpapi", "dnsapi",
    "secur32", "schannel", "credui", NULL
};
static const char *const k_sys_dlls[] = {
    "user32", "ntdll", "comctl32", "comdlg32", "ole32", "oleaut32",
    "uxtheme", "imm32", "setupapi", "crypt32", "bcrypt", "winspool",
    "propsys", "shcore", "advapi32", "shell32", "shlwapi", "version",
    "combase", "windows.storage", NULL
};
/* DLLs whose individual functions are reclassified to filesystem/threading. */
static const char *const k_fs_thread_dlls[] = {
    "kernel32", "kernelbase", "msvcrt", "ucrtbase", "api-ms-win-crt-",
    "api-ms-win-core-file-", "api-ms-win-core-threadpool-",
    "api-ms-win-core-processthreads-", NULL
};

static int prefix_match(const char *dll, const char *const *table)
{
    size_t i;
    /* compare basename (case-insensitive) against each prefix */
    for (i = 0; table[i]; i++) {
        const char *pre = table[i];
        size_t plen = strlen(pre);
        if (strncasecmp(dll, pre, plen) == 0)
            return 1;
    }
    return 0;
}

static int is_kernel_family(const char *dll)
{
    return prefix_match(dll, k_fs_thread_dlls);
}

static int is_thread_api(const char *fn)
{
    return strstr(fn, "Thread") || strstr(fn, "CriticalSection") ||
           strstr(fn, "Semaphore") || strstr(fn, "WaitFor") ||
           strstr(fn, "Mutex") || strstr(fn, "GetCurrent") ||
           strstr(fn, "CreateProcess") || strstr(fn, "Sleep") ||
           strstr(fn, "_beginthreadex") || strstr(fn, "EnterCritical");
}

static int is_file_api(const char *fn)
{
    return strstr(fn, "File") || strstr(fn, "MoveFile") ||
           strstr(fn, "DeleteFile") || strstr(fn, "FindFirst") ||
           strstr(fn, "GetFileAttributes") || strstr(fn, "GetTempPath") ||
           strstr(fn, "GetCurrentDirectory") || strstr(fn, "SetCurrentDirectory") ||
           strstr(fn, "GetFullPath") || strstr(fn, "GetDriveType") ||
           strstr(fn, "ReadFile") || strstr(fn, "WriteFile");
}

/* Classify a single import. Returns AIRLOCK_CAT_COUNT to skip (app dll). */
static airlock_compat_category_t classify(const char *dll, const char *fn)
{
    if (prefix_match(dll, k_gfx_dlls))    return AIRLOCK_CAT_GRAPHICS;
    if (prefix_match(dll, k_audio_dlls))  return AIRLOCK_CAT_AUDIO;
    if (prefix_match(dll, k_input_dlls))  return AIRLOCK_CAT_INPUT;
    if (prefix_match(dll, k_net_dlls))    return AIRLOCK_CAT_NETWORKING;

    if (is_kernel_family(dll)) {
        if (fn) {
            if (is_thread_api(fn)) return AIRLOCK_CAT_THREADING;
            if (is_file_api(fn))   return AIRLOCK_CAT_FILESYSTEM;
        }
        return AIRLOCK_CAT_SYSTEM;
    }
    if (prefix_match(dll, k_sys_dlls))    return AIRLOCK_CAT_SYSTEM;
    return AIRLOCK_CAT_COUNT; /* third-party app DLL — not scored */
}

/* ---- Detected-technology heuristics --------------------------------------- */

static void detect_tech(airlock_analysis_t *a, const char *dll)
{
    int d3d12 = 0, d3d11 = 0, d3d9 = 0, opengl = 0, vulkan = 0;
    if (strncasecmp(dll, "d3d12", 5) == 0) d3d12 = 1;
    else if (strncasecmp(dll, "d3d11", 5) == 0) d3d11 = 1;
    else if (strncasecmp(dll, "d3d9", 4) == 0)  d3d9 = 1;
    else if (strncasecmp(dll, "opengl32", 8) == 0) opengl = 1;
    else if (strncasecmp(dll, "vulkan", 6) == 0)  vulkan = 1;

    if (d3d12) strcpy(a->detected_graphics, "Direct3D 12");
    else if (d3d11) strcpy(a->detected_graphics, "Direct3D 11");
    else if (d3d9)  strcpy(a->detected_graphics, "Direct3D 9");
    else if (opengl) strcpy(a->detected_graphics, "OpenGL");
    else if (vulkan) strcpy(a->detected_graphics, "Vulkan");

    if (strncasecmp(dll, "xaudio2", 7) == 0 || strncasecmp(dll, "dsound", 6) == 0)
        strcpy(a->detected_audio, "XAudio2 / DirectSound");
    else if (strncasecmp(dll, "mmdevapi", 8) == 0)
        strcpy(a->detected_audio, "WASAPI");
    else if (strncasecmp(dll, "winmm", 5) == 0)
        if (!a->detected_audio[0]) strcpy(a->detected_audio, "WINMM");

    if (strncasecmp(dll, "xinput", 6) == 0)
        strcpy(a->detected_input, "XInput");
    else if (strncasecmp(dll, "dinput8", 7) == 0)
        strcpy(a->detected_input, "DirectInput");

    if (strncasecmp(dll, "ws2_32", 6) == 0)
        strcpy(a->detected_networking, "Winsock");
    else if (strncasecmp(dll, "wininet", 7) == 0)
        strcpy(a->detected_networking, "WinINet");
    else if (strncasecmp(dll, "winhttp", 7) == 0)
        strcpy(a->detected_networking, "WinHTTP");
}

/* ---- Issue helpers -------------------------------------------------------- */

static void add_issue(airlock_analysis_t *a, airlock_issue_level_t lv,
                      const char *fmt, ...)
{
    char *dst;
    if (a->issue_count >= AIRLOCK_MAX_ISSUES)
        return;
    a->issues[a->issue_count].level = lv;
    dst = a->issues[a->issue_count].text;
    {
        va_list ap;
        va_start(ap, fmt);
        vsnprintf(dst, sizeof a->issues[0].text, fmt, ap);
        va_end(ap);
    }
    a->issue_count++;
}

static void add_missing(airlock_analysis_t *a, const char *mod, const char *fn,
                        uint16_t ordinal, const char *called_by)
{
    airlock_missing_api_t *m;
    char fname[128];
    if (a->missing_count >= AIRLOCK_MAX_MISSING)
        return;
    if (fn)
        snprintf(fname, sizeof fname, "%s", fn);
    else
        snprintf(fname, sizeof fname, "#%u", ordinal);

    m = &a->missing[a->missing_count++];
    snprintf(m->module, sizeof m->module, "%s", mod);
    snprintf(m->function, sizeof m->function, "%s", fname);
    snprintf(m->called_by, sizeof m->called_by, "%s", called_by);
    snprintf(m->recommendation, sizeof m->recommendation,
             "implement %s!%s (register via airlock_win32_register_module)",
             mod, fname);
}

/* ---- Public: the analyzer ------------------------------------------------- */

airlock_status_t airlock_compat_analyze(const airlock_image_t *img,
                                      const char *called_by,
                                      airlock_analysis_t *out)
{
    airlock_analysis_t a;
    size_t i;

    if (!img || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(&a, 0, sizeof a);

    snprintf(a.called_by, sizeof a.called_by, "%s",
             called_by ? called_by : "");
    a.is_64bit = (img->opt.magic == AIRLOCK_PE_MAGIC_PE32_PLUS);
    a.valid_pe = 1;
    strcpy(a.gfx_recommendation, "Software");
    strcpy(a.audio_recommendation, "ALSA");
    strcpy(a.input_recommendation, "XInput backend");

    /* Score each category. */
    {
        uint32_t totals[AIRLOCK_CAT_COUNT] = {0};
        uint32_t supported[AIRLOCK_CAT_COUNT] = {0};
        uint32_t scored_total = 0, scored_supported = 0;

        for (i = 0; i < img->import_count; i++) {
            const airlock_import_t *im = &img->imports[i];
            airlock_compat_category_t cat = classify(im->module, im->name);
            int has;
            if (cat >= AIRLOCK_CAT_COUNT)
                continue; /* third-party app DLL */
            totals[cat]++;
            has = im->name ? airlock_win32_export_exists(im->module, im->name)
                           : 0;
            if (has)
                supported[cat]++;
            else if (im->name)
                add_missing(&a, im->module, im->name, 0, a.called_by);
            else
                add_missing(&a, im->module, NULL, im->ordinal, a.called_by);
            detect_tech(&a, im->module);
        }

        for (i = 0; i < AIRLOCK_CAT_COUNT; i++) {
            a.scores[i].category = (airlock_compat_category_t)i;
            a.scores[i].total_imports = totals[i];
            a.scores[i].supported_imports = supported[i];
            a.scores[i].percent =
                totals[i] ? (int)(supported[i] * 100u / totals[i]) : -1;
            scored_total += totals[i];
            scored_supported += supported[i];
        }
        a.overall_percent = scored_total
            ? (int)(scored_supported * 100u / scored_total) : 100;
    }

    /* ---- Potential issues ------------------------------------------------ */
    if (a.scores[AIRLOCK_CAT_GRAPHICS].total_imports) {
        a.shader_cache_enabled = 1;
        strcpy(a.gfx_recommendation,
               strncasecmp(a.detected_graphics, "Direct3D", 8) == 0
                   ? "Vulkan" : "OpenGL");
        if (a.scores[AIRLOCK_CAT_GRAPHICS].percent < 100)
            add_issue(&a, AIRLOCK_ISSUE_WARN,
                      "%s graphics coverage is partial (%d%%)",
                      a.detected_graphics[0] ? a.detected_graphics : "Graphics",
                      a.scores[AIRLOCK_CAT_GRAPHICS].percent);
        if (strstr(a.detected_graphics, "12"))
            add_issue(&a, AIRLOCK_ISSUE_WARN,
                      "D3D12 feature-level requirement — Vulkan fallback recommended");
        if (strstr(a.detected_graphics, "11"))
            add_issue(&a, AIRLOCK_ISSUE_INFO,
                      "D3D11 feature-level requirement");
    }

    if (a.scores[AIRLOCK_CAT_AUDIO].total_imports &&
        a.scores[AIRLOCK_CAT_AUDIO].percent < 100)
        strcpy(a.audio_recommendation, "PipeWire");

    /* High-resolution timer usage. */
    for (i = 0; i < img->import_count; i++) {
        const char *fn = img->imports[i].name;
        if (!fn) continue;
        if (strcmp(fn, "QueryPerformanceCounter") == 0 ||
            strcmp(fn, "QueryPerformanceFrequency") == 0 ||
            strcmp(fn, "timeGetTime") == 0 ||
            strcmp(fn, "GetSystemTimePreciseAsFileTime") == 0) {
            add_issue(&a, AIRLOCK_ISSUE_WARN,
                      "High-resolution timer usage (%.*s)",
                      (int)40, fn);
            break;
        }
    }

    /* Advanced synchronization usage. */
    {
        uint32_t thread_imports = 0;
        for (i = 0; i < img->import_count; i++) {
            const char *fn = img->imports[i].name;
            if (fn && is_thread_api(fn))
                thread_imports++;
        }
        if (thread_imports > 4)
            add_issue(&a, AIRLOCK_ISSUE_WARN,
                      "Advanced synchronization usage (%u thread/threadpool APIs)",
                      thread_imports);
    }

    if (a.missing_count)
        add_issue(&a, AIRLOCK_ISSUE_ERROR,
                  "%u required API(s) not implemented by Airlock",
                  (unsigned)a.missing_count);

    /* Ordinal imports are inherently unresolvable by name. */
    for (i = 0; i < img->import_count; i++) {
        if (!img->imports[i].name) {
            add_issue(&a, AIRLOCK_ISSUE_INFO,
                      "Ordinal import (%s!#%u) needs ordinal binding",
                      img->imports[i].module, img->imports[i].ordinal);
            break;
        }
    }

    *out = a;
    return AIRLOCK_OK;
}

/* ---- Report renderer ------------------------------------------------------ */

static void bar(char *buf, size_t n, int percent)
{
    int filled = (percent * 20 + 50) / 100; /* 20 cells, rounded */
    int k = 0;
    if (n < 21) return;
    for (int i = 0; i < 20; i++)
        buf[k++] = (i < filled) ? '#' : ' ';
    buf[k] = '\0';
}

void airlock_compat_report(const airlock_analysis_t *a)
{
    int i;
    char b[24];
    if (!a)
        return;

    printf("============================================\n");
    printf("   COMPATIBILITY ANALYSIS — %s\n", a->called_by);
    printf("============================================\n");
    printf("Architecture:        %s\n", a->is_64bit ? "x64" : "x86");
    printf("PE format:           %s\n", a->valid_pe ? "Valid" : "Invalid");
    printf("Graphics:            %s\n", a->detected_graphics[0] ? a->detected_graphics : "(none)");
    printf("Audio:               %s\n", a->detected_audio[0] ? a->detected_audio : "(none)");
    printf("Input:               %s\n", a->detected_input[0] ? a->detected_input : "(none)");
    printf("Networking:          %s\n", a->detected_networking[0] ? a->detected_networking : "(none)");

    printf("\nWindows APIs\n");
    for (i = 0; i < AIRLOCK_CAT_COUNT; i++) {
        const airlock_compat_score_t *s = &a->scores[i];
        if (s->percent < 0) {
            printf("%-16s N/A\n", airlock_compat_category_name(s->category));
            continue;
        }
        bar(b, sizeof b, s->percent);
        printf("%-16s [%s] %3d%%  (%u/%u)\n",
               airlock_compat_category_name(s->category), b, s->percent,
               s->supported_imports, s->total_imports);
    }
    printf("Overall             %3d%%\n", a->overall_percent);

    if (a->issue_count) {
        printf("\nPotential issues\n");
        printf("-----------------\n");
        for (i = 0; i < (int)a->issue_count; i++) {
            const char *sym = a->issues[i].level == AIRLOCK_ISSUE_ERROR ? "X" :
                              a->issues[i].level == AIRLOCK_ISSUE_WARN  ? "!" : "-";
            printf(" %s %s\n", sym, a->issues[i].text);
        }
    }

    if (a->missing_count) {
        printf("\nCompatibility failures (missing APIs)\n");
        printf("-------------------------------------\n");
        for (i = 0; i < (int)a->missing_count && i < 8; i++) {
            const airlock_missing_api_t *m = &a->missing[i];
            printf("  Module:       %s\n", m->module);
            printf("  Missing API:  %s\n", m->function);
            printf("  Called by:    %s\n", m->called_by);
            printf("  Recommenation:%s\n", m->recommendation);
            printf("  ---\n");
        }
        if (a->missing_count > 8)
            printf("  ... and %u more\n", (unsigned)(a->missing_count - 8));
    }

    printf("\nRecommended configuration\n");
    printf("-------------------------\n");
    printf("Graphics: %s\n", a->gfx_recommendation);
    printf("Audio:    %s\n", a->audio_recommendation);
    printf("Input:    %s\n", a->input_recommendation);
    printf("Shader cache: %s\n", a->shader_cache_enabled ? "ENABLED" : "disabled");
    printf("\n");
}
