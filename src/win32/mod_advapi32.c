/*
 * mod_advapi32.c — ADVAPI32.dll (registry, a slice of the security APIs).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef void *HKEY;
typedef unsigned long DWORD;
typedef unsigned long LONG;
typedef unsigned int REGSAM;

#define ERROR_SUCCESS 0L
#define ERROR_FILE_NOT_FOUND 2L
#define ERROR_INVALID_HANDLE 6L
#define ERROR_MORE_DATA 234L

#define HKEY_CLASSES_ROOT  ((HKEY)(uintptr_t)0x80000000u)
#define HKEY_CURRENT_USER  ((HKEY)(uintptr_t)0x80000001u)
#define HKEY_LOCAL_MACHINE ((HKEY)(uintptr_t)0x80000002u)
#define HKEY_USERS         ((HKEY)(uintptr_t)0x80000003u)

#define REG_SZ 1

#define MAX_REG 128

typedef struct reg_ent {
    int used;
    char path[256];
    char value[256];
} reg_ent_t;

static reg_ent_t g_reg[MAX_REG];

static const char *root_name(HKEY k)
{
    uintptr_t v = (uintptr_t)k;
    if (v == 0x80000000u) return "HKCR";
    if (v == 0x80000001u) return "HKCU";
    if (v == 0x80000002u) return "HKLM";
    if (v == 0x80000003u) return "HKU";
    return NULL;
}

static void make_path(char *dst, size_t n, HKEY root, const char *sub)
{
    const char *r = root_name(root);
    if (r)
        snprintf(dst, n, "%s\\%s", r, sub ? sub : "");
    else if ((uintptr_t)root > 0 && (uintptr_t)root <= MAX_REG) {
        const char *base = g_reg[(uintptr_t)root - 1].path;
        if (sub && *sub)
            snprintf(dst, n, "%s\\%s", base, sub);
        else
            snprintf(dst, n, "%s", base);
    } else {
        snprintf(dst, n, "%s", sub ? sub : "");
    }
}

static LONG WINAPI airlock_RegOpenKeyExA(HKEY hKey, const char *sub,
                                        DWORD options, REGSAM sam, HKEY *out)
{
    char path[256];
    size_t i;
    UNUSED(options); UNUSED(sam);
    if (!out)
        return ERROR_INVALID_HANDLE;
    make_path(path, sizeof path, hKey, sub);
    for (i = 0; i < MAX_REG; i++) {
        if (g_reg[i].used && strcasecmp(g_reg[i].path, path) == 0) {
            *out = (HKEY)(uintptr_t)(i + 1);
            return ERROR_SUCCESS;
        }
    }
    for (i = 0; i < MAX_REG; i++) {
        if (!g_reg[i].used) {
            g_reg[i].used = 1;
            snprintf(g_reg[i].path, sizeof g_reg[i].path, "%s", path);
            g_reg[i].value[0] = '\0';
            *out = (HKEY)(uintptr_t)(i + 1);
            return ERROR_SUCCESS;
        }
    }
    return ERROR_FILE_NOT_FOUND;
}

static LONG WINAPI airlock_RegSetValueExA(HKEY hKey, const char *name,
                                         DWORD reserved, DWORD type,
                                         const unsigned char *data, DWORD cb)
{
    uintptr_t idx = (uintptr_t)hKey;
    UNUSED(reserved); UNUSED(type); UNUSED(cb); UNUSED(name);
    if (idx == 0 || idx > MAX_REG || !g_reg[idx - 1].used)
        return ERROR_INVALID_HANDLE;
    snprintf(g_reg[idx - 1].value, sizeof g_reg[0].value, "%s",
             data ? (const char *)data : "");
    return ERROR_SUCCESS;
}

static LONG WINAPI airlock_RegQueryValueExA(HKEY hKey, const char *name,
                                           DWORD *reserved, DWORD *type,
                                           unsigned char *data, DWORD *cb)
{
    uintptr_t idx = (uintptr_t)hKey;
    size_t n;
    UNUSED(name); UNUSED(reserved);
    if (idx == 0 || idx > MAX_REG || !g_reg[idx - 1].used)
        return ERROR_INVALID_HANDLE;
    n = strlen(g_reg[idx - 1].value) + 1;
    if (type)
        *type = REG_SZ;
    if (!data || !cb)
        return ERROR_SUCCESS;
    if (*cb < n) {
        *cb = (DWORD)n;
        return ERROR_MORE_DATA;
    }
    memcpy(data, g_reg[idx - 1].value, n);
    *cb = (DWORD)n;
    return ERROR_SUCCESS;
}

static LONG WINAPI airlock_RegCloseKey(HKEY hKey)
{
    UNUSED(hKey);
    return ERROR_SUCCESS;
}

static const airlock_export_entry_t k_exports[] = {
    { "RegOpenKeyExA",   (void *)&airlock_RegOpenKeyExA },
    { "RegSetValueExA",  (void *)&airlock_RegSetValueExA },
    { "RegQueryValueExA",(void *)&airlock_RegQueryValueExA },
    { "RegCloseKey",     (void *)&airlock_RegCloseKey },
};

static const airlock_module_t k_mod = {
    "ADVAPI32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const airlock_module_t *airlock_win32_module_advapi32(void) { return &k_mod; }
