/*
 * loader.c — PE image loading pipeline.
 *
 * From a raw buffer or a file, this builds the full `cellar_image_t`: it maps
 * the section-aligned virtual image, walks the import table and binds each
 * thunk to a Cellar Win32 stub, indexes named exports, and validates the base
 * relocation blocks. It does not yet execute code — that is the job of the
 * (future) CPU/ABI emulation layer.
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

/* Declared in pe.c */
cellar_status_t cellar_parse_headers(const uint8_t *data, size_t len,
                                     cellar_image_t *img);

/* ---- Raw file I/O -------------------------------------------------------- */

static cellar_status_t read_file_all(const char *path, uint8_t **out,
                                     size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long   len;
    uint8_t *buf;

    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;

    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return CELLAR_ERR_INVALID_ARGUMENT;
    }

    buf = malloc((size_t)len ? (size_t)len : 1);
    if (!buf) {
        fclose(f);
        return CELLAR_ERR_OUT_OF_MEMORY;
    }

    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return CELLAR_ERR_INVALID_ARGUMENT;
    }

    fclose(f);
    *out = buf;
    *out_len = (size_t)len;
    return CELLAR_OK;
}

/* ---- Section mapping ----------------------------------------------------- */

static cellar_status_t map_sections(cellar_image_t *img)
{
    size_t i;

    if (img->opt.size_of_image == 0)
        return CELLAR_ERR_PE_BAD_SECTIONS;

    img->mapped_size = img->opt.size_of_image;
    img->mapped = calloc(1, img->mapped_size);
    if (!img->mapped)
        return CELLAR_ERR_OUT_OF_MEMORY;

    /* Headers region first. */
    {
        size_t hdr = CELLAR_MIN((size_t)img->opt.size_of_headers,
                                img->mapped_size);
        memcpy(img->mapped, img->raw, CELLAR_MIN(hdr, img->raw_size));
    }

    for (i = 0; i < img->section_count; i++) {
        cellar_section_view_t *s = &img->sections[i];
        if (s->raw_size == 0 || s->raw_offset >= img->raw_size)
            continue;
        if ((uint64_t)s->virtual_address + s->raw_size > img->mapped_size)
            continue; /* malformed; skip to stay safe */
        memcpy(img->mapped + s->virtual_address,
               img->raw + s->raw_offset, s->raw_size);
        s->mapped = img->mapped + s->virtual_address;
    }

    img->sections_mapped = true;
    return CELLAR_OK;
}

/* ---- Imports ------------------------------------------------------------- */

static size_t thunk_size(const cellar_image_t *img)
{
    return img->opt.magic == CELLAR_PE_MAGIC_PE32 ? 4 : 8;
}

/*
 * Read a thunk value at an IAT/ILT RVA, honoring the 32/64-bit format. The
 * high bit selects ordinal vs. name imports.
 */
static bool read_thunk(const cellar_image_t *img, uint32_t rva,
                       uint64_t *value, bool *ordinal)
{
    uint8_t *p = cellar_image_rva(img, rva);
    if (!p)
        return false;

    if (img->opt.magic == CELLAR_PE_MAGIC_PE32) {
        uint32_t v = cellar_le32(p);
        *ordinal = (v & 0x80000000u) != 0;
        *value = v & 0x7FFFFFFFu;
    } else {
        uint64_t v = cellar_le64(p);
        *ordinal = (v & 0x8000000000000000ull) != 0;
        *value = v & 0x7FFFFFFFFFFFFFFFull;
    }
    return true;
}

