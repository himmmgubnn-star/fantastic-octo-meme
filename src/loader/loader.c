/*
 * loader.c — PE image loading pipeline.
 *
 * From a raw buffer or a file, this builds the full `airlock_image_t`: it maps
 * the section-aligned virtual image, walks the import table and binds each
 * thunk to a Airlock Win32 stub, indexes named exports, and validates the base
 * relocation blocks. It does not yet execute code — that is the job of the
 * (future) CPU/ABI emulation layer.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/loader.h"
#include "airlock/pe.h"
#include "airlock/perf.h"
#include "airlock/platform.h"
#include "airlock/trace.h"
#include "airlock/win32.h"

/* Declared in pe.c */
airlock_status_t airlock_parse_headers(const uint8_t *data, size_t len,
                                     airlock_image_t *img);

/* ---- Raw file I/O -------------------------------------------------------- */

static airlock_status_t read_file_all(const char *path, uint8_t **out,
                                     size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    long   len;
    uint8_t *buf;

    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;

    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0 ||
        fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    }

    buf = malloc((size_t)len ? (size_t)len : 1);
    if (!buf) {
        fclose(f);
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    }

    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    }

    fclose(f);
    *out = buf;
    *out_len = (size_t)len;
    return AIRLOCK_OK;
}

/*
 * Load a file's bytes. For small files this reads normally; for files above
 * the performance tunable's threshold it memory-maps the file (zero-copy), so
 * large game executables load without a full buffer copy. Returns an owned
 * buffer either way: mmap results are copied out after parsing in
 * airlock_image_load_buffer, so the map can be released immediately.
 */
static airlock_status_t read_file_perf(const char *path, uint8_t **out,
                                      size_t *out_len)
{
    const airlock_perf_options_t *opt = airlock_perf_options();
    airlock_mapped_file_t mf;
    airlock_status_t st;
    uint8_t *buf;

    /* If a map is cheap and enabled, use it, then snapshot the bytes. */
    if (airlock_map_file(path, &mf) == AIRLOCK_OK && mf.size > 0) {
        if ((int)mf.size >= opt->mmap_threshold) {
            size_t msize = mf.size;
            buf = malloc(msize);
            if (!buf) {
                airlock_unmap_file(&mf);
                return AIRLOCK_ERR_OUT_OF_MEMORY;
            }
            memcpy(buf, mf.data, msize);
            airlock_unmap_file(&mf);
            airlock_perf_count_mmap_read();
            *out = buf;
            *out_len = msize;
            st = AIRLOCK_OK;
            return st;
        }
        airlock_unmap_file(&mf);
    }

    /* Fall back to a plain read (map failed or file is small). */
    return read_file_all(path, out, out_len);
}

/* ---- Section mapping ----------------------------------------------------- */

/*
 * SizeOfImage comes from the file and is used directly as an allocation size,
 * so it must be bounded before it is trusted. Without a ceiling, a few mutated
 * bytes turn into a multi-gigabyte allocation and a trivially reachable
 * out-of-memory condition. No real PE image approaches this.
 */
#define AIRLOCK_MAX_IMAGE_SIZE ((size_t)2 * 1024 * 1024 * 1024)

static airlock_status_t map_sections(airlock_image_t *img)
{
    size_t i;

    if (img->opt.size_of_image == 0 ||
        img->opt.size_of_image > AIRLOCK_MAX_IMAGE_SIZE)
        return AIRLOCK_ERR_PE_BAD_SECTIONS;

    img->mapped_size = img->opt.size_of_image;
    img->mapped = calloc(1, img->mapped_size);
    if (!img->mapped)
        return AIRLOCK_ERR_OUT_OF_MEMORY;

    /* Headers region first. */
    {
        size_t hdr = AIRLOCK_MIN((size_t)img->opt.size_of_headers,
                                img->mapped_size);
        memcpy(img->mapped, img->raw, AIRLOCK_MIN(hdr, img->raw_size));
    }

    for (i = 0; i < img->section_count; i++) {
        airlock_section_view_t *s = &img->sections[i];
        if (s->raw_size == 0 || s->raw_offset >= img->raw_size)
            continue;
        if ((uint64_t)s->raw_offset + s->raw_size > img->raw_size)
            continue; /* source would read past the end of the file */
        if ((uint64_t)s->virtual_address + s->raw_size > img->mapped_size)
            continue; /* malformed; skip to stay safe */
        memcpy(img->mapped + s->virtual_address,
               img->raw + s->raw_offset, s->raw_size);
        s->mapped = img->mapped + s->virtual_address;
    }

    /* Game-performance optimization: with --papi=1, touch every page of the
     * mapped image now so later execution doesn't stall on page faults. */
    if (airlock_perf_options()->papi)
        airlock_prefault(img->mapped, img->mapped_size);

    img->sections_mapped = true;
    return AIRLOCK_OK;
}

