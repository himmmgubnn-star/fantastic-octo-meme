/*
 * test_loader.c — unit tests for the Cellar PE loader.
 *
 * Uses a tiny hand-built PE32 executable synthesized in memory so the test
 * has no external fixture dependencies. Exercises header parsing, section
 * mapping, RVA translation, import resolution, and export indexing, plus a
 * few negative cases.
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

static int g_failures = 0;

#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } \
    } while (0)

/* ---- synthetic PE32 builder ---------------------------------------------- */

struct pe_buf {
    uint8_t *p;
    size_t   cap;
};

static void bput16(struct pe_buf *b, size_t off, uint16_t v)
{
    b->p[off] = (uint8_t)(v & 0xFF);
    b->p[off + 1] = (uint8_t)((v >> 8) & 0xFF);
}

static void bput32(struct pe_buf *b, size_t off, uint32_t v)
{
    size_t i;
    for (i = 0; i < 4; i++)
        b->p[off + i] = (uint8_t)((v >> (8 * i)) & 0xFF);
}

/* Offsets in the synthesized PE. */
#define OFF_DOS      0x000
#define OFF_NT       0x040
#define OFF_COFF     (OFF_NT + 4)
#define OFF_OPT      (OFF_COFF + 20)
#define SEC_VA       0x1000
#define RAW_OFF      0x200
#define RAW_SIZE     0x500
#define IMG_BASE     0x00400000