static cellar_status_t parse_imports(cellar_image_t *img)
{
    const cellar_pe_data_directory_t *dir;
    cellar_pe_import_descriptor_t desc;
    uint32_t rva, iat_rva;
    size_t cap = 0, n = 0;
    size_t thunk = thunk_size(img);

    dir = &img->opt.data_directory[CELLAR_PE_DIR_IMPORT];
    if (dir->virtual_address == 0 || dir->size == 0)
        return CELLAR_OK; /* no imports — fine */

    rva = dir->virtual_address;
    for (;;) {
        uint8_t *p;
        uint32_t iat, thunk_idx;

        if (!cellar_image_read(img, rva, &desc, sizeof desc))
            break; /* fell off the image; treat as end-of-table */
        /* End of the import descriptor array: all fields zero. */
        if (desc.characteristics == 0 && desc.first_thunk == 0 &&
            desc.name_rva == 0)
            break;

        iat_rva = desc.first_thunk ? desc.first_thunk : desc.characteristics;
        if (iat_rva == 0) {
            rva += sizeof desc;
            continue;
        }

        p = cellar_image_rva(img, desc.name_rva);
        if (!p)
            return CELLAR_ERR_PE_BAD_IMPORTS;
        /* module name is a NUL-terminated ASCII string inside the image */
        {
            char *mod = (char *)p;
            for (thunk_idx = 0; ; thunk_idx++) {
                uint64_t val;
                bool ordinal;
                const char *fn = NULL;
                uint16_t ord = 0;
                uint8_t *fni;

                iat = iat_rva + thunk_idx * (uint32_t)thunk;
                if (!read_thunk(img, iat, &val, &ordinal))
                    break;

                if (ordinal) {
                    ord = (uint16_t)val;
                } else {
                    if (val == 0)
                        break;
                    fni = cellar_image_rva(img, (uint32_t)val);
                    if (!fni)
                        return CELLAR_ERR_PE_BAD_IMPORTS;
                    /* skip 2-byte hint; name follows as ASCII */
                    fn = (const char *)(fni + sizeof(uint16_t));
                }

                if (n == cap) {
                    size_t ncap = cap ? cap * 2 : 16;
                    cellar_import_t *ni = realloc(img->imports,
                                                  ncap * sizeof *ni);
                    if (!ni)
                        return CELLAR_ERR_OUT_OF_MEMORY;
                    img->imports = ni;
                    cap = ncap;
                }

                img->imports[n].module = mod;
                img->imports[n].name = fn;
                img->imports[n].ordinal = ord;
                img->imports[n].resolved =
                    cellar_win32_resolve(mod, fn, ord);
                n++;
            }
        }
        rva += sizeof desc;
    }

    img->import_count = n;
    img->imports_parsed = true;
    return CELLAR_OK;
}

/* ---- Exports ------------------------------------------------------------- */

static cellar_status_t parse_exports(cellar_image_t *img)
{
    const cellar_pe_data_directory_t *dir;
    cellar_pe_export_directory_t exp;
    uint32_t *names;
    uint32_t i;

    dir = &img->opt.data_directory[CELLAR_PE_DIR_EXPORT];
    if (dir->virtual_address == 0 || dir->size == 0)
        return CELLAR_OK;

    if (!cellar_image_read(img, dir->virtual_address, &exp, sizeof exp))
        return CELLAR_ERR_PE_BAD_EXPORTS;

    img->export_name_count = exp.number_of_names;
    if (exp.number_of_names == 0)
        return CELLAR_OK;

    if (exp.number_of_names > 0x100000) /* sanity */
        return CELLAR_ERR_PE_BAD_EXPORTS;

    img->export_names = calloc(exp.number_of_names, sizeof(char *));
    if (!img->export_names)
        return CELLAR_ERR_OUT_OF_MEMORY;

    names = calloc(exp.number_of_names, sizeof(uint32_t));
    if (!names) {
        free(img->export_names);
        img->export_names = NULL;
        return CELLAR_ERR_OUT_OF_MEMORY;
    }

    if (!cellar_image_read(img, exp.address_of_names, names,
                           exp.number_of_names * sizeof(uint32_t))) {
        free(names);
        return CELLAR_ERR_PE_BAD_EXPORTS;
    }

    for (i = 0; i < exp.number_of_names; i++) {
        uint8_t *p = cellar_image_rva(img, names[i]);
        if (p) {
            const uint8_t *end = memchr(p, 0, 4096);
            size_t len = end ? (size_t)(end - p) : 4096;
            img->export_names[i] = malloc(len + 1);
            if (!img->export_names[i])
                continue;
            memcpy(img->export_names[i], p, len);
            img->export_names[i][len] = '\0';
        }
    }

    free(names);
    img->exports_parsed = true;
    return CELLAR_OK;
}

