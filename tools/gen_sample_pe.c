/*
 * gen_sample_pe.c — writes a minimal, valid PE32 executable to disk.
 *
 * Cellar needs Windows executables to load, but building one on Linux usually
 * requires MinGW. This tiny generator synthesizes a self-contained PE that
 * imports ExitProcess/GetStdHandle from KERNEL32.dll and exports a couple of
 * symbols, so you can exercise the loader without any Windows toolchain.
 *
 *   make sample    ->  writes samples/hello.exe
 *   cellar samples/hello.exe
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEC_VA   0x1000
#define RAW_OFF  0x200
#define RAW_SIZE 0x500
#define FO(rva)  (RAW_OFF + ((rva) - SEC_VA))

static void p16(unsigned char *b, size_t off, unsigned v)
{
    b[off] = (unsigned char)(v & 0xFF);
    b[off + 1] = (unsigned char)((v >> 8) & 0xFF);
}

static void p32(unsigned char *b, size_t off, unsigned long v)
{
    size_t i;
    for (i = 0; i < 4; i++)
        b[off + i] = (unsigned char)((v >> (8 * i)) & 0xFF);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "samples/hello.exe";
    size_t opt_size = 224;               /* IMAGE_OPTIONAL_HEADER32 */
    size_t section_off = 0x58 + opt_size;
    size_t total = RAW_OFF + RAW_SIZE;
    size_t i;
    unsigned char *b;
    FILE *f;

    b = calloc(1, total);
    if (!b)
        return 1;

    /* DOS header. */
    p16(b, 0x00, 0x5A4D);            /* 'MZ' */
    p32(b, 0x3C, 0x40);              /* e_lfanew */

    /* NT signature + COFF header. */
    p32(b, 0x40, 0x00004550);        /* 'PE\0\0' */
    p16(b, 0x44, 0x014C);            /* machine = x86 */
    p16(b, 0x46, 1);                 /* 1 section */
    p16(b, 0x54, (unsigned short)opt_size); /* size_of_optional_header */
    p16(b, 0x56, 0x0002);            /* characteristics = EXECUTABLE_IMAGE */

    /* Optional header. */
    p16(b, 0x58, 0x010B);
    p32(b, 0x68, SEC_VA);            /* address_of_entry_point */
    p32(b, 0x74, 0x00400000);        /* image_base */
    p32(b, 0x7C, 0x1000);            /* section_alignment */
    p32(b, 0x80, 0x200);             /* file_alignment */
    p16(b, 0x8C, 6);                 /* subsystem_version_major */
    p32(b, 0x94, 0x2000);            /* size_of_image */
    p32(b, 0x98, 0x200);             /* size_of_headers */
    p16(b, 0xA0, 3);                 /* subsystem = windows CUI */
    p32(b, 0xC8, 16);                /* number_of_rva_and_sizes */

    /* Data directories (export idx 0, import idx 1). */
    p32(b, 0xCC + 0 + 0, 0x1100);    /* export  RVA */
    p32(b, 0xCC + 0 + 4, 0x80);
    p32(b, 0xCC + 8 + 0, 0x1000);    /* import  RVA */
    p32(b, 0xCC + 8 + 4, 0x40);

    /* Section table. */
    memcpy(b + section_off, ".text", 6);
    p32(b, section_off + 8,  0x1000);
    p32(b, section_off + 12, SEC_VA);
    p32(b, section_off + 16, RAW_SIZE);
    p32(b, section_off + 20, RAW_OFF);
    p32(b, section_off + 36, 0x60000020); /* code|exec|read */

    /* Import descriptor. */
    p32(b, FO(0x1000), 0);
    p32(b, FO(0x1004), 0);
    p32(b, FO(0x1008), 0);
    p32(b, FO(0x100C), 0x1060);
    p32(b, FO(0x1010), 0x1030);
    for (i = 0; i < 5; i++)
        p32(b, FO(0x1014) + i * 4, 0);   /* terminator */
    p32(b, FO(0x1030), 0x1040);
    p32(b, FO(0x1034), 0x1050);
    p32(b, FO(0x1038), 0);
    p16(b, FO(0x1040), 0);
    memcpy(b + FO(0x1042), "ExitProcess", 12);
    p16(b, FO(0x1050), 0);
    memcpy(b + FO(0x1052), "GetStdHandle", 13);
    memcpy(b + FO(0x1060), "KERNEL32.dll", 13);

    /* Export directory. */
    p32(b, FO(0x1100), 0);
    p32(b, FO(0x1104), 0);
    p16(b, FO(0x1108), 0);
    p16(b, FO(0x110A), 0);
    p32(b, FO(0x110C), 0x1180);
    p32(b, FO(0x1110), 1);
    p32(b, FO(0x1114), 2);
    p32(b, FO(0x1118), 2);
    p32(b, FO(0x111C), 0x1190);
    p32(b, FO(0x1120), 0x11A0);
    p32(b, FO(0x1124), 0x11B0);
    memcpy(b + FO(0x1180), "mytest.dll", 11);
    p32(b, FO(0x1190), 0x1000);
    p32(b, FO(0x1194), 0x1000);
    p32(b, FO(0x11A0), 0x11C0);
    p32(b, FO(0x11A4), 0x11C6);
    p16(b, FO(0x11B0), 0);
    p16(b, FO(0x11B2), 1);
    memcpy(b + FO(0x11C0), "FnOne", 6);
    memcpy(b + FO(0x11C6), "FnTwo", 6);

    f = fopen(path, "wb");
    if (!f) {
        fprintf(stderr, "cannot open %s for writing\n", path);
        free(b);
        return 1;
    }
    fwrite(b, 1, total, f);
    fclose(f);
    free(b);
    printf("wrote %s (%zu bytes)\n", path, total);
    return 0;
}
