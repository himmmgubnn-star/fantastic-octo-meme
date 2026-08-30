/*
 * init.c — Win32 layer bootstrap: registers every built-in module.
 *
 * SPDX-License-Identifier: MIT
 */
#include "cellar/cellar.h"
#include "cellar/win32.h"

/* One accessor per module (defined in each mod_*.c file). */
const cellar_module_t *cellar_win32_module_kernel32(void);

void cellar_win32_init(void)
{
    cellar_win32_register_module(cellar_win32_module_kernel32());
}
