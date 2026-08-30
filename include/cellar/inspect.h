/*
 * inspect.h — automatic application inspector.
 *
 * Before launching a Windows binary, Cellar inspects the PE: architecture,
 * imported DLLs, exported APIs, TLS, resources, manifests, the COM/CLR
 * directory, delay-loads, and the runtimes those imply (.NET, VC++). The
 * result is a structured report used by the compatibility database and the
 * `cellar inspect` command.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_INSPECT_H
#define CELLAR_INSPECT_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"
#include "loader.h"

#define CELLAR_INSPECT_MAX_DLLS 32

typedef struct cellar_inspect {
    char     path[512];
    char     basename[128];
    int      is_64bit;
    int      is_dll;
    uint16_t machine;
    uint16_t subsystem;

    size_t   import_count;
    size_t   export_count;
    size_t   unique_dll_count;
    char     unique_dlls[CELLAR_INSPECT_MAX_DLLS][64];

    int      has_tls;
    int      has_resources;
    int      has_manifest;       /* RT_MANIFEST (24) in the resource tree   */
    int      has_com;            /* IMAGE_DIRECTORY_ENTRY_COM_DESCRIPTOR    */
    int      has_delay_imports;
    int      is_dotnet;          /* COM/CLR directory or mscoree import     */
    int      needs_vcruntime;    /* msvcr / vcruntime / ucrtbase            */
    int      needs_dotnet;

    char     graphics[32];
    char     audio[32];
    char     input[32];
    char     networking[32];

    uint32_t tls_rva, tls_size;
    uint32_t resource_rva, resource_size;
    uint32_t com_rva, com_size;
} cellar_inspect_t;

/* Inspect a loaded image. `path` is recorded for the report (may be NULL). */
cellar_status_t cellar_inspect_image(const cellar_image_t *img,
                                     const char *path,
                                     cellar_inspect_t *out);

/* Render the inspector report to stdout. */
void cellar_inspect_report(const cellar_inspect_t *ins);

#endif /* CELLAR_INSPECT_H */
