/*
 * gen_sample_pe.c — writes a minimal, valid PE32 executable to disk.
 *
 * Cellar needs Windows executables to load, but building one on Linux usually
 * requires MinGW. This tiny generator synthesizes a self-contained PE that
 * imports from several system DLLs (KERNEL32, d3d11, XInput, winmm, ws2_32)
 * and exports a couple of symbols, so you can exercise the loader AND the
 * Application Compatibility Analyzer without any Windows toolchain.
 *
 *   make sample        ->  writes samples/hello.exe
 *   cellar samples/hello.exe     (load/report)
 *   cellar analyze samples/hello.exe   (compatibility analysis)
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SEC_VA   0x1000
#define RAW_OFF  0x200
#define RAW_SIZE 0x1000
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

static void name(unsigned char *b, unsigned rva, const char *s)
{
    memcpy(b + FO(rva), s, strlen(s) + 1);
}

static void hintname(unsigned char *b, unsigned rva, const char *s)
{
    p16(b, FO(rva), 0);            /* hint */
    memcpy(b + FO(rva) + 2, s, strlen(s) + 1);
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "samples/hello.exe";
    size_t opt_size = 224;          /* IMAGE_OPTIONAL_HEADER32 */
    size_t section_off = 0x58 + opt_size;
    size_t total = RAW_OFF + RAW_SIZE;
    unsigned char *b;
    FILE *f;

    b = calloc(1, total);
    if (!b)
        return 1;

    /* DOS header. */
    p16(b, 0x00, 0x5A4D);
    p32(b, 0x3C, 0x40);

    /* NT signature + COFF header. */
    p32(b, 0x40, 0x00004550);
    p16(b, 0x44, 0x014C);           /* machine = x86 */
    p16(b, 0x46, 1);                /* 1 section */
    p16(b, 0x54, (unsigned short)opt_size);
    p16(b, 0x56, 0x0002);           /* EXECUTABLE_IMAGE */

    /* Optional header. */
    p16(b, 0x58, 0x010B);
    p32(b, 0x68, SEC_VA);           /* address_of_entry_point */
    p32(b, 0x74, 0x00400000);       /* image_base */
    p32(b, 0x7C, 0x1000);           /* section_alignment */
    p32(b, 0x80, 0x200);            /* file_alignment */
    p16(b, 0x8C, 6);
    p32(b, 0x94, 0x4000);           /* size_of_image */
    p32(b, 0x98, 0x200);            /* size_of_headers */
    p16(b, 0xA0, 3);                /* subsystem = windows CUI */
    p32(b, 0xC8, 16);               /* number_of_rva_and_sizes */
    p32(b, 0xCC + 0 + 0, 0x1200);   /* export  RVA */
    p32(b, 0xCC + 0 + 4, 0x80);
    p32(b, 0xCC + 8 + 0, 0x1000);   /* import  RVA */
    p32(b, 0xCC + 8 + 4, 0x80);

    /* Section table. */
    memcpy(b + section_off, ".text", 6);
    p32(b, section_off + 8,  0x1000);
    p32(b, section_off + 12, SEC_VA);
    p32(b, section_off + 16, RAW_SIZE);
    p32(b, section_off + 20, RAW_OFF);
    p32(b, section_off + 36, 0x60000020);

    /* ---- Import descriptors (6 x 20 bytes at RVA 0x1000) ------------------ */
    /* desc[0] kernel32 */  p32(b, FO(0x1000), 0); p32(b, FO(0x1004), 0);
                            p32(b, FO(0x1008), 0); p32(b, FO(0x100C), 0x1080);
                            p32(b, FO(0x1010), 0x10D0);
    /* desc[1] d3d11 */     p32(b, FO(0x1014), 0); p32(b, FO(0x1018), 0);
                            p32(b, FO(0x101C), 0); p32(b, FO(0x1020), 0x1090);
                            p32(b, FO(0x1024), 0x10E0);
    /* desc[2] xinput */    p32(b, FO(0x1028), 0); p32(b, FO(0x102C), 0);
                            p32(b, FO(0x1030), 0); p32(b, FO(0x1034), 0x10A0);
                            p32(b, FO(0x1038), 0x10F0);
    /* desc[3] winmm */     p32(b, FO(0x103C), 0); p32(b, FO(0x1040), 0);
                            p32(b, FO(0x1044), 0); p32(b, FO(0x1048), 0x10B0);
                            p32(b, FO(0x104C), 0x1100);
    /* desc[4] ws2_32 */    p32(b, FO(0x1050), 0); p32(b, FO(0x1054), 0);
                            p32(b, FO(0x1058), 0); p32(b, FO(0x105C), 0x10C0);
                            p32(b, FO(0x1060), 0x1110);
    /* terminator (20 bytes) */

    /* ---- DLL names --------------------------------------------------------- */
    name(b, 0x1080, "KERNEL32.dll");
    name(b, 0x1090, "d3d11.dll");
    name(b, 0x10A0, "XINPUT1_3.dll");
    name(b, 0x10B0, "winmm.dll");
    name(b, 0x10C0, "ws2_32.dll");

    /* ---- IATs (thunks -> hint/name RVAs) ----------------------------------- */
    p32(b, FO(0x10D0), 0x1120); p32(b, FO(0x10D4), 0x1140); p32(b, FO(0x10D8), 0);
    p32(b, FO(0x10E0), 0x1160); p32(b, FO(0x10E4), 0);
    p32(b, FO(0x10F0), 0x1180); p32(b, FO(0x10F4), 0);
    p32(b, FO(0x1100), 0x11A0); p32(b, FO(0x1104), 0);
    p32(b, FO(0x1110), 0x11C0); p32(b, FO(0x1114), 0);

    /* ---- Hint/name entries (32 bytes apart: hint + name + NUL) ------------- */
    hintname(b, 0x1120, "ExitProcess");
    hintname(b, 0x1140, "GetStdHandle");
    hintname(b, 0x1160, "D3D11CreateDevice");
    hintname(b, 0x1180, "XInputGetState");
    hintname(b, 0x11A0, "waveOutOpen");
    hintname(b, 0x11C0, "WSAStartup");

    /* ---- Export directory (RVA 0x1200) -------------------------------------- */
    p32(b, FO(0x1200), 0); p32(b, FO(0x1204), 0);
    p16(b, FO(0x1208), 0); p16(b, FO(0x120A), 0);
    p32(b, FO(0x120C), 0x1280);         /* name_rva */
    p32(b, FO(0x1210), 1);              /* ordinal_base */
    p32(b, FO(0x1214), 2);              /* number_of_functions */
    p32(b, FO(0x1218), 2);              /* number_of_names */
    p32(b, FO(0x121C), 0x1290);         /* address_of_functions */
    p32(b, FO(0x1220), 0x12A0);         /* address_of_names */
    p32(b, FO(0x1224), 0x12B0);         /* address_of_name_ordinals */
    name(b, 0x1280, "mytest.dll");
    p32(b, FO(0x1290), 0x1000);
    p32(b, FO(0x1294), 0x1000);
    p32(b, FO(0x12A0), 0x12C0);
    p32(b, FO(0x12A4), 0x12C6);
    p16(b, FO(0x12B0), 0);
    p16(b, FO(0x12B2), 1);
    name(b, 0x12C0, "FnOne");
    name(b, 0x12C6, "FnTwo");

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