/* ---- Imports ------------------------------------------------------------- */

static size_t thunk_size(const airlock_image_t *img)
{
    return img->opt.magic == AIRLOCK_PE_MAGIC_PE32 ? 4 : 8;
}

/*
 * Read a thunk value at an IAT/ILT RVA, honoring the 32/64-bit format. The
 * high bit selects ordinal vs. name imports.
 */
static bool read_thunk(const airlock_image_t *img, uint32_t rva,
                       uint64_t *value, bool *ordinal)
{
    /*
     * Read through the bounds-checked accessor rather than dereferencing the
     * resolved pointer: a thunk sitting in the last bytes of the image would
     * otherwise read past the end of the mapping.
     */
    if (img->opt.magic == AIRLOCK_PE_MAGIC_PE32) {
        uint32_t le;
        uint32_t v;
        if (!airlock_image_read(img, rva, &le, sizeof le))
            return false;
        v = airlock_le32((const uint8_t *)&le);
        *ordinal = (v & 0x80000000u) != 0;
        *value = v & 0x7FFFFFFFu;
    } else {
        uint64_t le;
        uint64_t v;
        if (!airlock_image_read(img, rva, &le, sizeof le))
            return false;
        v = airlock_le64((const uint8_t *)&le);
        *ordinal = (v & 0x8000000000000000ull) != 0;
        *value = v & 0x7FFFFFFFFFFFFFFFull;
    }
    return true;
}

static airlock_status_t parse_imports(airlock_image_t *img)
{
    const airlock_pe_data_directory_t *dir;
    airlock_pe_import_descriptor_t desc;
    uint32_t rva, iat_rva;
    size_t cap = 0, n = 0;
    size_t thunk = thunk_size(img);

    dir = &img->opt.data_directory[AIRLOCK_PE_DIR_IMPORT];
    if (dir->virtual_address == 0 || dir->size == 0)
        return AIRLOCK_OK; /* no imports — fine */

    rva = dir->virtual_address;
    for (;;) {
        uint8_t *p;
        uint32_t iat, thunk_idx;

        if (!airlock_image_read(img, rva, &desc, sizeof desc))
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

        p = airlock_image_rva(img, desc.name_rva);
        if (!p)
            return AIRLOCK_ERR_PE_BAD_IMPORTS;
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
                    fni = airlock_image_rva(img, (uint32_t)val);
                    if (!fni)
                        return AIRLOCK_ERR_PE_BAD_IMPORTS;
                    /* skip 2-byte hint; name follows as ASCII */
                    fn = (const char *)(fni + sizeof(uint16_t));
                }

                if (n == cap) {
                    size_t ncap = cap ? cap * 2 : 16;
                    airlock_import_t *ni = realloc(img->imports,
                                                  ncap * sizeof *ni);
                    if (!ni)
                        return AIRLOCK_ERR_OUT_OF_MEMORY;
                    img->imports = ni;
                    cap = ncap;
                }

                img->imports[n].module = mod;
                img->imports[n].name = fn;
                img->imports[n].ordinal = ord;
                img->imports[n].resolved =
                    airlock_win32_resolve(mod, fn, ord);
                AIRLOCK_TRACE(AIRLOCK_TRACE_DLL,
                             "import %s!%s -> %s",
                             mod, fn ? fn : "#(ordinal)",
                             img->imports[n].resolved ? "ok" : "UNRESOLVED");
                n++;
            }
        }
        rva += sizeof desc;
    }

    img->import_count = n;
    img->imports_parsed = true;
    airlock_perf_count_imports(n);
    return AIRLOCK_OK;
}

/* ---- Exports ------------------------------------------------------------- */

