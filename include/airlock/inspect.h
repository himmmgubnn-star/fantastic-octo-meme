/*
 * inspect.h — automatic application inspector.
 *
 * Before launching a Windows binary, Airlock inspects the PE: architecture,
 * imported DLLs, exported APIs, TLS, resources, manifests, the COM/CLR
 * directory, delay-loads, and the runtimes those imply (.NET, VC++). The
 * result is a structured report used by the compatibility database and the
 * `airlock inspect` command.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_INSPECT_H
#define AIRLOCK_INSPECT_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"
#include "loader.h"

#define AIRLOCK_INSPECT_MAX_DLLS 32

typedef struct airlock_inspect {
    char     path[512];
    char     basename[128];
    int      is_64bit;
    int      is_dll;
    uint16_t machine;
    uint16_t subsystem;

    size_t   import_count;
    size_t   export_count;
    size_t   unique_dll_count;
    char     unique_dlls[AIRLOCK_INSPECT_MAX_DLLS][64];

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
} airlock_inspect_t;

/* Inspect a loaded image. `path` is recorded for the report (may be NULL). */
airlock_status_t airlock_inspect_image(const airlock_image_t *img,
                                     const char *path,
                                     airlock_inspect_t *out);

/* Render the inspector report to stdout. */
void airlock_inspect_report(const airlock_inspect_t *ins);

#endif /* AIRLOCK_INSPECT_H */
