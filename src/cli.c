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
#include "cellar/db.h"
#include "cellar/debug.h"
#include "cellar/inspect.h"
#include "cellar/loader.h"
#include "cellar/pe.h"
#include "cellar/perf.h"
#include "cellar/plugin.h"
#include "cellar/platform.h"
#include "cellar/prefix.h"
#include "cellar/runtime.h"
#include "cellar/testlab.h"
#include "cellar/trace.h"
#include "cellar/win32.h"

static int remember_in_db(const cellar_inspect_t *ins, const cellar_analysis_t *a);
static int cmd_inspect(const char *path);

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

    {
        cellar_inspect_t ins;
        if (cellar_inspect_image(&img, path, &ins) == CELLAR_OK)
            remember_in_db(&ins, &analysis);
    }

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

static int remember_in_db(const cellar_inspect_t *ins, const cellar_analysis_t *a)
{
    cellar_db_t db;
    cellar_db_entry_t e;
    char path[1024];
    cellar_db_default_path(path, sizeof path);
    if (cellar_db_open(&db, path) != CELLAR_OK)
        return 0;
    cellar_db_from_analysis(&e, ins, a);
    cellar_db_put(&db, &e);
    cellar_db_save(&db);
    cellar_db_close(&db);
    return 0;
}

static int cmd_inspect(const char *path)
{
    cellar_status_t st;
    cellar_image_t img;
    cellar_inspect_t ins;
    cellar_analysis_t a;

    st = cellar_image_load_file(path, CELLAR_LOAD_DEFAULT |
                                      CELLAR_LOAD_PARSE_EXPORTS |
                                      CELLAR_LOAD_PARSE_RELOCS, &img);
    if (st != CELLAR_OK) {
        fprintf(stderr, "cellar: cannot load '%s': %s\n",
                path, cellar_status_string(st));
        return 1;
    }
    st = cellar_inspect_image(&img, path, &ins);
    if (st != CELLAR_OK) {
        cellar_image_unload(&img);
        return 1;
    }
    cellar_inspect_report(&ins);
    if (cellar_compat_analyze(&img, ins.basename, &a) == CELLAR_OK)
        remember_in_db(&ins, &a);
    cellar_image_unload(&img);
    return 0;
}

static int cmd_db(int argc, char **argv)
{
    cellar_db_t db;
    char path[1024];
    const char *sub = argc > 2 ? argv[2] : "list";
    cellar_db_default_path(path, sizeof path);
    if (cellar_db_open(&db, path) != CELLAR_OK) {
        fprintf(stderr, "cellar: cannot open %s\n", path);
        return 1;
    }
    if (strcmp(sub, "list") == 0) {
        size_t i;
        printf("Compatibility database (%s) — %zu application(s)\n",
               path, cellar_db_count(&db));
        for (i = 0; i < cellar_db_count(&db); i++) {
            const cellar_db_entry_t *e = cellar_db_at(&db, i);
            printf("  %-24s  %-6s  %s\n", e->application, e->architecture,
                   cellar_rating_name(e->rating));
        }
    } else if (strcmp(sub, "show") == 0) {
        const cellar_db_entry_t *e;
        if (argc < 4) {
            fprintf(stderr, "cellar: db show needs an application name\n");
            cellar_db_close(&db);
            return 1;
        }
        e = cellar_db_find(&db, argv[3]);
        if (!e) {
            fprintf(stderr, "cellar: no entry for '%s'\n", argv[3]);
            cellar_db_close(&db);
            return 1;
        }
        cellar_db_report(e);
    } else {
        fprintf(stderr, "cellar: unknown db subcommand '%s'\n", sub);
        cellar_db_close(&db);
        return 1;
    }
    cellar_db_close(&db);
    return 0;
}

