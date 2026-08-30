/*
 * init.c — Win32 layer bootstrap: registers every built-in module.
 *
 * Modules are independent (kernel32, user32, advapi32, shell32, ole32,
 * comdlg32, gdi32, ws2_32, version, ntdll, winmm) so the API surface can
 * grow one DLL at a time.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cellar/cellar.h"
#include "cellar/win32.h"

const cellar_module_t *cellar_win32_module_kernel32(void);
const cellar_module_t *cellar_win32_module_winmm(void);
const cellar_module_t *cellar_win32_module_user32(void);
const cellar_module_t *cellar_win32_module_advapi32(void);
const cellar_module_t *cellar_win32_module_shell32(void);
const cellar_module_t *cellar_win32_module_ole32(void);
const cellar_module_t *cellar_win32_module_comdlg32(void);
const cellar_module_t *cellar_win32_module_gdi32(void);
const cellar_module_t *cellar_win32_module_ws2_32(void);
const cellar_module_t *cellar_win32_module_version(void);
const cellar_module_t *cellar_win32_module_ntdll(void);

void cellar_win32_init(void)
{
    cellar_win32_register_module(cellar_win32_module_kernel32());
    cellar_win32_register_module(cellar_win32_module_ntdll());
    cellar_win32_register_module(cellar_win32_module_user32());
    cellar_win32_register_module(cellar_win32_module_advapi32());
    cellar_win32_register_module(cellar_win32_module_shell32());
    cellar_win32_register_module(cellar_win32_module_ole32());
    cellar_win32_register_module(cellar_win32_module_comdlg32());
    cellar_win32_register_module(cellar_win32_module_gdi32());
    cellar_win32_register_module(cellar_win32_module_ws2_32());
    cellar_win32_register_module(cellar_win32_module_version());
    cellar_win32_register_module(cellar_win32_module_winmm());
}
