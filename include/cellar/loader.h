/*
 * loader.h — PE loader public API and in-memory image model.
 *
 * The loader is the heart of Cellar. It turns a raw PE file (or an in-memory
 * buffer) into a `cellar_image_t`: the headers are validated, the section
 * table is parsed, and a section-aligned virtual image is assembled so that
 * RVA lookups and the entry point behave the way they would on Windows.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_LOADER_H
#define CELLAR_LOADER_H

#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>

#include "cellar.h"
#include "pe.h"

/* Flags passed to cellar_image_load (from_file / from_buffer). */
enum cellar_load_flags {
    CELLAR_LOAD_NONE        = 0,
    CELLAR_LOAD_MAP_SECTIONS = (1 << 0), /* assemble section-aligned image  */
    CELLAR_LOAD_PARSE_IMPORTS = (1 << 1),/* walk and resolve import table   */
    CELLAR_LOAD_PARSE_EXPORTS = (1 << 2),/* build export name index         */
    CELLAR_LOAD_PARSE_RELOCS  = (1 << 3),/* validate base relocation blocks */
    CELLAR_LOAD_DEFAULT = CELLAR_LOAD_MAP_SECTIONS |
                          CELLAR_LOAD_PARSE_IMPORTS
};

/* A single resolved import (one thunk). `module` and `name` point into the
 * image buffer; `resolved` is the target Cellar-side function (or NULL if the
 * import could not be satisfied — Cellar still records it for diagnostics). */
typedef struct cellar_import {
    char        *module;       /* DLL name, e.g. "KERNEL32.dll"  */
    const char  *name;         /* function name (NULL => ordinal) */
    uint16_t     ordinal;
    void        *resolved;     /* Cellar Win32 stub (see <cellar/win32.h>) */
} cellar_import_t;

/* One parsed section. `mapped` is the section-aligned address within the
 * assembled image (only valid when CELLAR_LOAD_MAP_SECTIONS is set). */
typedef struct cellar_section_view {
    char     name[8];
    uint32_t virtual_address;
    uint32_t virtual_size;
    uint32_t raw_offset;
    uint32_t raw_size;
    uint32_t characteristics;
    uint8_t *mapped;           /* NULL if section isn't backed by raw data */
} cellar_section_view_t;

/* The parsed, in-memory representation of a PE image. */
typedef struct cellar_image {
    uint8_t *raw;              /* original file bytes (owned)              */
    size_t   raw_size;

    cellar_pe_dos_header_t     dos;
    cellar_pe_coff_header_t    coff;
    cellar_pe_optional_header_t opt;

    cellar_section_view_t *sections; /* [coff.number_of_sections]          */
    size_t                section_count;

    uint8_t *mapped;           /* section-aligned virtual image (owned)    */
    size_t   mapped_size;

    cellar_import_t *imports;  /* [import_count] resolved imports          */
    size_t           import_count;

    /* Export index: number of named exports + the name array itself. */
    char  **export_names;
    size_t  export_name_count;

    bool     is_dll;
    bool     sections_mapped;
    bool     imports_parsed;
    bool     exports_parsed;
    bool     relocs_parsed;
} cellar_image_t;

/* ---- API ----------------------------------------------------------------- */

/* Load and parse a PE image from a file on disk. */
cellar_status_t cellar_image_load_file(const char *path,
                                       enum cellar_load_flags flags,
                                       cellar_image_t *out);

/* Load and parse a PE image from an in-memory buffer (the buffer is copied). */
cellar_status_t cellar_image_load_buffer(const void *data, size_t len,
                                         enum cellar_load_flags flags,
                                         cellar_image_t *out);

/* Translate an RVA into the assembled image (returns NULL when unmapped). */
uint8_t *cellar_image_rva(const cellar_image_t *img, uint32_t rva);

/* Translate an RVA into the raw file buffer (NULL when not in a section). */
const uint8_t *cellar_image_rva_raw(const cellar_image_t *img, uint32_t rva);

/* Bounds-checked read of `len` bytes at `rva` from the assembled image. */
bool cellar_image_read(const cellar_image_t *img, uint32_t rva,
                       void *dst, size_t len);

/* Release every allocation owned by the image (safe to call on a zeroed img). */
void cellar_image_unload(cellar_image_t *img);

#endif /* CELLAR_LOADER_H */