static int cmd_runtime(int argc, char **argv)
{
    char bottle[1024];
    const char *sub;
    cellar_path_join(bottle, sizeof bottle, cellar_prefix_dir(), "default");
    cellar_mkdir_p(bottle);
    cellar_runtime_init(bottle);
    sub = argc > 2 ? argv[2] : "list";
    if (strcmp(sub, "list") == 0) {
        cellar_runtime_report();
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "cellar: runtime %s needs a kind "
                        "(vcruntime|dotnet|directx|fonts|systemlibs)\n", sub);
        return 1;
    }
    {
        cellar_runtime_kind_t k = cellar_runtime_parse(argv[3]);
        if (k >= CELLAR_RT_COUNT) {
            fprintf(stderr, "cellar: unknown runtime '%s'\n", argv[3]);
            return 1;
        }
        if (strcmp(sub, "install") == 0) {
            if (cellar_runtime_install(k) != CELLAR_OK)
                return 1;
            printf("installed %s\n", cellar_runtime_kind_name(k));
            return 0;
        }
        if (strcmp(sub, "uninstall") == 0) {
            cellar_runtime_uninstall(k);
            printf("uninstalled %s\n", cellar_runtime_kind_name(k));
            return 0;
        }
    }
    fprintf(stderr, "cellar: unknown runtime subcommand '%s'\n", sub);
    return 1;
}

static int cmd_prefix(int argc, char **argv)
{
    const char *root = cellar_prefix_dir();
    const char *sub;
    if (argc < 3) {
        fprintf(stderr, "cellar: prefix needs create|list|info|delete|backup|restore|launch\n");
        return 1;
    }
    sub = argv[2];
    if (strcmp(sub, "list") == 0) {
        cellar_prefix_info_t list[64];
        size_t n = cellar_prefix_list(root, list, 64);
        size_t i;
        printf("Prefixes in %s (%zu):\n", root, n);
        for (i = 0; i < n; i++)
            printf("  %-16s  %s  gfx=%s audio=%s\n",
                   list[i].name, list[i].path, list[i].gfx, list[i].audio);
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "cellar: prefix %s needs a name\n", sub);
        return 1;
    }
    if (strcmp(sub, "create") == 0) {
        if (cellar_prefix_create(root, argv[3]) != CELLAR_OK) {
            fprintf(stderr, "cellar: failed to create prefix '%s'\n", argv[3]);
            return 1;
        }
        printf("created prefix %s/%s\n", root, argv[3]);
        return 0;
    }
    if (strcmp(sub, "delete") == 0) {
        cellar_prefix_delete(root, argv[3]);
        printf("deleted prefix %s\n", argv[3]);
        return 0;
    }
    if (strcmp(sub, "info") == 0) {
        cellar_prefix_info_t info;
        cellar_prefix_info(root, argv[3], &info);
        if (!info.exists) {
            fprintf(stderr, "cellar: prefix '%s' does not exist\n", argv[3]);
            return 1;
        }
        printf("Name:     %s\n", info.name);
        printf("Path:     %s\n", info.path);
        printf("Version:  %s\n", cellar_version_profile(info.version_mode)->name);
        printf("Graphics: %s\n", info.gfx);
        printf("Audio:    %s\n", info.audio);
        return 0;
    }
    if (strcmp(sub, "backup") == 0) {
        const char *file = argc > 4 ? argv[4] : NULL;
        if (!file) {
            fprintf(stderr, "cellar: prefix backup NAME FILE\n");
            return 1;
        }
        if (cellar_prefix_backup(root, argv[3], file) != CELLAR_OK)
            return 1;
        printf("backed up %s -> %s\n", argv[3], file);
        return 0;
    }
    if (strcmp(sub, "restore") == 0) {
        const char *file = argc > 4 ? argv[4] : NULL;
        if (!file) {
            fprintf(stderr, "cellar: prefix restore NAME FILE\n");
            return 1;
        }
        if (cellar_prefix_restore(root, argv[3], file) != CELLAR_OK)
            return 1;
        printf("restored %s from %s\n", argv[3], file);
        return 0;
    }
    if (strcmp(sub, "launch") == 0) {
        cellar_prefix_info_t info;
        const char *exe = argc > 4 ? argv[4] : NULL;
        if (!exe) {
            fprintf(stderr, "cellar: prefix launch NAME game.exe\n");
            return 1;
        }
        cellar_prefix_info(root, argv[3], &info);
        if (!info.exists) {
            fprintf(stderr, "cellar: prefix '%s' does not exist "
                            "(try: cellar prefix create %s)\n",
                    argv[3], argv[3]);
            return 1;
        }
        printf("== launching %s in prefix %s ==\n", exe, argv[3]);
        printf("  bottle:   %s\n", info.path);
        printf("  version:  %s\n", cellar_version_profile(info.version_mode)->name);
        printf("  graphics: %s\n", info.gfx);
        printf("  audio:    %s\n", info.audio);
        printf("\n");
        cmd_inspect(exe);
        printf("Execution of loaded binaries is not yet implemented "
               "(see docs/ROADMAP.md, Milestone 1).\n");
        return 0;
    }
    fprintf(stderr, "cellar: unknown prefix subcommand '%s'\n", sub);
    return 1;
}

