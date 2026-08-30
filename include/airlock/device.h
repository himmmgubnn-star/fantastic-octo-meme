/*
 * device.h — Windows device compatibility layer.
 *
 * Webcams, microphones, USB, Bluetooth, controllers and printers as a
 * registry of devices backed by Linux subsystems. Airlock does not talk to
 * hardware directly here — it records attachment so Win32 APIs (XInput,
 * DirectShow, SetupAPI) have something to enumerate.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_DEVICE_H
#define AIRLOCK_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

typedef enum airlock_dev_kind {
    AIRLOCK_DEV_WEBCAM = 0,
    AIRLOCK_DEV_MIC,
    AIRLOCK_DEV_USB,
    AIRLOCK_DEV_BLUETOOTH,
    AIRLOCK_DEV_CONTROLLER,
    AIRLOCK_DEV_PRINTER
} airlock_dev_kind_t;

typedef struct airlock_device {
    uint32_t id;
    airlock_dev_kind_t kind;
    char name[64];
    char backend[32];
    int  attached;
} airlock_device_t;

airlock_status_t airlock_device_attach(airlock_dev_kind_t kind, const char *name,
                                     const char *backend, uint32_t *out_id);
airlock_status_t airlock_device_detach(uint32_t id);
const airlock_device_t *airlock_device_get(uint32_t id);
size_t airlock_device_list(airlock_device_t *out, size_t cap);
void airlock_device_reset(void);

#endif /* AIRLOCK_DEVICE_H */
