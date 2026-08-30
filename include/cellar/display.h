/*
 * display.h — virtual Windows display environment.
 *
 * Multiple monitors, DPI scaling, resolution, refresh rate, HDR, orientation,
 * and window mode (windowed / fullscreen / borderless). This is the model
 * USER32/GDI32 query through GetSystemMetrics / GetDeviceCaps.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_DISPLAY_H
#define CELLAR_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

typedef enum cellar_orientation {
    CELLAR_ORIENT_LANDSCAPE = 0,
    CELLAR_ORIENT_PORTRAIT,
    CELLAR_ORIENT_LANDSCAPE_FLIPPED,
    CELLAR_ORIENT_PORTRAIT_FLIPPED
} cellar_orientation_t;

typedef enum cellar_window_mode {
    CELLAR_WINDOW_WINDOWED = 0,
    CELLAR_WINDOW_FULLSCREEN,
    CELLAR_WINDOW_BORDERLESS
} cellar_window_mode_t;

typedef struct cellar_monitor {
    char     name[32];
    uint32_t width;
    uint32_t height;
    uint32_t dpi;
    uint32_t refresh_hz;
    int      hdr;
    cellar_orientation_t orientation;
    int      primary;
} cellar_monitor_t;

typedef struct cellar_display {
    cellar_monitor_t monitors[8];
    size_t count;
    cellar_window_mode_t mode;
} cellar_display_t;

void cellar_display_init_default(cellar_display_t *d);
cellar_status_t cellar_display_add_monitor(cellar_display_t *d,
                                           const cellar_monitor_t *m);
cellar_status_t cellar_display_set_mode(cellar_display_t *d,
                                        cellar_window_mode_t mode);
const cellar_monitor_t *cellar_display_primary(const cellar_display_t *d);
float cellar_display_scale(const cellar_monitor_t *m); /* dpi / 96 */

/* Process-wide display (lazy-initialized to one 1920×1080 @ 96 DPI). */
cellar_display_t *cellar_display_current(void);

#endif /* CELLAR_DISPLAY_H */
