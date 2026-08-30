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
#ifndef CELLAR_COM_H
#define CELLAR_COM_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

typedef struct cellar_guid {
    uint32_t data1;
    uint16_t data2;
    uint16_t data3;
    uint8_t  data4[8];
} cellar_guid_t;

int  cellar_guid_eq(const cellar_guid_t *a, const cellar_guid_t *b);
/* Parse "{xxxxxxxx-xxxx-xxxx-xxxx-xxxxxxxxxxxx}" or the dash form without braces. */
cellar_status_t cellar_guid_parse(const char *s, cellar_guid_t *out);
void cellar_guid_format(const cellar_guid_t *g, char *dst, size_t n);

extern const cellar_guid_t CELLAR_IID_IUNKNOWN;

typedef struct cellar_iunknown cellar_iunknown_t;
typedef struct cellar_iunknown_vtbl {
    int      (*query_interface)(cellar_iunknown_t *self,
                                const cellar_guid_t *iid, void **out);
    uint32_t (*add_ref)(cellar_iunknown_t *self);
    uint32_t (*release)(cellar_iunknown_t *self);
} cellar_iunknown_vtbl_t;

struct cellar_iunknown {
    const cellar_iunknown_vtbl_t *vtbl;
    uint32_t refs;
};

/* Default IUnknown vtable (QueryInterface understands IID_IUnknown only). */
const cellar_iunknown_vtbl_t *cellar_iunknown_vtbl(void);

typedef enum cellar_apt {
    CELLAR_APT_MTA = 0,
    CELLAR_APT_STA
} cellar_apt_t;

cellar_status_t cellar_com_init(cellar_apt_t apt);
void cellar_com_uninit(void);
int  cellar_com_inited(void);
cellar_apt_t cellar_com_apt(void);

typedef cellar_status_t (*cellar_com_factory_t)(void **out);

cellar_status_t cellar_com_register_class(const cellar_guid_t *clsid,
                                          const char *progid,
                                          cellar_com_factory_t fac);
cellar_status_t cellar_com_create(const cellar_guid_t *clsid,
                                  const cellar_guid_t *iid, void **out);

/* Same-process marshal: magic + pointer. Not NDR; honest about the scope. */
cellar_status_t cellar_com_marshal(cellar_iunknown_t *obj,
                                   uint8_t *buf, size_t cap, size_t *out_len);
cellar_status_t cellar_com_unmarshal(const uint8_t *buf, size_t len,
                                     cellar_iunknown_t **out);

/* Built-in "Cellar.Null" class (IUnknown only). */
extern const cellar_guid_t CELLAR_CLSID_NULL;
cellar_status_t cellar_com_register_builtins(void);

#endif /* CELLAR_COM_H */
