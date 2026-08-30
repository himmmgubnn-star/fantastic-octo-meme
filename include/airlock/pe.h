/*
 * pe.h — clean-room Portable Executable (PE/COFF) format definitions.
 *
 * These structs mirror the layouts published in the Microsoft PE format
 * specification (and observed in real binaries), so a Airlock loader can map
 * a raw file buffer directly onto them. They are declared *packed* to keep
 * the on-disk layout stable regardless of host ABI, and every scalar is
 * little-endian on disk.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_PE_H
#define AIRLOCK_PE_H

#include <stdint.h>

/* ---- Host/compiler-independent packing ---------------------------------- */
#if defined(_MSC_VER)
#define AIRLOCK_PE_PACKED_BEGIN __pragma(pack(push, 1))
#define AIRLOCK_PE_PACKED_END   __pragma(pack(pop))
#else
#define AIRLOCK_PE_PACKED_BEGIN
#define AIRLOCK_PE_PACKED_END
#define AIRLOCK_PE_PACKED __attribute__((packed))
#endif

#define AIRLOCK_PE_DOS_SIGNATURE  0x5A4Du      /* "MZ"  */
#define AIRLOCK_PE_NT_SIGNATURE   0x00004550u  /* "PE\0\0" */

#define AIRLOCK_PE_MAGIC_PE32     0x10bu
#define AIRLOCK_PE_MAGIC_PE32_PLUS 0x20bu

/* ---- IMAGE_FILE_HEADER.Characteristics ----------------------------------- */
#define AIRLOCK_PE_F_DLL 0x2000u

/* ---- IMAGE_SECTION_HEADER.Characteristics -------------------------------- */
#define AIRLOCK_PE_SCN_CNT_CODE   0x00000020u
#define AIRLOCK_PE_SCN_CNT_INIT   0x00000040u
#define AIRLOCK_PE_SCN_CNT_UNINIT 0x00000080u
#define AIRLOCK_PE_SCN_MEM_EXECUTE 0x20000000u
#define AIRLOCK_PE_SCN_MEM_READ   0x40000000u
#define AIRLOCK_PE_SCN_MEM_WRITE  0x80000000u

/* ---- Optional header subsystem ------------------------------------------- */
#define AIRLOCK_PE_SUBSYSTEM_NATIVE 1
#define AIRLOCK_PE_SUBSYSTEM_WINDOWS_GUI 2
#define AIRLOCK_PE_SUBSYSTEM_WINDOWS_CUI 3

/* ---- Data directory indices (entries 11..15 are used by newer tooling) --- */
#define AIRLOCK_PE_DIR_EXPORT    0
#define AIRLOCK_PE_DIR_IMPORT    1
#define AIRLOCK_PE_DIR_RESOURCE  2
#define AIRLOCK_PE_DIR_EXCEPTION 3
#define AIRLOCK_PE_DIR_SECURITY  4
#define AIRLOCK_PE_DIR_BASERELOC 5
#define AIRLOCK_PE_DIR_DEBUG     6
#define AIRLOCK_PE_DIR_ARCH      7
#define AIRLOCK_PE_DIR_GLOBALPTR 8
#define AIRLOCK_PE_DIR_TLS       9
#define AIRLOCK_PE_DIR_LOADCONFIG 10
#define AIRLOCK_PE_DIR_BOUNDIMPORT 11
#define AIRLOCK_PE_DIR_IAT       12
#define AIRLOCK_PE_DIR_DELAYIMPORT 13
#define AIRLOCK_PE_DIR_COM       14
#define AIRLOCK_PE_DIR_RESERVED  15

#define AIRLOCK_PE_DATA_DIR_COUNT 16

AIRLOCK_PE_PACKED_BEGIN

/* IMAGE_DOS_HEADER (first 64 bytes of every PE file). Only the fields Airlock
 * needs are declared; e_lfanew at offset 0x3C locates the NT headers. */
typedef struct airlock_pe_dos_header {
    uint16_t e_magic;     /* 0x00 'MZ' */
    uint16_t e_cblp;
    uint16_t e_cp;
    uint16_t e_crlc;
    uint16_t e_cparhdr;
    uint16_t e_minalloc;
    uint16_t e_maxalloc;
    uint16_t e_ss;
    uint16_t e_sp;
    uint16_t e_csum;
    uint16_t e_ip;
    uint16_t e_cs;
    uint16_t e_lfarlc;
    uint16_t e_ovno;
    uint16_t e_res[4];
    uint16_t e_oemid;
    uint16_t e_oeminfo;
    uint16_t e_res2[10];
    uint32_t e_lfanew;   /* 0x3C */
} AIRLOCK_PE_PACKED airlock_pe_dos_header_t;

/* IMAGE_FILE_HEADER (20 bytes). */
typedef struct airlock_pe_coff_header {
    uint16_t machine;
    uint16_t number_of_sections;
    uint32_t time_date_stamp;
    uint32_t pointer_to_symbol_table;
    uint32_t number_of_symbols;
    uint16_t size_of_optional_header;
    uint16_t characteristics;
} AIRLOCK_PE_PACKED airlock_pe_coff_header_t;

