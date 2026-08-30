/*
 * display.h — virtual Windows display environment.
 *
 * Multiple monitors, DPI scaling, resolution, refresh rate, HDR, orientation,
 * and window mode (windowed / fullscreen / borderless). This is the model
 * USER32/GDI32 query through GetSystemMetrics / GetDeviceCaps.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_DISPLAY_H
#define AIRLOCK_DISPLAY_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

typedef enum airlock_orientation {
    AIRLOCK_ORIENT_LANDSCAPE = 0,
    AIRLOCK_ORIENT_PORTRAIT,
    AIRLOCK_ORIENT_LANDSCAPE_FLIPPED,
    AIRLOCK_ORIENT_PORTRAIT_FLIPPED
} airlock_orientation_t;

typedef enum airlock_window_mode {
    AIRLOCK_WINDOW_WINDOWED = 0,
    AIRLOCK_WINDOW_FULLSCREEN,
    AIRLOCK_WINDOW_BORDERLESS
} airlock_window_mode_t;

typedef struct airlock_monitor {
    char     name[32];
    uint32_t width;
    uint32_t height;
    uint32_t dpi;
    uint32_t refresh_hz;
    int      hdr;
    airlock_orientation_t orientation;
    int      primary;
} airlock_monitor_t;

typedef struct airlock_display {
    airlock_monitor_t monitors[8];
    size_t count;
    airlock_window_mode_t mode;
} airlock_display_t;

void airlock_display_init_default(airlock_display_t *d);
airlock_status_t airlock_display_add_monitor(airlock_display_t *d,
                                           const airlock_monitor_t *m);
airlock_status_t airlock_display_set_mode(airlock_display_t *d,
                                        airlock_window_mode_t mode);
const airlock_monitor_t *airlock_display_primary(const airlock_display_t *d);
float airlock_display_scale(const airlock_monitor_t *m); /* dpi / 96 */

/* Process-wide display (lazy-initialized to one 1920×1080 @ 96 DPI). */
airlock_display_t *airlock_display_current(void);

#endif /* AIRLOCK_DISPLAY_H */
