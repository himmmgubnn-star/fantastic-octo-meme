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
#include "cellar/desktop.h"
#include "cellar/device.h"
#include "cellar/inspect.h"
#include "cellar/loader.h"
#include "cellar/pe.h"
#include "cellar/perf.h"
#include "cellar/plugin.h"
#include "cellar/platform.h"
#include "cellar/prefix.h"
#include "cellar/runtime.h"
#include "cellar/shadercache.h"
#include "cellar/testlab.h"
#include "cellar/trace.h"
#include "cellar/win32.h"
#include "cellar/workspace.h"

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
        fprintf(stderr, "cellar: prefix needs create|list|info|delete|clone|"
                        "import|export|set|settings|backup|restore|launch\n");
        return 1;
    }
    sub = argv[2];
    if (strcmp(sub, "list") == 0) {
        cellar_prefix_info_t list[64];
        size_t n = cellar_prefix_list(root, list, 64);
        size_t i;
        printf("Prefixes in %s (%zu):\n", root, n);
        for (i = 0; i < n; i++)
            printf("  %-16s  %s  gfx=%s audio=%s arch=%s runner=%s\n",
                   list[i].name, list[i].path, list[i].gfx, list[i].audio,
                   list[i].arch, list[i].runner);
        return 0;
    }
    if (argc < 4) {
        fprintf(stderr, "cellar: prefix %s needs a name\n", sub);
        return 1;
    }
    if (strcmp(sub, "create") == 0) {
        const char *arch = NULL;
        if (argc > 4) {
            if (strncmp(argv[4], "--arch=", 7) == 0)
                arch = argv[4] + 7;
            else if (strcmp(argv[4], "--arch") == 0 && argc > 5)
                arch = argv[5];
        }
        if (cellar_prefix_create_arch(root, argv[3], arch) != CELLAR_OK) {
            fprintf(stderr, "cellar: failed to create prefix '%s'\n", argv[3]);
            return 1;
        }
        printf("created prefix %s/%s (arch=%s)\n", root, argv[3],
               arch ? arch : "win64");
        return 0;
    }
    if (strcmp(sub, "clone") == 0) {
        const char *dst = argc > 4 ? argv[4] : NULL;
        if (!dst) {
            fprintf(stderr, "cellar: prefix clone SRC DST\n");
            return 1;
        }
        if (cellar_prefix_clone(root, argv[3], dst) != CELLAR_OK)
            return 1;
        printf("cloned %s -> %s\n", argv[3], dst);
        return 0;
    }
    if (strcmp(sub, "import") == 0) {
        const char *file = argc > 4 ? argv[4] : NULL;
        if (!file) {
            fprintf(stderr, "cellar: prefix import NAME FILE\n");
            return 1;
        }
        if (cellar_prefix_import(root, argv[3], file) != CELLAR_OK)
            return 1;
        printf("imported %s from %s\n", argv[3], file);
        return 0;
    }
    if (strcmp(sub, "export") == 0) {
        const char *file = argc > 4 ? argv[4] : NULL;
        if (!file) {
            fprintf(stderr, "cellar: prefix export NAME FILE\n");
            return 1;
        }
        if (cellar_prefix_export(root, argv[3], file) != CELLAR_OK)
            return 1;
        printf("exported %s -> %s\n", argv[3], file);
        return 0;
    }
    if (strcmp(sub, "set") == 0) {
        const char *key, *val;
        if (argc < 6) {
            fprintf(stderr, "cellar: prefix set NAME KEY VALUE\n");
            return 1;
        }
        key = argv[4];
        val = argv[5];
        if (cellar_prefix_set_setting(root, argv[3], key, val) != CELLAR_OK)
            return 1;
        printf("%s.%s=%s\n", argv[3], key, val);
        return 0;
    }
    if (strcmp(sub, "settings") == 0) {
        const char *keys[16] = { "version_mode", "gfx", "audio", "arch",
                                 "runner", "resolution", "virtual_desktop",
                                 "vd_width", "vd_height", "vd_dpi", "box64",
                                 "cpu_core_limit", "frame_cap", "esync",
                                 "fsync", "dll_overrides" };
        size_t i;
        for (i = 0; i < sizeof keys / sizeof keys[0]; i++) {
            char v[256];
            if (cellar_prefix_get_setting(root, argv[3], keys[i], v,
                                          sizeof v) == CELLAR_OK)
                printf("%s=%s\n", keys[i], v);
        }
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
        printf("Arch:     %s\n", info.arch);
        printf("Runner:   %s\n", info.runner);
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

static cellar_setup_kind_t setup_from_name(const char *s)
{
    if (!s)
        return CELLAR_SETUP_EXE;
    if (strcmp(s, "msi") == 0 || strcmp(s, "install-msi") == 0)
        return CELLAR_SETUP_MSI;
    if (strcmp(s, "import") == 0 || strcmp(s, "import-prefix") == 0)
        return CELLAR_SETUP_IMPORT;
    if (strcmp(s, "portable") == 0 || strcmp(s, "add-portable") == 0)
        return CELLAR_SETUP_PORTABLE;
    return CELLAR_SETUP_EXE;
}

static int print_workspace(const cellar_workspace_t *w)
{
    printf("Name:          %s\n", w->name);
    printf("Path:          %s\n", w->path);
    printf("Setup:         %s\n", w->setup_label);
    printf("Source:        %s\n", w->source[0] ? w->source : "(none)");
    printf("Executable:    %s\n", w->executable[0] ? w->executable : "(unset)");
    printf("Architecture:  %s\n", w->architecture[0] ? w->architecture : "?");
    printf("Runner:        %s\n", w->runner[0] ? w->runner : "?");
    printf("Windows:       %s\n", cellar_version_profile(w->windows_version)->name);
    printf("Graphics:      %s\n", w->gfx_backend[0] ? w->gfx_backend : "Auto");
    printf("Audio:         %s\n", w->audio_backend[0] ? w->audio_backend : "Auto");
    printf("DLL overrides: %s\n", w->dll_overrides[0] ? w->dll_overrides : "(none)");
    printf("Dependencies:  %s\n", w->dependencies[0] ? w->dependencies : "(none)");
    printf("Tags:          %s\n", w->tags[0] ? w->tags : "(none)");
    printf("Perf mode:     %s\n", cellar_perf_mode_name(w->perf_mode));
    printf("Resolution:    %ux%u @%u DPI%s\n", w->resolution_width,
           w->resolution_height, w->dpi, w->virtual_desktop ? " virtual" : "");
    printf("Hash:          %s\n", w->exe_hash[0] ? w->exe_hash : "(none)");
    printf("Rating:        %s\n", w->compat_rating[0] ? w->compat_rating : "UNKNOWN");
    printf("Size:          %llu bytes%s\n",
           (unsigned long long)w->install_size, w->favorite ? "  [favorite]" : "");
    printf("Shortcut:      %s\n", w->has_shortcut ? "yes" : "no");
    printf("Sandbox:       %s\n", w->sandbox_enabled ? "enabled" : "disabled");
    {
        char perms[512];
        cellar_workspace_permissions_text(w->permissions, perms, sizeof perms);
        printf("Permissions:   %s\n", perms);
    }
    printf("Controls:      %s\n", w->controls[0] ? w->controls : "default");
    return 0;
}

static int cmd_profile(int argc, char **argv);

static int cmd_app(int argc, char **argv)
{
    const char *root = cellar_workspace_root();
    const char *sub;
    if (argc < 3) {
        fprintf(stderr, "cellar: app needs add|list|show|remove|set|permissions|"
                        "perf|controls|resolution|doctor|snapshot|rollback|diff|"
                        "repair|support|diagnose|safety|shader|deps|run|profile\n");
        return 1;
    }
    sub = argv[2];

    if (strcmp(sub, "add") == 0 || strcmp(sub, "new") == 0) {
        const char *name, *source = NULL, *launch = NULL;
        cellar_setup_kind_t kind = CELLAR_SETUP_EXE;
        int i;
        cellar_workspace_t w;
        if (argc < 4) {
            fprintf(stderr, "cellar: app add NAME [SOURCE] [--kind exe|msi|import|portable] [--launch EXE]\n");
            return 1;
        }
        name = argv[3];
        for (i = 4; i < argc; i++) {
            if (strcmp(argv[i], "--kind") == 0 && i + 1 < argc) {
                kind = setup_from_name(argv[++i]);
            } else if (strcmp(argv[i], "--launch") == 0 && i + 1 < argc) {
                launch = argv[++i];
            } else if (!source) {
                source = argv[i];
            }
        }
        if (cellar_workspace_install(root, name, kind, source, launch, &w) != CELLAR_OK) {
            fprintf(stderr, "cellar: failed to add workspace '%s'\n", name);
            return 1;
        }
        printf("Added workspace %s (kind=%s, id=%s)\n", name,
               cellar_setup_kind_name(kind), w.id);
        return 0;
    }

    if (strcmp(sub, "list") == 0) {
        cellar_workspace_t ws[64];
        size_t n = cellar_workspace_list(root, ws, 64);
        size_t i;
        printf("Winaltor workspaces in %s (%zu):\n", root, n);
        for (i = 0; i < n; i++) {
            printf("  %-20s %s  %s  %s%s  (%llu bytes)\n",
                   ws[i].name, ws[i].setup_label, ws[i].architecture,
                   ws[i].gfx_backend[0] ? ws[i].gfx_backend : "Auto",
                   ws[i].favorite ? " *" : "",
                   (unsigned long long)ws[i].install_size);
        }
        return 0;
    }

    if (strcmp(sub, "diagnose") == 0) {
        char buf[4096];
        if (argc < 4) {
            fprintf(stderr, "cellar: app diagnose LOGFILE\n");
            return 1;
        }
        if (cellar_workspace_diagnose(argv[3], buf, sizeof buf) != CELLAR_OK)
            return 1;
        printf("%s", buf);
        return 0;
    }

    if (argc < 4) {
        fprintf(stderr, "cellar: app %s needs a workspace name\n", sub);
        return 1;
    }

    if (strcmp(sub, "show") == 0) {
        cellar_workspace_t w;
        if (cellar_workspace_load(root, argv[3], &w) != CELLAR_OK) {
            fprintf(stderr, "cellar: workspace '%s' not found\n", argv[3]);
            return 1;
        }
        print_workspace(&w);
        return 0;
    }

    if (strcmp(sub, "remove") == 0 || strcmp(sub, "rm") == 0) {
        if (cellar_workspace_remove(root, argv[3]) != CELLAR_OK)
            return 1;
        printf("Removed workspace %s\n", argv[3]);
        return 0;
    }

    if (strcmp(sub, "set") == 0) {
        if (argc < 6) {
            fprintf(stderr, "cellar: app set NAME KEY VALUE\n");
            return 1;
        }
        if (cellar_workspace_set(root, argv[3], argv[4], argv[5]) != CELLAR_OK) {
            fprintf(stderr, "cellar: cannot set %s\n", argv[4]);
            return 1;
        }
        printf("Set %s=%s for %s\n", argv[4], argv[5], argv[3]);
        return 0;
    }

    if (strcmp(sub, "permissions") == 0) {
        cellar_workspace_t w;
        if (argc >= 5) {
            uint32_t perms = (uint32_t)strtoul(argv[4], NULL, 0);
            if (cellar_workspace_set_permissions(root, argv[3], perms) != CELLAR_OK)
                return 1;
            printf("Set permissions to 0x%x\n", (unsigned)perms);
        } else {
            if (cellar_workspace_load(root, argv[3], &w) != CELLAR_OK)
                return 1;
            {
                char p[512];
                cellar_workspace_permissions_text(w.permissions, p, sizeof p);
                printf("%s\n", p);
            }
        }
        return 0;
    }

    if (strcmp(sub, "perf") == 0) {
        cellar_workspace_t w;
        if (argc >= 5) {
            size_t i;
            for (i = 0; i < (size_t)CELLAR_PERF_MODE_COUNT; i++)
                if (strcmp(cellar_perf_mode_name((cellar_perf_mode_t)i), argv[4]) == 0) {
                    if (cellar_workspace_set_perf_mode(root, argv[3],
                                                       (cellar_perf_mode_t)i) != CELLAR_OK)
                        return 1;
                    printf("Set perf mode %s\n", argv[4]);
                    return 0;
                }
            fprintf(stderr, "cellar: unknown perf mode '%s'\n", argv[4]);
            return 1;
        } else {
            if (cellar_workspace_load(root, argv[3], &w) != CELLAR_OK)
                return 1;
            printf("%s\n", cellar_perf_mode_name(w.perf_mode));
        }
        return 0;
    }

    if (strcmp(sub, "controls") == 0) {
        if (argc >= 5) {
            char controls[1024];
            int i;
            controls[0] = '\0';
            for (i = 4; i < argc; i++) {
                if (i > 4)
                    cellar_strlcat(controls, sizeof controls, " ");
                cellar_strlcat(controls, sizeof controls, argv[i]);
            }
            if (cellar_workspace_set_controls(root, argv[3], controls) != CELLAR_OK)
                return 1;
            printf("Set controls for %s\n", argv[3]);
        } else {
            cellar_workspace_t w;
            if (cellar_workspace_load(root, argv[3], &w) != CELLAR_OK)
                return 1;
            printf("%s\n", w.controls[0] ? w.controls : "default");
        }
        return 0;
    }

    if (strcmp(sub, "resolution") == 0) {
        if (argc < 6) {
            fprintf(stderr, "cellar: app resolution NAME WIDTH HEIGHT [DPI [VIRTUAL]]\n");
            return 1;
        }
        {
            uint32_t wd = (uint32_t)strtoul(argv[4], NULL, 0);
            uint32_t ht = (uint32_t)strtoul(argv[5], NULL, 0);
            uint32_t dpi = argc >= 7 ? (uint32_t)strtoul(argv[6], NULL, 0) : 96;
            int virt = argc >= 8 ? atoi(argv[7]) : 0;
            if (cellar_workspace_set_resolution(root, argv[3], wd, ht, dpi, virt) != CELLAR_OK)
                return 1;
            printf("Set resolution %ux%u @%u DPI%s\n", wd, ht, dpi, virt ? " virtual" : "");
        }
        return 0;
    }

    if (strcmp(sub, "doctor") == 0) {
        cellar_doctor_report_t r;
        if (cellar_workspace_doctor(root, argv[3], &r) != CELLAR_OK) {
            fprintf(stderr, "cellar: doctor failed\n");
            return 1;
        }
        cellar_doctor_report(&r);
        return r.ready ? 0 : 1;
    }

    if (strcmp(sub, "snapshot") == 0) {
        const char *label = argc >= 5 ? argv[4] : "default";
        if (cellar_workspace_snapshot(root, argv[3], label) != CELLAR_OK)
            return 1;
        printf("Snapshotted %s as %s\n", argv[3], label);
        return 0;
    }

    if (strcmp(sub, "rollback") == 0 || strcmp(sub, "restore") == 0) {
        if (argc < 5) {
            fprintf(stderr, "cellar: app rollback NAME LABEL\n");
            return 1;
        }
        if (cellar_workspace_rollback(root, argv[3], argv[4]) != CELLAR_OK)
            return 1;
        printf("Rolled back %s to %s\n", argv[3], argv[4]);
        return 0;
    }

    if (strcmp(sub, "diff") == 0) {
        char diff[4096];
        if (argc < 6) {
            fprintf(stderr, "cellar: app diff NAME A B\n");
            return 1;
        }
        {
            int nd = cellar_workspace_profile_diff(root, argv[3], argv[4], argv[5],
                                                   diff, sizeof diff);
            if (nd < 0) {
                fprintf(stderr, "cellar: cannot diff profiles\n");
                return 1;
            }
            if (nd == 0)
                printf("profiles are identical\n");
            else
                printf("%s", diff);
        }
        return 0;
    }

    if (strcmp(sub, "repair") == 0) {
        if (cellar_workspace_repair(root, argv[3]) != CELLAR_OK)
            return 1;
        printf("Repaired %s\n", argv[3]);
        return 0;
    }

    if (strcmp(sub, "support") == 0) {
        const char *out = argc >= 5 ? argv[4] : NULL;
        char path[640];
        if (!out) {
            snprintf(path, sizeof path, "%s/%s.support.txt", root, argv[3]);
            out = path;
        }
        if (cellar_workspace_support(root, argv[3], out) != CELLAR_OK)
            return 1;
        printf("Wrote support bundle to %s\n", out);
        return 0;
    }

    if (strcmp(sub, "safety") == 0) {
        char buf[2048];
        if (cellar_workspace_safety_report(root, argv[3], buf, sizeof buf) != CELLAR_OK)
            return 1;
        printf("%s", buf);
        return 0;
    }

    if (strcmp(sub, "shader") == 0) {
        if (argc >= 5 && strcmp(argv[4], "clear") == 0) {
            if (cellar_workspace_shader_clear(root, argv[3]) != CELLAR_OK)
                return 1;
            printf("Cleared shader cache for %s\n", argv[3]);
        } else {
            printf("%llu bytes\n", (unsigned long long)cellar_workspace_shader_size(root, argv[3]));
        }
        return 0;
    }

    if (strcmp(sub, "deps") == 0) {
        cellar_workspace_t w;
        if (cellar_workspace_load(root, argv[3], &w) != CELLAR_OK)
            return 1;
        printf("%s\n", w.dependencies[0] ? w.dependencies : "(none)");
        return 0;
    }

    if (strcmp(sub, "run") == 0) {
        cellar_doctor_report_t r;
        cellar_workspace_t w;
        if (cellar_workspace_load(root, argv[3], &w) != CELLAR_OK) {
            fprintf(stderr, "cellar: workspace '%s' not found\n", argv[3]);
            return 1;
        }
        if (cellar_workspace_doctor(root, argv[3], &r) != CELLAR_OK)
            return 1;
        printf("== launching %s ==\n", argv[3]);
        print_workspace(&w);
        printf("\nLaunch doctor:\n");
        cellar_doctor_report(&r);
        if (!r.ready)
            return 1;
        printf("\nExecution of Windows binaries is not yet implemented "
               "(see docs/ROADMAP.md, Milestone 1); this is a launch dry-run.\n");
        return 0;
    }

    if (strcmp(sub, "profile") == 0) {
        return cmd_profile(argc, argv);
    }

    fprintf(stderr, "cellar: unknown app subcommand '%s'\n", sub);
    return 1;
}

static int cmd_device(int argc, char **argv)
{
    char buf[4096];
    (void)argc;
    if (argv[2] && strcmp(argv[2], "report") != 0) {
        fprintf(stderr, "cellar: device subcommands: report\n");
        return 1;
    }
    if (cellar_device_report(buf, sizeof buf) != CELLAR_OK)
        return 1;
    printf("%s", buf);
    return 0;
}

static int cmd_container(int argc, char **argv)
{
    /* A container and a workspace are the same isolated environment. */
    if (argc < 3) {
        fprintf(stderr, "cellar: container needs create|list|show|remove\n");
        return 1;
    }
    {
        char *fake[32];
        int n = 0;
        int i;
        fake[n++] = (char *)"app";   /* argv[0] (ignored)             */
        fake[n++] = (char *)"container"; /* argv[1] (ignored)         */
        fake[n++] = strcmp(argv[2], "create") == 0 ? (char *)"add" : argv[2];
        for (i = 3; i < argc && n < 30; i++)
            fake[n++] = argv[i];
        fake[n] = NULL;              /* argv[argc] sentinel           */
        return cmd_app(n, fake);
    }
}

static int cmd_profile(int argc, char **argv)
{
    const char *root = cellar_workspace_root();
    if (argc < 3) {
        fprintf(stderr, "cellar: profile needs list|show|apply|export|import\n");
        return 1;
    }
    if (strcmp(argv[2], "list") == 0 && argc >= 4) {
        char labels[16][64];
        size_t n = cellar_workspace_profile_list(root, argv[3], labels, 16);
        size_t i;
        printf("Profiles for %s (%zu):\n", argv[3], n);
        for (i = 0; i < n; i++)
            printf("  %s\n", labels[i]);
        return 0;
    }
    if (strcmp(argv[2], "show") == 0 && argc >= 5) {
        cellar_profile_point_t p;
        if (cellar_workspace_profile_load(root, argv[3], argv[4], &p) != CELLAR_OK)
            return 1;
        printf("Label:        %s\n", p.label);
        printf("Runner:       %s\n", p.runner);
        printf("Architecture: %s\n", p.architecture);
        printf("Windows:      %s\n", cellar_version_profile(p.windows_version)->name);
        printf("Graphics:     %s\n", p.gfx_backend);
        printf("Audio:        %s\n", p.audio_backend);
        printf("Dependencies: %s\n", p.dependencies);
        printf("DLL overrides:%s\n", p.dll_overrides);
        printf("Resolution:   %s\n", p.resolution);
        printf("Launch:       %s %s\n", p.launch_executable, p.launch_args);
        printf("Trust:        %s\n", cellar_profile_trust_name(p.trust));
        printf("Version:      %u\n", p.version);
        return 0;
    }
    if (strcmp(argv[2], "apply") == 0 && argc >= 5) {
        cellar_profile_point_t p;
        if (cellar_workspace_profile_load(root, argv[3], argv[4], &p) != CELLAR_OK)
            return 1;
        if (cellar_workspace_profile_apply(root, argv[3], &p) != CELLAR_OK)
            return 1;
        printf("Applied %s to %s\n", argv[4], argv[3]);
        return 0;
    }
    if (strcmp(argv[2], "export") == 0 && argc >= 5) {
        if (cellar_workspace_profile_export(root, argv[3], argv[4]) != CELLAR_OK)
            return 1;
        printf("Exported %s profile to %s\n", argv[3], argv[4]);
        return 0;
    }
    if (strcmp(argv[2], "import") == 0 && argc >= 5) {
        if (cellar_workspace_profile_import(root, argv[3], argv[4]) != CELLAR_OK)
            return 1;
        printf("Imported %s into %s\n", argv[4], argv[3]);
        return 0;
    }
    fprintf(stderr, "cellar: usage: profile list|show|apply|export|import NAME [LABEL/PATH]\n");
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
        "  cellar prefix create|list|info|delete|clone|import|export\n"
        "        |set|settings|backup|restore|launch\n"
        "  cellar runtime list|install|uninstall KIND\n"
        "  cellar app add NAME [SRC] [--kind exe|msi|import|portable]\n"
        "  cellar app list|show|remove|set|run|doctor|repair NAME ...\n"
        "  cellar app snapshot|rollback|diff|support|safety|shader NAME ...\n"
        "  cellar profile list|show|apply|export|import NAME [LABEL|PATH]\n"
        "  cellar container create|list|show|remove NAME ...\n"
        "  cellar device report           host device / capability report\n"
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

    if (strcmp(argv[1], "app") == 0)
        return cmd_app(argc, argv);

    if (strcmp(argv[1], "profile") == 0)
        return cmd_profile(argc, argv);

    if (strcmp(argv[1], "container") == 0)
        return cmd_container(argc, argv);

    if (strcmp(argv[1], "device") == 0)
        return cmd_device(argc, argv);

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
