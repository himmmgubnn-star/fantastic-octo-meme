/*
 * inspect.c — automatic application inspector.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/inspect.h"
#include "airlock/pe.h"

static void basename_of(const char *path, char *dst, size_t n)
{
    const char *b;
    if (!path || !*path) {
        snprintf(dst, n, "%s", "(memory)");
        return;
    }
    b = strrchr(path, '/');
    snprintf(dst, n, "%s", b ? b + 1 : path);
}

static int add_unique_dll(airlock_inspect_t *ins, const char *dll)
{
    size_t i;
    if (!dll || !*dll)
        return 0;
    for (i = 0; i < ins->unique_dll_count; i++)
        if (strcasecmp(ins->unique_dlls[i], dll) == 0)
            return 0;
    if (ins->unique_dll_count >= AIRLOCK_INSPECT_MAX_DLLS)
        return 0;
    snprintf(ins->unique_dlls[ins->unique_dll_count],
             sizeof ins->unique_dlls[0], "%s", dll);
    ins->unique_dll_count++;
    return 1;
}

static int dll_is(const char *dll, const char *pre)
{
    return dll && strncasecmp(dll, pre, strlen(pre)) == 0;
}

static void classify_dll(airlock_inspect_t *ins, const char *dll)
{
    if (dll_is(dll, "d3d12"))      snprintf(ins->graphics, sizeof ins->graphics, "Direct3D 12");
    else if (dll_is(dll, "d3d11")) snprintf(ins->graphics, sizeof ins->graphics, "Direct3D 11");
    else if (dll_is(dll, "d3d9"))  snprintf(ins->graphics, sizeof ins->graphics, "Direct3D 9");
    else if (dll_is(dll, "opengl32")) snprintf(ins->graphics, sizeof ins->graphics, "OpenGL");
    else if (dll_is(dll, "vulkan")) snprintf(ins->graphics, sizeof ins->graphics, "Vulkan");

    if (dll_is(dll, "xaudio2") || dll_is(dll, "dsound"))
        snprintf(ins->audio, sizeof ins->audio, "XAudio2");
    else if (dll_is(dll, "mmdevapi"))
        snprintf(ins->audio, sizeof ins->audio, "WASAPI");
    else if (dll_is(dll, "winmm") && !ins->audio[0])
        snprintf(ins->audio, sizeof ins->audio, "WINMM");

    if (dll_is(dll, "xinput"))
        snprintf(ins->input, sizeof ins->input, "XInput");
    else if (dll_is(dll, "dinput"))
        snprintf(ins->input, sizeof ins->input, "DirectInput");

    if (dll_is(dll, "ws2_32") || dll_is(dll, "wsock32"))
        snprintf(ins->networking, sizeof ins->networking, "Winsock");

    if (dll_is(dll, "mscoree") || dll_is(dll, "clr") || dll_is(dll, "mscorlib")) {
        ins->is_dotnet = 1;
        ins->needs_dotnet = 1;
    }
    if (dll_is(dll, "msvcr") || dll_is(dll, "vcruntime") || dll_is(dll, "ucrtbase") ||
        dll_is(dll, "msvcp"))
        ins->needs_vcruntime = 1;
}

/* Walk the PE resource directory looking for RT_MANIFEST (id 24). Offsets
 * inside the tree are relative to the resource data-directory RVA. */
static void walk_resources(const airlock_image_t *img, uint32_t root,
                           uint32_t off, int depth, int *found_manifest)
{
    uint8_t hdr[16];
    uint16_t named, ids, n, i;
    if (depth > 3 || !found_manifest)
        return;
    if (!airlock_image_read(img, root + off, hdr, sizeof hdr))
        return;
    named = airlock_le16(hdr + 12);
    ids   = airlock_le16(hdr + 14);
    n = (uint16_t)(named + ids);
    if (n > 256)
        n = 256;
    for (i = 0; i < n; i++) {
        uint8_t ent[8];
        uint32_t name, offset;
        if (!airlock_image_read(img, root + off + 16u + (uint32_t)i * 8u,
                               ent, sizeof ent))
            break;
        name   = airlock_le32(ent);
        offset = airlock_le32(ent + 4);
        if (depth == 0 && (name & 0x80000000u) == 0 && (name & 0xFFFFu) == 24u)
            *found_manifest = 1;
        if (offset & 0x80000000u)
            walk_resources(img, root, offset & 0x7FFFFFFFu, depth + 1,
                           found_manifest);
    }
}

