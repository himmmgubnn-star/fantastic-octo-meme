/*
 * cli.c — Cellar command-line driver.
 *
 *   cellar [--list-modules] program.exe [args...]
 *
 * In this milestone the CLI loads and inspects a Windows executable and prints
 * a report of what Cellar found: format, architecture, subsystem, imports and
 * exports. Executing the binary (the ABI emulation layer) is the next step on
 * the roadmap.
 *
 * SPDX-License-Identifier: MIT
 */
/* Expose setenv under strict feature-test defaults. */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/audio.h"
#include "cellar/compat.h"
#include "cellar/crash.h"
#include "cellar/loader.h"
#include "cellar/pe.h"
#include "cellar/perf.h"
#include "cellar/plugin.h"
#include "cellar/platform.h"
#include "cellar/trace.h"
#include "cellar/win32.h"

static const char *machine_name(uint16_t m)
{
    switch (m) {
    case 0x014c: return "x86";
    case 0x8664: return "x86-64";
    case 0x01c0: return "ARM";
    case 0xaa64: return "ARM64";
    default:     return "unknown";
    }
}

static const char *subsystem_name(uint16_t s)
{
    switch (s) {
    case CELLAR_PE_SUBSYSTEM_NATIVE:     return "native";
    case CELLAR_PE_SUBSYSTEM_WINDOWS_GUI:return "windows-gui";
    case CELLAR_PE_SUBSYSTEM_WINDOWS_CUI:return "windows-console";
    default:                             return "other";
    }
}

static const char *architecture_word(const cellar_image_t *img)
{
    return img->opt.magic == CELLAR_PE_MAGIC_PE32 ? "32-bit" : "64-bit";
}

/* ---- Perf report ---------------------------------------------------------- */

static void dump_perf(void)
{
    const cellar_perf_counters_t *c = cellar_perf_counters();
    const cellar_perf_options_t *opt = cellar_perf_options();
    cellar_perf_sample_t ring[64];
    size_t n, i;

    printf("== performance ==\n");
    printf("  images loaded      %llu\n", (unsigned long long)c->images_loaded);
    printf("  imports resolved   %llu\n", (unsigned long long)c->imports_resolved);
    printf("  bytes mapped       %llu\n", (unsigned long long)c->map_bytes);
    printf("  mmap file reads    %llu\n", (unsigned long long)c->mmap_reads);
    printf("  audio bytes        %llu\n", (unsigned long long)c->audio_bytes);
    printf("  prefault calls     %llu\n", (unsigned long long)c->prefault_calls);

    printf("  options: papi=%d mmap_threshold=%d large_pages=%d\n",
           opt->papi, opt->mmap_threshold, opt->large_pages);

    n = cellar_perf_ring_drain(ring, 64);
    if (n) {
        printf("  trace:\n");
        for (i = 0; i < n; i++)
            printf("    t=%llums %-24s %llu\n",
                   (unsigned long long)ring[i].t_ms, ring[i].label,
                   (unsigned long long)ring[i].value);
    }
}

static int dump_audio_sink(const char *path)
{
    const cellar_audio_backend_t *backend = cellar_audio_default_backend();
    cellar_audio_device_t dev;
    cellar_audio_format_t fmt;
    uint8_t buf[4096];
    size_t i;
    cellar_status_t st;

    memset(&fmt, 0, sizeof fmt);
    fmt.format_tag = 1;
    fmt.channels = 2;
    fmt.sample_rate = 44100;
    fmt.bits_per_sample = 16;
    fmt.block_align = (uint16_t)(2 * fmt.channels);
    fmt.avg_bytes_per_sec = fmt.sample_rate * fmt.block_align;

    /* Route the WAV sink to the requested output path (default is fine if
     * no path is given and CELLAR_WAV_OUT is unset). */
    if (path && *path)
        setenv("CELLAR_WAV_OUT", path, 1);

    st = cellar_audio_open(&dev, backend, &fmt);
    if (st != CELLAR_OK) {
        fprintf(stderr, "cellar: audio open failed (%s)\n",
                cellar_status_string(st));
        return 1;
    }

    printf("== audio ==\n");
    printf("  backend:  %s\n", backend->name);
    printf("  format:   %u Hz, %u ch, %u-bit PCM\n",
           fmt.sample_rate, fmt.channels, fmt.bits_per_sample);
    printf("  output:   %s\n", path);

    /* Generate a short 440 Hz tone so the WAV sink yields a real, audible
     * file, and the ALSA backend produces sound. */
    {
        int16_t *samples = (int16_t *)buf;
        size_t nsamples = sizeof buf / sizeof(int16_t);
        for (i = 0; i < nsamples; i += 2) {
            double t = (double)i / (double)(fmt.sample_rate * 2);
            double ph = t * 440.0 * 2.0 * 3.141592653589793;
            int16_t s = (int16_t)(12000.0 * ((ph - (int)ph) > 0.5 ? 1 : -1));
            samples[i] = s;     /* left  */
            samples[i + 1] = s; /* right */
        }
        for (i = 0; i < 8; i++)   /* ~0.75 s at 44100 Hz, mono-ish buffer */
            cellar_audio_write(&dev, buf, sizeof buf);
    }

    cellar_audio_close(&dev);
    return 0;
}

