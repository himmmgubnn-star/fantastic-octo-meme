/*
 * mod_ws2_32.c — WS2_32.dll (Winsock 2).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef unsigned short WORD;

typedef struct cellar_wsadata {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} cellar_wsadata_t;

static int g_wsa;

static int WINAPI cellar_WSAStartup(WORD ver, cellar_wsadata_t *data)
{
    g_wsa++;
    if (data) {
        memset(data, 0, sizeof *data);
        data->wVersion = ver ? ver : 0x0202;
        data->wHighVersion = 0x0202;
        memcpy(data->szDescription, "Cellar Winsock 2.2", 19);
        memcpy(data->szSystemStatus, "Running", 8);
        data->iMaxSockets = 0xFFFF;
    }
    return 0;
}

static int WINAPI cellar_WSACleanup(void)
{
    if (g_wsa > 0)
        g_wsa--;
    return 0;
}

static int WINAPI cellar_socket(int af, int type, int proto)
{
    UNUSED(af); UNUSED(type); UNUSED(proto);
    /* Real POSIX sockets arrive with the execution layer. A dummy fd-like
     * handle keeps programs that probe Winsock from aborting at bind time. */
    return 1;
}

static int WINAPI cellar_closesocket(int s)
{
    UNUSED(s);
    return 0;
}

static unsigned short WINAPI cellar_htons_w(unsigned short x)
{
    return (unsigned short)((x << 8) | (x >> 8));
}

static const cellar_export_entry_t k_exports[] = {
    { "WSAStartup",  (void *)&cellar_WSAStartup },
    { "WSACleanup",  (void *)&cellar_WSACleanup },
    { "socket",      (void *)&cellar_socket },
    { "closesocket", (void *)&cellar_closesocket },
    { "htons",       (void *)&cellar_htons_w },
};

static const cellar_module_t k_mod = {
    "ws2_32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const cellar_module_t *cellar_win32_module_ws2_32(void) { return &k_mod; }
