/*
 * com.h — COM / OLE compatibility infrastructure.
 *
 * IUnknown, GUIDs, reference counting, class registration, apartments, and
 * same-process marshaling. A lot of Windows software (installers, Office,
 * shell extensions, games' overlay/UWP pieces) sits on COM; this is the
 * skeleton those APIs bind to.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_COM_H
#define AIRLOCK_COM_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

typedef struct airlock_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} airlock_guid_t;

int  airlock_guid_eq(const airlock_guid_t *a, const airlock_guid_t *b);
/* Parse "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" or the dash form without braces. */
airlock_status_t airlock_guid_parse(const char *s, airlock_guid_t *out);
void airlock_guid_format(const airlock_guid_t *g, char *dst, size_t n);

extern const airlock_guid_t AIRLOCK_IID_IUNKNOWN;

typedef struct airlock_iunknown airlock_iunknown_t;
typedef struct airlock_iunknown_vtbl {
    int      (*query_interface)(airlock_iunknown_t *self,
                                const airlock_guid_t *iid, void **out);
    uint32_t (*add_ref)(airlock_iunknown_t *self);
    uint32_t (*release)(airlock_iunknown_t *self);
} airlock_iunknown_vtbl_t;

struct airlock_iunknown {
    const airlock_iunknown_vtbl_t *vtbl;
    uint32_t refs;
};

/* Default IUnknown vtable (QueryInterface understands IID_IUnknown only). */
const airlock_iunknown_vtbl_t *airlock_iunknown_vtbl(void);

typedef enum airlock_apt {
    AIRLOCK_APT_MTA = 0,
    AIRLOCK_APT_STA
} airlock_apt_t;

airlock_status_t airlock_com_init(airlock_apt_t apt);
void airlock_com_uninit(void);
int  airlock_com_inited(void);
airlock_apt_t airlock_com_apt(void);

typedef airlock_status_t (*airlock_com_factory_t)(void **out);

airlock_status_t airlock_com_register_class(const airlock_guid_t *clsid,
                                          const char *progid,
                                          airlock_com_factory_t fac);
airlock_status_t airlock_com_create(const airlock_guid_t *clsid,
                                  const airlock_guid_t *iid, void **out);

/* Same-process marshal: magic + pointer. Not NDR; honest about the scope. */
airlock_status_t airlock_com_marshal(airlock_iunknown_t *obj,
                                   uint8_t *buf, size_t cap, size_t *out_len);
airlock_status_t airlock_com_unmarshal(const uint8_t *buf, size_t len,
                                     airlock_iunknown_t **out);

/* Built-in "Airlock.Null" class (IUnknown only). */
extern const airlock_guid_t AIRLOCK_CLSID_NULL;
airlock_status_t airlock_com_register_builtins(void);

#endif /* AIRLOCK_COM_H */