/* ---- Base relocations ---------------------------------------------------- */

static cellar_status_t parse_relocs(cellar_image_t *img)
{
    const cellar_pe_data_directory_t *dir;
    uint32_t rva, end;

    dir = &img->opt.data_directory[CELLAR_PE_DIR_BASERELOC];
    if (dir->virtual_address == 0 || dir->size == 0)
        return CELLAR_OK;

    rva = dir->virtual_address;
    end = rva + dir->size;
    while (rva + sizeof(cellar_pe_base_reloc_t) <= end) {
        cellar_pe_base_reloc_t block;
        uint32_t entries;
        if (!cellar_image_read(img, rva, &block, sizeof block))
            break;
        if (block.size_of_block < sizeof block)
            return CELLAR_ERR_PE_BAD_RELOCATIONS;
        entries = (uint32_t)((block.size_of_block - (uint32_t)sizeof block) / 2);
        if (rva + block.size_of_block > end)
            return CELLAR_ERR_PE_BAD_RELOCATIONS;
        (void)entries;
        rva += block.size_of_block;
    }

    img->relocs_parsed = true;
    return CELLAR_OK;
}

/* ---- Public API ---------------------------------------------------------- */

cellar_status_t cellar_image_load_buffer(const void *data, size_t len,
                                         enum cellar_load_flags flags,
                                         cellar_image_t *out)
{
    cellar_status_t st;
    cellar_image_t img;

    if (!data || !out)
        return CELLAR_ERR_INVALID_ARGUMENT;

    st = cellar_parse_headers((const uint8_t *)data, len, &img);
    if (st != CELLAR_OK)
        return st;

    /* Copy the raw buffer so the image owns its bytes. */
    img.raw = malloc(len ? len : 1);
    if (!img.raw) {
        cellar_image_unload(&img);
        return CELLAR_ERR_OUT_OF_MEMORY;
    }
    memcpy(img.raw, data, len);
    img.raw_size = len;

    if (flags & CELLAR_LOAD_MAP_SECTIONS) {
        st = map_sections(&img);
        if (st != CELLAR_OK) {
            cellar_image_unload(&img);
            return st;
        }
    }

    if (flags & CELLAR_LOAD_PARSE_IMPORTS) {
        st = parse_imports(&img);
        if (st != CELLAR_OK) {
            cellar_image_unload(&img);
            return st;
        }
    }

    if (flags & CELLAR_LOAD_PARSE_EXPORTS) {
        st = parse_exports(&img);
        if (st != CELLAR_OK) {
            cellar_image_unload(&img);
            return st;
        }
    }

    if (flags & CELLAR_LOAD_PARSE_RELOCS) {
        st = parse_relocs(&img);
        if (st != CELLAR_OK) {
            cellar_image_unload(&img);
            return st;
        }
    }

    *out = img;
    return CELLAR_OK;
}

cellar_status_t cellar_image_load_file(const char *path,
                                       enum cellar_load_flags flags,
                                       cellar_image_t *out)
{
    cellar_status_t st;
    uint8_t *buf = NULL;
    size_t len = 0;

    st = read_file_all(path, &buf, &len);
    if (st != CELLAR_OK)
        return st;

    st = cellar_image_load_buffer(buf, len, flags, out);
    free(buf);
    return st;
}

void cellar_image_unload(cellar_image_t *img)
{
    size_t i;
    if (!img)
        return;

    free(img->sections);

    if (img->export_names) {
        for (i = 0; i < img->export_name_count; i++)
            free(img->export_names[i]);
        free(img->export_names);
    }

    free(img->imports);
    free(img->mapped);
    free(img->raw);
    memset(img, 0, sizeof *img);
}
