/*
 * init.c — Win32 layer bootstrap: registers every built-in module.
 *
 * Modules are independent (kernel32, user32, advapi32, shell32, ole32,
 * comdlg32, gdi32, ws2_32, version, ntdll, winmm) so the API surface can
 * grow one DLL at a time.
 *
 * SPDX-License-Identifier: MIT
 */
#include "airlock/airlock.h"
#include "airlock/win32.h"

const airlock_module_t *airlock_win32_module_kernel32(void);
const airlock_module_t *airlock_win32_module_winmm(void);
const airlock_module_t *airlock_win32_module_user32(void);
const airlock_module_t *airlock_win32_module_advapi32(void);
const airlock_module_t *airlock_win32_module_shell32(void);
const airlock_module_t *airlock_win32_module_ole32(void);
const airlock_module_t *airlock_win32_module_comdlg32(void);
const airlock_module_t *airlock_win32_module_gdi32(void);
const airlock_module_t *airlock_win32_module_ws2_32(void);
const airlock_module_t *airlock_win32_module_version(void);
const airlock_module_t *airlock_win32_module_ntdll(void);

void airlock_win32_init(void)
{
    airlock_win32_register_module(airlock_win32_module_kernel32());
    airlock_win32_register_module(airlock_win32_module_ntdll());
    airlock_win32_register_module(airlock_win32_module_user32());
    airlock_win32_register_module(airlock_win32_module_advapi32());
    airlock_win32_register_module(airlock_win32_module_shell32());
    airlock_win32_register_module(airlock_win32_module_ole32());
    airlock_win32_register_module(airlock_win32_module_comdlg32());
    airlock_win32_register_module(airlock_win32_module_gdi32());
    airlock_win32_register_module(airlock_win32_module_ws2_32());
    airlock_win32_register_module(airlock_win32_module_version());
    airlock_win32_register_module(airlock_win32_module_winmm());
}