airlock_status_t airlock_inspect_image(const airlock_image_t *img,
                                     const char *path,
                                     airlock_inspect_t *out)
{
    airlock_inspect_t ins;
    size_t i;
    const airlock_pe_data_directory_t *dd;

    if (!img || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(&ins, 0, sizeof ins);

    if (path)
        snprintf(ins.path, sizeof ins.path, "%s", path);
    basename_of(path, ins.basename, sizeof ins.basename);

    ins.is_64bit  = (img->opt.magic == AIRLOCK_PE_MAGIC_PE32_PLUS);
    ins.is_dll    = img->is_dll;
    ins.machine   = img->coff.machine;
    ins.subsystem = img->opt.subsystem;
    ins.import_count = img->import_count;
    ins.export_count = img->export_name_count;

    for (i = 0; i < img->import_count; i++) {
        add_unique_dll(&ins, img->imports[i].module);
        classify_dll(&ins, img->imports[i].module);
    }

    dd = img->opt.data_directory;
    ins.tls_rva = dd[AIRLOCK_PE_DIR_TLS].virtual_address;
    ins.tls_size = dd[AIRLOCK_PE_DIR_TLS].size;
    ins.has_tls = (ins.tls_rva != 0 && ins.tls_size != 0);

    ins.resource_rva = dd[AIRLOCK_PE_DIR_RESOURCE].virtual_address;
    ins.resource_size = dd[AIRLOCK_PE_DIR_RESOURCE].size;
    ins.has_resources = (ins.resource_rva != 0 && ins.resource_size != 0);
    if (ins.has_resources)
        walk_resources(img, ins.resource_rva, 0, 0, &ins.has_manifest);

    ins.com_rva = dd[AIRLOCK_PE_DIR_COM].virtual_address;
    ins.com_size = dd[AIRLOCK_PE_DIR_COM].size;
    ins.has_com = (ins.com_rva != 0 && ins.com_size != 0);
    if (ins.has_com) {
        ins.is_dotnet = 1;
        ins.needs_dotnet = 1;
    }

    ins.has_delay_imports = (dd[AIRLOCK_PE_DIR_DELAYIMPORT].virtual_address != 0 &&
                             dd[AIRLOCK_PE_DIR_DELAYIMPORT].size != 0);

    *out = ins;
    return AIRLOCK_OK;
}

static const char *machine_name(uint16_t m)
{
    switch (m) {
    case 0x014c: return "x86";
    case 0x8664: return "x64";
    case 0x01c0: return "ARM";
    case 0xaa64: return "ARM64";
    default:     return "unknown";
    }
}

void airlock_inspect_report(const airlock_inspect_t *ins)
{
    size_t i;
    if (!ins)
        return;
    printf("============================================\n");
    printf("   APPLICATION INSPECTOR — %s\n", ins->basename);
    printf("============================================\n");
    printf("PE architecture:     %s (%s)\n",
           machine_name(ins->machine), ins->is_64bit ? "PE32+" : "PE32");
    printf("Kind:                %s\n", ins->is_dll ? "DLL" : "executable");
    printf("Imported DLLs:       %zu\n", ins->unique_dll_count);
    printf("Imported APIs:       %zu\n", ins->import_count);
    printf("Exported APIs:       %zu\n", ins->export_count);
    printf("Manifest:            %s\n", ins->has_manifest ? "present" : "none");
    printf("TLS:                 %s\n", ins->has_tls ? "yes" : "no");
    printf("Resources:           %s\n", ins->has_resources ? "yes" : "no");
    printf("Delay-load imports:  %s\n", ins->has_delay_imports ? "yes" : "no");
    printf("COM / CLR directory: %s\n", ins->has_com ? "yes" : "no");
    printf(".NET:                %s\n", ins->needs_dotnet ? "Required" : "not required");
    printf("VC Runtime:          %s\n", ins->needs_vcruntime ? "Required" : "not required");
    printf("Graphics:            %s\n", ins->graphics[0] ? ins->graphics : "(none)");
    printf("Audio:               %s\n", ins->audio[0] ? ins->audio : "(none)");
    printf("Input:               %s\n", ins->input[0] ? ins->input : "(none)");
    printf("Networking:          %s\n", ins->networking[0] ? ins->networking : "(none)");
    if (ins->unique_dll_count) {
        printf("\nImported modules:\n");
        for (i = 0; i < ins->unique_dll_count; i++)
            printf("  %s\n", ins->unique_dlls[i]);
    }
    printf("\n");
}