static size_t build_test_pe(struct pe_buf *b)
{
    size_t opt_size = sizeof(cellar_pe_optional_header_t);
    size_t section_off = OFF_OPT + opt_size;
    size_t total = RAW_OFF + RAW_SIZE;
    size_t i;

    b->cap = total;
    b->p = calloc(1, total);
    if (!b->p)
        return 0;

    /* DOS header. */
    bput16(b, OFF_DOS + 0x00, 0x5A4D);          /* 'MZ' */
    bput32(b, OFF_DOS + 0x3C, OFF_NT);          /* e_lfanew */

    /* NT signature + COFF header. */
    bput32(b, OFF_NT, 0x00004550);              /* 'PE\0\0' */
    bput16(b, OFF_COFF + 0x00, 0x014C);         /* machine = x86 */
    bput16(b, OFF_COFF + 0x02, 1);              /* 1 section */
    bput16(b, OFF_COFF + 0x10, (uint16_t)opt_size);
    bput16(b, OFF_COFF + 0x12, 0x0002);         /* characteristics: EXECUTABLE_IMAGE */

    /* Optional header (offsets per IMAGE_OPTIONAL_HEADER32). */
    bput16(b, OFF_OPT + 0x00, 0x010B);          /* magic = PE32 */
    bput32(b, OFF_OPT + 0x10, SEC_VA);          /* address_of_entry_point */
    bput32(b, OFF_OPT + 0x1C, (uint32_t)(IMG_BASE & 0xFFFFFFFF)); /* image_base */
    bput32(b, OFF_OPT + 0x24, 0x1000);          /* section_alignment */
    bput32(b, OFF_OPT + 0x28, 0x200);           /* file_alignment */
    bput16(b, OFF_OPT + 0x34, 6);               /* subsystem_version_major */
    bput32(b, OFF_OPT + 0x3C, 0x2000);          /* size_of_image */
    bput32(b, OFF_OPT + 0x40, 0x200);           /* size_of_headers */
    bput16(b, OFF_OPT + 0x48, 3);               /* subsystem = windows CUI */
    bput32(b, OFF_OPT + 0x70, 16);              /* number_of_rva_and_sizes */

    /* Data directory: import (index 1) at RVA 0x1000, export (index 0) at 0x1100. */
    {
        size_t dd = OFF_OPT + 0x74; /* data_directory base within struct */
        bput32(b, dd + CELLAR_PE_DIR_EXPORT * 8 + 0, 0x1100);
        bput32(b, dd + CELLAR_PE_DIR_EXPORT * 8 + 4, 0x80);
        bput32(b, dd + CELLAR_PE_DIR_IMPORT * 8 + 0, 0x1000);
        bput32(b, dd + CELLAR_PE_DIR_IMPORT * 8 + 4, 0x40);
    }

    /* Section table (1 entry). */
    memcpy(b->p + section_off, ".text", 6);
    bput32(b, section_off + 8,  0x1000);        /* virtual_size */
    bput32(b, section_off + 12, SEC_VA);        /* virtual_address */
    bput32(b, section_off + 16, RAW_SIZE);      /* size_of_raw_data */
    bput32(b, section_off + 20, RAW_OFF);       /* pointer_to_raw_data */
    bput32(b, section_off + 36, 0x60000020);    /* code|exec|read */

    /* ---- raw data, RVA 0x1000 maps to file offset RAW_OFF ---------------- */
    #define FO(rva) (RAW_OFF + ((rva) - SEC_VA))

    /* Import descriptor #1 (RVA 0x1000). */
    bput32(b, FO(0x1000), 0);                   /* characteristics */
    bput32(b, FO(0x1004), 0);                   /* time_date_stamp */
    bput32(b, FO(0x1008), 0);                   /* forwarder_chain */
    bput32(b, FO(0x100C), 0x1060);              /* name_rva -> "KERNEL32.dll" */
    bput32(b, FO(0x1010), 0x1030);              /* first_thunk -> IAT */
    for (i = 0; i < 5; i++)                     /* terminator descriptor (20B) */
        bput32(b, FO(0x1014) + i * 4, 0);

    bput32(b, FO(0x1030), 0x1040);              /* IAT[0] -> ExitProcess BY_NAME */
    bput32(b, FO(0x1034), 0x1050);              /* IAT[1] -> GetStdHandle BY_NAME */
    bput32(b, FO(0x1038), 0);                   /* IAT[2] terminator */

    bput16(b, FO(0x1040), 0);                   /* hint */
    memcpy(b->p + FO(0x1042), "ExitProcess", 12);
    bput16(b, FO(0x1050), 0);                   /* hint */
    memcpy(b->p + FO(0x1052), "GetStdHandle", 13);

    memcpy(b->p + FO(0x1060), "KERNEL32.dll", 13);

    /* Export directory (RVA 0x1100). */
    bput32(b, FO(0x1100), 0);                   /* characteristics */
    bput32(b, FO(0x1104), 0);                   /* time_date_stamp */
    bput16(b, FO(0x1108), 0);                   /* major */
    bput16(b, FO(0x110A), 0);                   /* minor */
    bput32(b, FO(0x110C), 0x1180);              /* name_rva */
    bput32(b, FO(0x1110), 1);                   /* ordinal_base */
    bput32(b, FO(0x1114), 2);                   /* number_of_functions */
    bput32(b, FO(0x1118), 2);                   /* number_of_names */
    bput32(b, FO(0x111C), 0x1190);              /* address_of_functions */
    bput32(b, FO(0x1120), 0x11A0);              /* address_of_names */
    bput32(b, FO(0x1124), 0x11B0);              /* address_of_name_ordinals */

    memcpy(b->p + FO(0x1180), "mytest.dll", 11);
    bput32(b, FO(0x1190), 0x1000);              /* EAT[0] */
    bput32(b, FO(0x1194), 0x1000);              /* EAT[1] */
    bput32(b, FO(0x11A0), 0x11C0);              /* ENT[0] -> "FnOne" */
    bput32(b, FO(0x11A4), 0x11C6);              /* ENT[1] -> "FnTwo" */
    bput16(b, FO(0x11B0), 0);                   /* EOT[0] */
    bput16(b, FO(0x11B2), 1);                   /* EOT[1] */
    memcpy(b->p + FO(0x11C0), "FnOne", 6);
    memcpy(b->p + FO(0x11C6), "FnTwo", 6);

    return total;
}

/* ---- tests --------------------------------------------------------------- */