static int dump_image(const char *path)
{
    cellar_status_t st;
    cellar_image_t img;
    size_t i;

    st = cellar_image_load_file(path, CELLAR_LOAD_DEFAULT |
                                      CELLAR_LOAD_PARSE_EXPORTS |
                                      CELLAR_LOAD_PARSE_RELOCS, &img);
    if (st != CELLAR_OK) {
        fprintf(stderr, "cellar: cannot load '%s': %s\n",
                path, cellar_status_string(st));
        return 1;
    }

    printf("== %s ==\n", path);
    printf("  format         PE/%s  (%s)\n",
           architecture_word(&img), machine_name(img.coff.machine));
    printf("  kind           %s\n", img.is_dll ? "DLL" : "executable");
    printf("  subsystem      %s\n", subsystem_name(img.opt.subsystem));
    printf("  entry point    RVA 0x%08X\n", img.opt.address_of_entry_point);
    printf("  image base     0x%08llX\n",
           (unsigned long long)img.opt.image_base);
    printf("  image size     0x%08X (%u bytes)\n",
           img.opt.size_of_image, img.opt.size_of_image);
    printf("  sections       %zu\n", img.section_count);
    printf("  imports        %zu\n", img.import_count);
    printf("  exports        %zu\n", img.export_name_count);

    if (img.import_count) {
        printf("\n  imports:\n");
        for (i = 0; i < img.import_count; i++) {
            const cellar_import_t *im = &img.imports[i];
            if (im->name)
                printf("    %-16s %s%s\n", im->module, im->name,
                       im->resolved ? "" : "   [UNRESOLVED]");
            else
                printf("    %-16s #%u\n", im->module, im->ordinal);
        }
    }

    if (img.export_name_count) {
        printf("\n  exports:\n");
        for (i = 0; i < img.export_name_count; i++)
            printf("    %s\n", img.export_names[i]);
    }

    cellar_image_unload(&img);
    return 0;
}

static int cmd_analyze(const char *path)
{
    cellar_status_t st;
    cellar_image_t img;
    cellar_analysis_t analysis;
    cellar_app_profile_t prof;
    const char *base;

    st = cellar_image_load_file(path, CELLAR_LOAD_DEFAULT, &img);
    if (st != CELLAR_OK) {
        fprintf(stderr, "cellar: cannot load '%s': %s\n",
                path, cellar_status_string(st));
        return 1;
    }

    base = strrchr(path, '/');
    base = base ? base + 1 : path;

    st = cellar_compat_analyze(&img, base, &analysis);
    if (st != CELLAR_OK) {
        fprintf(stderr, "cellar: analysis failed: %s\n",
                cellar_status_string(st));
        cellar_image_unload(&img);
        return 1;
    }

    cellar_compat_report(&analysis);

    /* Remember a per-application profile with the recommended config. */
    memset(&prof, 0, sizeof prof);
    snprintf(prof.app_name, sizeof prof.app_name, "%s", base);
    prof.version_mode = CELLAR_WIN_10;
    snprintf(prof.gfx_backend, sizeof prof.gfx_backend, "%s",
             analysis.gfx_recommendation);
    snprintf(prof.audio_backend, sizeof prof.audio_backend, "%s",
             analysis.audio_recommendation);
    prof.shader_cache_enabled = analysis.shader_cache_enabled;
    cellar_profile_save(cellar_prefix_dir(), &prof);

    cellar_image_unload(&img);
    return 0;
}