static int cmd_test(void)
{
    cellar_lab_report_t r;
    cellar_lab_run(&r);
    cellar_lab_print(&r);
    return r.failed ? 1 : 0;
}

static int cmd_debug(const char *path)
{
    cellar_status_t st;
    cellar_image_t img;
    cellar_inspect_t ins;
    cellar_debug_snapshot_t snap;
    size_t i;
    const char *base;

    st = cellar_image_load_file(path, CELLAR_LOAD_DEFAULT, &img);
    if (st != CELLAR_OK) {
        fprintf(stderr, "cellar: cannot load '%s': %s\n",
                path, cellar_status_string(st));
        return 1;
    }
    base = strrchr(path, '/');
    base = base ? base + 1 : path;
    cellar_inspect_image(&img, path, &ins);
    cellar_debug_begin(base);
    cellar_debug_note_module();
    for (i = 0; i < ins.unique_dll_count; i++)
        cellar_debug_note_module();
    cellar_debug_note_handle();
    for (i = 0; i < img.import_count; i++) {
        if (img.imports[i].name && !img.imports[i].resolved) {
            char api[160];
            snprintf(api, sizeof api, "%s!%s",
                     img.imports[i].module, img.imports[i].name);
            cellar_debug_note_api(api, "Win32");
            break;
        }
    }
    cellar_debug_snapshot(&snap);
    cellar_debug_report(&snap);
    cellar_image_unload(&img);
    return 0;
}

static void usage(FILE *out)
{
    fprintf(out,
        "cellar %s — a Windows compatibility layer for Linux & Android\n"
        "\n"
        "usage:\n"
        "  cellar inspect game.exe       PE inspector (arch, DLLs, TLS, runtimes)\n"
        "  cellar analyze game.exe       application compatibility analysis\n"
        "  cellar db list|show APP       compatibility database\n"
        "  cellar prefix create|list|info|delete|backup|restore|launch\n"
        "  cellar runtime list|install|uninstall KIND\n"
        "  cellar test                   run the compatibility test lab\n"
        "  cellar debug game.exe         debugger snapshot of a loaded PE\n"
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

    if (strcmp(argv[1], "inspect") == 0) {
        if (argc < 3) {
            fprintf(stderr, "cellar: inspect needs a path to a .exe\n");
            return 1;
        }
        return cmd_inspect(argv[2]);
    }

    if (strcmp(argv[1], "analyze") == 0) {
        if (argc < 3) {
            fprintf(stderr, "cellar: analyze needs a path to a .exe\n");
            return 1;
        }
        return cmd_analyze(argv[2]);
    }

    if (strcmp(argv[1], "db") == 0)
        return cmd_db(argc, argv);

    if (strcmp(argv[1], "prefix") == 0)
        return cmd_prefix(argc, argv);

    if (strcmp(argv[1], "runtime") == 0)
        return cmd_runtime(argc, argv);

    if (strcmp(argv[1], "test") == 0)
        return cmd_test();

    if (strcmp(argv[1], "debug") == 0) {
        if (argc < 3) {
            fprintf(stderr, "cellar: debug needs a path to a .exe\n");
            return 1;
        }
        return cmd_debug(argv[2]);
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
