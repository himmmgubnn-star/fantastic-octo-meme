/*
 * device.h — Windows device compatibility layer.
 *
 * Webcams, microphones, USB, Bluetooth, controllers and printers as a
 * registry of devices backed by Linux subsystems. Cellar does not talk to
 * hardware directly here — it records attachment so Win32 APIs (XInput,
 * DirectShow, SetupAPI) have something to enumerate.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_DEVICE_H
#define CELLAR_DEVICE_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

typedef enum cellar_dev_kind {
    CELLAR_DEV_WEBCAM = 0,
    CELLAR_DEV_MIC,
    CELLAR_DEV_USB,
    CELLAR_DEV_BLUETOOTH,
    CELLAR_DEV_CONTROLLER,
    CELLAR_DEV_PRINTER
} cellar_dev_kind_t;

typedef struct cellar_device {
    uint32_t id;
    cellar_dev_kind_t kind;
    char name[64];
    char backend[32];
    int  attached;
} cellar_device_t;

cellar_status_t cellar_device_attach(cellar_dev_kind_t kind, const char *name,
                                     const char *backend, uint32_t *out_id);
cellar_status_t cellar_device_detach(uint32_t id);
const cellar_device_t *cellar_device_get(uint32_t id);
size_t cellar_device_list(cellar_device_t *out, size_t cap);
void cellar_device_reset(void);

#endif /* CELLAR_DEVICE_H */
