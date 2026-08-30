/*
 * pe.c — PE/COFF parsing: headers, section table, and RVA translation.
 *
 * This file is intentionally free of any Linux-isms: it only looks at a byte
 * buffer and produces the parsed `airlock_image_t` model. Higher-level concerns
 * (virtual-image mapping, import resolution, file I/O) live in loader.c.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdlib.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/loader.h"
#include "airlock/pe.h"

/* ---- Headers & sections -------------------------------------------------- */

airlock_status_t airlock_parse_headers(const uint8_t *data, size_t len,
                                     airlock_image_t *img)
{
    const airlock_pe_dos_header_t *dos;
    const airlock_pe_coff_header_t *coff;
    const airlock_pe_section_t *sec;
    size_t needed;
    size_t nt_off, opt_off, sec_off;
    size_t i;

    if (!data || !img)
        return AIRLOCK_ERR_INVALID_ARGUMENT;

    memset(img, 0, sizeof *img);

    /* --- DOS header ----------------------------------------------------- */
    if (len < sizeof(airlock_pe_dos_header_t))
        return AIRLOCK_ERR_PE_TRUNCATED;
    dos = (const airlock_pe_dos_header_t *)data;
    if (dos->e_magic != AIRLOCK_PE_DOS_SIGNATURE)
        return AIRLOCK_ERR_PE_NOT_PE;
    img->dos = *dos;

    nt_off = dos->e_lfanew;
    if (nt_off > len - sizeof(uint32_t) - sizeof(airlock_pe_coff_header_t))
        return AIRLOCK_ERR_PE_TRUNCATED;

    /* --- NT signature + COFF header ------------------------------------ */
    if (airlock_le32(data + nt_off) != AIRLOCK_PE_NT_SIGNATURE)
        return AIRLOCK_ERR_PE_NOT_PE;

    coff = (const airlock_pe_coff_header_t *)(data + nt_off + 4);
    img->coff = *coff;

    /* --- Optional header ------------------------------------------------ */
    opt_off = nt_off + 4 + sizeof(airlock_pe_coff_header_t);
    needed = opt_off + coff->size_of_optional_header;
    if (needed > len || coff->size_of_optional_header < 2)
        return AIRLOCK_ERR_PE_TRUNCATED;

    memset(&img->opt, 0, sizeof img->opt);
    memcpy(&img->opt, data + opt_off,
           AIRLOCK_MIN((size_t)coff->size_of_optional_header,
                      sizeof img->opt));

    if (img->opt.magic != AIRLOCK_PE_MAGIC_PE32 &&
        img->opt.magic != AIRLOCK_PE_MAGIC_PE32_PLUS)
        return AIRLOCK_ERR_PE_BAD_MAGIC;

    img->is_dll = (img->coff.characteristics & AIRLOCK_PE_F_DLL) != 0;

    /* --- Section table --------------------------------------------------- */
    sec_off = opt_off + coff->size_of_optional_header;
    needed = sec_off + (size_t)coff->number_of_sections *
                       sizeof(airlock_pe_section_t);
    if (needed > len)
        return AIRLOCK_ERR_PE_TRUNCATED;

    img->section_count = coff->number_of_sections;
    if (img->section_count > 0) {
        img->sections = calloc(img->section_count, sizeof *img->sections);
        if (!img->sections)
            return AIRLOCK_ERR_OUT_OF_MEMORY;
    }

    sec = (const airlock_pe_section_t *)(data + sec_off);
    for (i = 0; i < img->section_count; i++) {
        airlock_section_view_t *v = &img->sections[i];
        memcpy(v->name, sec[i].name, 8);
        v->virtual_address = sec[i].virtual_address;
        v->virtual_size    = sec[i].virtual_size;
        v->raw_offset      = sec[i].pointer_to_raw_data;
        v->raw_size        = sec[i].size_of_raw_data;
        v->characteristics = sec[i].characteristics;
    }

    img->raw      = (uint8_t *)(uintptr_t)data;
    img->raw_size = len;
    return AIRLOCK_OK;
}

/* ---- RVA translation ----------------------------------------------------- */

/*
 * Returns the index of the section containing `rva`, or -1. A virtual address
 * is "in" a section when it lies within [virtual_address, virtual_address +
 * max(virtual_size, raw_size)) — Windows uses the larger of the two for
 * validation purposes.
 */
static long section_for_rva(const airlock_image_t *img, uint32_t rva)
{
    size_t i;
    for (i = 0; i < img->section_count; i++) {
        const airlock_section_view_t *s = &img->sections[i];
        uint64_t span = AIRLOCK_MAX((uint64_t)s->virtual_size,
                                   (uint64_t)s->raw_size);
        if (rva >= (uint64_t)s->virtual_address &&
            rva < (uint64_t)s->virtual_address + span)
            return (long)i;
    }
    return -1;
}

uint8_t *airlock_image_rva(const airlock_image_t *img, uint32_t rva)
{
    long idx;
    if (!img || !img->mapped)
        return NULL;
    if (rva < img->opt.size_of_headers)
        return img->mapped + rva;

    idx = section_for_rva(img, rva);
    if (idx < 0)
        return NULL;
    return img->mapped + rva;
}

const uint8_t *airlock_image_rva_raw(const airlock_image_t *img, uint32_t rva)
{
    long idx;
    if (!img || !img->raw)
        return NULL;

    if (rva < img->opt.size_of_headers)
        return (uint8_t *)(uintptr_t)(img->raw + rva);

    idx = section_for_rva(img, rva);
    if (idx < 0)
        return NULL;

    {
        const airlock_section_view_t *s = &img->sections[idx];
        uint32_t delta = rva - s->virtual_address;
        if (delta < s->raw_size)
            return img->raw + s->raw_offset + delta;
    }
    return NULL;
}

bool airlock_image_read(const airlock_image_t *img, uint32_t rva,
                       void *dst, size_t len)
{
    uint8_t *p;
    if (!img || !dst || len == 0)
        return false;
    p = airlock_image_rva(img, rva);
    if (!p)
        return false;
    if (rva + len > img->mapped_size)
        return false;
    memcpy(dst, p, len);
    return true;
}