/* IMAGE_SECTION_HEADER (40 bytes). */
typedef struct airlock_pe_section {
    char     name[8];
    uint32_t virtual_size;
    uint32_t virtual_address;
    uint32_t size_of_raw_data;
    uint32_t pointer_to_raw_data;
    uint32_t pointer_to_relocations;
    uint32_t pointer_to_line_numbers;
    uint16_t number_of_relocations;
    uint16_t number_of_line_numbers;
    uint32_t characteristics;
} AIRLOCK_PE_PACKED airlock_pe_section_t;

/* IMAGE_DATA_DIRECTORY. */
typedef struct airlock_pe_data_directory {
    uint32_t virtual_address;
    uint32_t size;
} AIRLOCK_PE_PACKED airlock_pe_data_directory_t;

/* IMAGE_OPTIONAL_HEADER — union of the 32-bit and 64-bit variants. */
typedef struct airlock_pe_optional_header {
    uint16_t magic;
    uint8_t  linker_major;
    uint8_t  linker_minor;
    uint32_t size_of_code;
    uint32_t size_of_initialized_data;
    uint32_t size_of_uninitialized_data;
    uint32_t address_of_entry_point;
    uint32_t base_of_code;
    uint32_t base_of_data;            /* 0 for PE32+ */
    uint64_t image_base;
    uint32_t section_alignment;
    uint32_t file_alignment;
    uint16_t os_version_major;
    uint16_t os_version_minor;
    uint16_t image_version_major;
    uint16_t image_version_minor;
    uint16_t subsystem_version_major;
    uint16_t subsystem_version_minor;
    uint32_t win32_version_value;
    uint32_t size_of_image;
    uint32_t size_of_headers;
    uint32_t checksum;
    uint16_t subsystem;
    uint16_t dll_characteristics;
    uint64_t size_of_stack_reserve;
    uint64_t size_of_stack_commit;
    uint64_t size_of_heap_reserve;
    uint64_t size_of_heap_commit;
    uint32_t loader_flags;
    uint32_t number_of_rva_and_sizes;
    airlock_pe_data_directory_t data_directory[AIRLOCK_PE_DATA_DIR_COUNT];
} AIRLOCK_PE_PACKED airlock_pe_optional_header_t;

/* IMAGE_IMPORT_DESCRIPTOR (20 bytes; all-zero entry terminates the array). */
typedef struct airlock_pe_import_descriptor {
    uint32_t characteristics;      /* 0 for ILT-based binding; else RVA of ILT */
    uint32_t time_date_stamp;
    uint32_t forwarder_chain;
    uint32_t name_rva;             /* RVA of the ASCII DLL name, e.g. "KERNEL32.dll" */
    uint32_t first_thunk;          /* RVA of the IAT (Import Address Table) */
} AIRLOCK_PE_PACKED airlock_pe_import_descriptor_t;

/* IMAGE_THUNK_DATA — a single import thunk. Which field is live depends on the
 * high bit of the value (ordinal vs. name import). */
typedef union airlock_pe_thunk_data {
    uint64_t address_of_data;      /* value when ordinal-imported */
    struct {
        uint32_t offset;           /* RVA of IMAGE_IMPORT_BY_NAME */
        uint32_t flags;
    } name_import;
    uint32_t ordinal;
} AIRLOCK_PE_PACKED airlock_pe_thunk_data_t;

/* IMAGE_IMPORT_BY_NAME — pointed to by a name-import thunk. */
typedef struct airlock_pe_import_by_name {
    uint16_t hint;
    /* ASCII function name follows immediately. */
} AIRLOCK_PE_PACKED airlock_pe_import_by_name_t;

/* IMAGE_EXPORT_DIRECTORY (40 bytes). */
typedef struct airlock_pe_export_directory {
    uint32_t characteristics;
    uint32_t time_date_stamp;
    uint16_t major_version;
    uint16_t minor_version;
    uint32_t name_rva;
    uint32_t ordinal_base;
    uint32_t number_of_functions;
    uint32_t number_of_names;
    uint32_t address_of_functions;    /* RVA of EAT  */
    uint32_t address_of_names;        /* RVA of ENT  */
    uint32_t address_of_name_ordinals;/* RVA of EOT  */
} AIRLOCK_PE_PACKED airlock_pe_export_directory_t;

/* IMAGE_BASE_RELOCATION block header (each entry: type:4 | offset:12). */
typedef struct airlock_pe_base_reloc {
    uint32_t virtual_address;
    uint32_t size_of_block;
} AIRLOCK_PE_PACKED airlock_pe_base_reloc_t;

#define AIRLOCK_PE_RELOC_ABS0    0
#define AIRLOCK_PE_RELOC_HIGHLOW 3
#define AIRLOCK_PE_RELOC_DIR64   10

AIRLOCK_PE_PACKED_END

#endif /* AIRLOCK_PE_H */