static void dump_platform(void)
{
    printf("== platform ==\n");
    printf("  pid              %u\n", cellar_getpid());
    printf("  tid              %u\n", cellar_gettid());
    printf("  monotonic ms     %llu\n",
           (unsigned long long)cellar_monotonic_ms());
    printf("  perf freq        %llu counts/sec\n",
           (unsigned long long)cellar_perf_frequency());
    printf("  high-performance hint: ");
    if (cellar_perf_hint_high_performance() == CELLAR_OK)
        printf("applied\n");
    else
        printf("not supported here\n");
}

static void usage(FILE *out)
{
    fprintf(out,
        "cellar %s — a Windows compatibility layer for Linux & Android\n"
        "\n"
        "usage:\n"
        "  cellar analyze game.exe       application compatibility analysis\n"
        "  cellar --list-modules         list registered Win32 modules\n"
        "  cellar --perf                 show perf counters / tracing / options\n"
        "  cellar --platform             show OS + perf-hint info\n"
        "  cellar --audio [out.wav]      render a tone through the audio backend\n"
        "  cellar --trace=api,dll prog   enable dynamic tracing categories\n"
        "  cellar --papi=1 program.exe   enable page population for the load\n"
        "  cellar program.exe [args...]  load and inspect a Windows executable\n"
        "\n"
        "Execution of loaded binaries is not yet implemented; this build parses\n"
        "and reports the PE structure, exercises audio, and collects perf data.\n"
        "\n"
        "Options:\n"
        "  --papi=0|1        populate pages in new mappings (faster steady-state)\n"
        "  --trace=list      tracing categories: graphics,filesystem,threading,\n"
        "                    dll,api,audio,timer,compat,all (also via $CELLAR_TRACE)\n"
        "  CELLAR_WAV_OUT=path  set the audio WAV sink output file\n"
        "  CELLAR_PREFIX=dir    set the compatibility-prefix directory\n",
        CELLAR_VERSION_STRING);
}

int main(int argc, char **argv)
{
    int i;
    int want_perf = 0;

    cellar_win32_init();
    cellar_trace_init_from_env();
    cellar_backend_init();

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    }

    if (strcmp(argv[1], "analyze") == 0) {
        if (argc < 3) {
            fprintf(stderr, "cellar: analyze needs a path to a .exe\n");
            return 1;
        }
        return cmd_analyze(argv[2]);
    }

    if (strcmp(argv[1], "--list-modules") == 0) {
        cellar_win32_dump_registry();
        return 0;
    }

    if (strcmp(argv[1], "--platform") == 0) {
        dump_platform();
        return 0;
    }

    if (strcmp(argv[1], "--audio") == 0) {
        const char *out = argc > 2 ? argv[2] : "cellar-out.wav";
        return dump_audio_sink(out);
    }

    /* Parse options and flags (--papi=1, --perf) before the program list. */
    for (i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--perf") == 0) {
            want_perf = 1;
            continue;
        }
        if (strncmp(argv[i], "--papi=", 7) == 0) {
            cellar_perf_options_t opt = *cellar_perf_options();
            opt.papi = atoi(argv[i] + 7) ? 1 : 0;
            cellar_perf_set_options(&opt);
            continue;
        }
        if (strncmp(argv[i], "--trace=", 8) == 0) {
            cellar_trace_enable(cellar_trace_parse(argv[i] + 8));
            continue;
        }
        break; /* first non-option argument starts the program list */
    }

    if (i >= argc) {
        /* No program given. */
        if (want_perf) {
            dump_perf();
            return 0;
        }
        usage(stderr);
        return 1;
    }

    for (; i < argc; i++) {
        if (dump_image(argv[i]) != 0)
            return 1;
    }

    /* Report cumulative counters when --perf was requested. */
    if (want_perf)
        dump_perf();

    return 0;
}
