/*
 * mod_ws2_32.c — WS2_32.dll (Winsock 2).
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif
#define UNUSED(x) ((void)(x))

typedef unsigned short WORD;

typedef struct airlock_wsadata {
    WORD wVersion;
    WORD wHighVersion;
    char szDescription[257];
    char szSystemStatus[129];
    unsigned short iMaxSockets;
    unsigned short iMaxUdpDg;
    char *lpVendorInfo;
} airlock_wsadata_t;

static int g_wsa;

static int WINAPI airlock_WSAStartup(WORD ver, airlock_wsadata_t *data)
{
    g_wsa++;
    if (data) {
        memset(data, 0, sizeof *data);
        data->wVersion = ver ? ver : 0x0202;
        data->wHighVersion = 0x0202;
        memcpy(data->szDescription, "Airlock Winsock 2.2", 19);
        memcpy(data->szSystemStatus, "Running", 8);
        data->iMaxSockets = 0xFFFF;
    }
    return 0;
}

static int WINAPI airlock_WSACleanup(void)
{
    if (g_wsa > 0)
        g_wsa--;
    return 0;
}

static int WINAPI airlock_socket(int af, int type, int proto)
{
    UNUSED(af); UNUSED(type); UNUSED(proto);
    /* Real POSIX sockets arrive with the execution layer. A dummy fd-like
     * handle keeps programs that probe Winsock from aborting at bind time. */
    return 1;
}

static int WINAPI airlock_closesocket(int s)
{
    UNUSED(s);
    return 0;
}

static unsigned short WINAPI airlock_htons_w(unsigned short x)
{
    return (unsigned short)((x << 8) | (x >> 8));
}

static const airlock_export_entry_t k_exports[] = {
    { "WSAStartup",  (void *)&airlock_WSAStartup },
    { "WSACleanup",  (void *)&airlock_WSACleanup },
    { "socket",      (void *)&airlock_socket },
    { "closesocket", (void *)&airlock_closesocket },
    { "htons",       (void *)&airlock_htons_w },
};

static const airlock_module_t k_mod = {
    "ws2_32.dll", k_exports, sizeof k_exports / sizeof k_exports[0]
};

const airlock_module_t *airlock_win32_module_ws2_32(void) { return &k_mod; }