static airlock_status_t parse_exports(airlock_image_t *img)
{
    const airlock_pe_data_directory_t *dir;
    airlock_pe_export_directory_t exp;
    uint32_t *names;
    uint32_t i;

    dir = &img->opt.data_directory[AIRLOCK_PE_DIR_EXPORT];
    if (dir->virtual_address == 0 || dir->size == 0)
        return AIRLOCK_OK;

    if (!airlock_image_read(img, dir->virtual_address, &exp, sizeof exp))
        return AIRLOCK_ERR_PE_BAD_EXPORTS;

    img->export_name_count = exp.number_of_names;
    if (exp.number_of_names == 0)
        return AIRLOCK_OK;

    if (exp.number_of_names > 0x100000) /* sanity */
        return AIRLOCK_ERR_PE_BAD_EXPORTS;

    img->export_names = calloc(exp.number_of_names, sizeof(char *));
    if (!img->export_names)
        return AIRLOCK_ERR_OUT_OF_MEMORY;

    names = calloc(exp.number_of_names, sizeof(uint32_t));
    if (!names) {
        free(img->export_names);
        img->export_names = NULL;
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    }

    if (!airlock_image_read(img, exp.address_of_names, names,
                           exp.number_of_names * sizeof(uint32_t))) {
        free(names);
        return AIRLOCK_ERR_PE_BAD_EXPORTS;
    }

    for (i = 0; i < exp.number_of_names; i++) {
        uint8_t *p = airlock_image_rva(img, names[i]);
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
    return AIRLOCK_OK;
}

/* ---- Base relocations ---------------------------------------------------- */

static airlock_status_t parse_relocs(airlock_image_t *img)
{
    const airlock_pe_data_directory_t *dir;
    uint32_t rva, end;

    dir = &img->opt.data_directory[AIRLOCK_PE_DIR_BASERELOC];
    if (dir->virtual_address == 0 || dir->size == 0)
        return AIRLOCK_OK;

    rva = dir->virtual_address;
    end = rva + dir->size;
    while (rva + sizeof(airlock_pe_base_reloc_t) <= end) {
        airlock_pe_base_reloc_t block;
        uint32_t entries;
        if (!airlock_image_read(img, rva, &block, sizeof block))
            break;
        if (block.size_of_block < sizeof block)
            return AIRLOCK_ERR_PE_BAD_RELOCATIONS;
        entries = (uint32_t)((block.size_of_block - (uint32_t)sizeof block) / 2);
        if (rva + block.size_of_block > end)
            return AIRLOCK_ERR_PE_BAD_RELOCATIONS;
        (void)entries;
        rva += block.size_of_block;
    }

    img->relocs_parsed = true;
    return AIRLOCK_OK;
}

/* ---- Public API ---------------------------------------------------------- */

airlock_status_t airlock_image_load_buffer(const void *data, size_t len,
                                         enum airlock_load_flags flags,
                                         airlock_image_t *out)
{
    airlock_status_t st;
    airlock_image_t img;

    if (!data || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;

    st = airlock_parse_headers((const uint8_t *)data, len, &img);
    if (st != AIRLOCK_OK)
        return st;

    /* Copy the raw buffer so the image owns its bytes. */
    img.raw = malloc(len ? len : 1);
    if (!img.raw) {
        airlock_image_unload(&img);
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    }
    memcpy(img.raw, data, len);
    img.raw_size = len;

    if (flags & AIRLOCK_LOAD_MAP_SECTIONS) {
        st = map_sections(&img);
        if (st != AIRLOCK_OK) {
            airlock_image_unload(&img);
            return st;
        }
    }

    if (flags & AIRLOCK_LOAD_PARSE_IMPORTS) {
        st = parse_imports(&img);
        if (st != AIRLOCK_OK) {
            airlock_image_unload(&img);
            return st;
        }
    }

    if (flags & AIRLOCK_LOAD_PARSE_EXPORTS) {
        st = parse_exports(&img);
        if (st != AIRLOCK_OK) {
            airlock_image_unload(&img);
            return st;
        }
    }

    if (flags & AIRLOCK_LOAD_PARSE_RELOCS) {
        st = parse_relocs(&img);
        if (st != AIRLOCK_OK) {
            airlock_image_unload(&img);
            return st;
        }
    }

    *out = img;
    airlock_perf_count_images();
    if (img.sections_mapped)
        airlock_perf_count_map(img.mapped_size);
    return AIRLOCK_OK;
}

airlock_status_t airlock_image_load_file(const char *path,
                                       enum airlock_load_flags flags,
                                       airlock_image_t *out)
{
    airlock_status_t st;
    uint8_t *buf = NULL;
    size_t len = 0;

    st = read_file_perf(path, &buf, &len);
    if (st != AIRLOCK_OK)
        return st;

    st = airlock_image_load_buffer(buf, len, flags, out);
    free(buf);
    return st;
}

void airlock_image_unload(airlock_image_t *img)
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
