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
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/loader.h"
#include "cellar/pe.h"
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

static void usage(FILE *out)
{
    fprintf(out,
        "cellar %s — a Windows compatibility layer for Linux\n"
        "\n"
        "usage:\n"
        "  cellar --list-modules         list registered Win32 modules\n"
        "  cellar program.exe [args...]  load and inspect a Windows executable\n"
        "\n"
        "Execution of the loaded binary is not yet implemented; this build\n"
        "parses and reports the PE structure.\n",
        CELLAR_VERSION_STRING);
}

int main(int argc, char **argv)
{
    int i;

    cellar_win32_init();

    if (argc < 2) {
        usage(stderr);
        return 1;
    }

    if (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0) {
        usage(stdout);
        return 0;
    }

    if (strcmp(argv[1], "--list-modules") == 0) {
        cellar_win32_dump_registry();
        return 0;
    }

    for (i = 1; i < argc; i++) {
        if (dump_image(argv[i]) != 0)
            return 1;
    }

    return 0;
}