static void test_load_and_resolve(void)
{
    struct pe_buf b = {0};
    cellar_image_t img;
    cellar_status_t st;
    size_t total = build_test_pe(&b);
    size_t i;
    int found_exit = 0, found_std = 0, found_other = 0;

    CHECK(total > 0, "build_test_pe returned nonzero size");

    st = cellar_image_load_buffer(b.p, total,
                                  CELLAR_LOAD_DEFAULT |
                                  CELLAR_LOAD_PARSE_EXPORTS, &img);
    CHECK(st == CELLAR_OK, "load_buffer succeeds");
    if (st != CELLAR_OK) {
        free(b.p);
        return;
    }

    CHECK(img.is_dll == false, "synthetic image is an executable");
    CHECK(img.section_count == 1, "one section parsed");
    CHECK(img.sections_mapped, "sections mapped");
    CHECK(img.imports_parsed, "imports parsed");
    CHECK(img.exports_parsed, "exports parsed");
    CHECK(img.import_count == 2, "two imports parsed");
    CHECK(img.export_name_count == 2, "two exports parsed");

    /* RVA translation. */
    CHECK(cellar_image_rva(&img, 0x1000) == img.mapped + 0x1000,
          "RVA maps into mapped image");
    CHECK(cellar_image_rva(&img, 0x1000 + 0x100) == img.mapped + 0x1100,
          "RVA in section maps correctly");
    CHECK(cellar_image_rva(&img, 0x100000) == NULL,
          "RVA outside image returns NULL");

    /* Import bindings. */
    for (i = 0; i < img.import_count; i++) {
        const cellar_import_t *im = &img.imports[i];
        if (im->name && strcmp(im->name, "ExitProcess") == 0) {
            found_exit = 1;
            CHECK(im->resolved != NULL, "ExitProcess resolves to a stub");
        } else if (im->name && strcmp(im->name, "GetStdHandle") == 0) {
            found_std = 1;
            CHECK(im->resolved != NULL, "GetStdHandle resolves to a stub");
        } else {
            found_other = 1;
        }
    }
    CHECK(found_exit, "ExitProcess import found");
    CHECK(found_std, "GetStdHandle import found");
    CHECK(!found_other, "no unexpected imports");

    /* Export names. */
    CHECK(strcmp(img.export_names[0], "FnOne") == 0,
          "first export name is FnOne");
    CHECK(strcmp(img.export_names[1], "FnTwo") == 0,
          "second export name is FnTwo");

    cellar_image_unload(&img);
    free(b.p);
}

static void test_negative_cases(void)
{
    cellar_image_t img;
    cellar_status_t st;
    uint8_t garbage[256];

    /* Not a PE at all. */
    memset(garbage, 0x41, sizeof garbage);
    st = cellar_image_load_buffer(garbage, sizeof garbage,
                                  CELLAR_LOAD_DEFAULT, &img);
    CHECK(st == CELLAR_ERR_PE_NOT_PE, "garbage is rejected as PE_NOT_PE");

    /* Truncated buffer. */
    st = cellar_image_load_buffer(garbage, 4, CELLAR_LOAD_DEFAULT, &img);
    CHECK(st == CELLAR_ERR_PE_TRUNCATED, "short buffer is TRUNCATED");

    /* NULL arguments. */
    st = cellar_image_load_buffer(NULL, 10, CELLAR_LOAD_DEFAULT, &img);
    CHECK(st == CELLAR_ERR_INVALID_ARGUMENT, "NULL buffer rejected");
}

static void test_status_strings(void)
{
    CHECK(strcmp(cellar_status_string(CELLAR_OK), "ok") == 0,
          "status string for OK");
    CHECK(strcmp(cellar_status_string(CELLAR_ERR_PE_NOT_PE),
                 "not a PE image (bad MZ/PE signature)") == 0,
          "status string for PE_NOT_PE");
    CHECK(cellar_status_string((cellar_status_t)9999) != NULL,
          "unknown status returns a string");
}

int main(void)
{
    cellar_win32_init();

    test_load_and_resolve();
    test_negative_cases();
    test_status_strings();

    if (g_failures == 0) {
        printf("test_loader: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_loader: %d test(s) failed\n", g_failures);
    return 1;
}
