/*
 * com.c — COM / OLE infrastructure.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/com.h"

const airlock_guid_t AIRLOCK_IID_IUNKNOWN = {
    0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

const airlock_guid_t AIRLOCK_CLSID_NULL = {
    0x43454c4c, 0x4152, 0x0001, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }
};

int airlock_guid_eq(const airlock_guid_t *a, const airlock_guid_t *b)
{
    return a && b && memcmp(a, b, sizeof *a) == 0;
}

static int hexn(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

static uint32_t hex_run(const char **ps, int nibbles)
{
    uint32_t v = 0;
    int i;
    const char *s = *ps;
    for (i = 0; i < nibbles; i++) {
        int h = hexn(*s++);
        if (h < 0)
            return 0;
        v = (v << 4) | (uint32_t)h;
    }
    *ps = s;
    return v;
}

airlock_status_t airlock_guid_parse(const char *s, airlock_guid_t *out)
{
    const char *p;
    int i;
    if (!s || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    p = s;
    if (*p == '{') p++;
    memset(out, 0, sizeof *out);
    out->data1 = hex_run(&p, 8);
    if (*p == '-') p++;
    out->data2 = (uint16_t)hex_run(&p, 4);
    if (*p == '-') p++;
    out->data3 = (uint16_t)hex_run(&p, 4);
    if (*p == '-') p++;
    for (i = 0; i < 2; i++)
        out->data4[i] = (uint8_t)hex_run(&p, 2);
    if (*p == '-') p++;
    for (i = 2; i < 8; i++)
        out->data4[i] = (uint8_t)hex_run(&p, 2);
    return AIRLOCK_OK;
}

void airlock_guid_format(const airlock_guid_t *g, char *dst, size_t n)
{
    if (!g || !dst || n == 0)
        return;
    snprintf(dst, n,
             "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             g->data1, g->data2, g->data3,
             g->data4[0], g->data4[1], g->data4[2], g->data4[3],
             g->data4[4], g->data4[5], g->data4[6], g->data4[7]);
}

static int iu_qi(airlock_iunknown_t *self, const airlock_guid_t *iid, void **out)
{
    if (!self || !iid || !out)
        return 1;
    if (!airlock_guid_eq(iid, &AIRLOCK_IID_IUNKNOWN))
        return 1;
    self->vtbl->add_ref(self);
    *out = self;
    return 0;
}

static uint32_t iu_addref(airlock_iunknown_t *self)
{
    if (!self)
        return 0;
    self->refs++;
    return self->refs;
}

static uint32_t iu_release(airlock_iunknown_t *self)
{
    if (!self)
        return 0;
    if (self->refs == 0)
        return 0;
    self->refs--;
    if (self->refs == 0) {
        free(self);
        return 0;
    }
    return self->refs;
}

static const airlock_iunknown_vtbl_t k_iu_vtbl = {
    iu_qi, iu_addref, iu_release
};

const airlock_iunknown_vtbl_t *airlock_iunknown_vtbl(void)
{
    return &k_iu_vtbl;
}

static _Thread_local int g_inited;
static _Thread_local airlock_apt_t g_apt;

typedef struct class_ent {
    airlock_guid_t clsid;
    char progid[64];
    airlock_com_factory_t fac;
    int used;
} class_ent_t;

static class_ent_t g_classes[32];

airlock_status_t airlock_com_init(airlock_apt_t apt)
{
    if (g_inited) {
        if (g_apt != apt)
            return AIRLOCK_ERR_INVALID_ARGUMENT; /* RPC_E_CHANGED_MODE */
        return AIRLOCK_OK; /* S_FALSE equivalent: already initialized */
    }
    g_apt = apt;
    g_inited = 1;
    return AIRLOCK_OK;
}

void airlock_com_uninit(void)
{
    g_inited = 0;
}

int airlock_com_inited(void)
{
    return g_inited;
}

airlock_apt_t airlock_com_apt(void)
{
    return g_apt;
}

airlock_status_t airlock_com_register_class(const airlock_guid_t *clsid,
                                          const char *progid,
                                          airlock_com_factory_t fac)
{
    size_t i;
    if (!clsid || !fac)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    for (i = 0; i < 32; i++) {
        if (g_classes[i].used && airlock_guid_eq(&g_classes[i].clsid, clsid)) {
            g_classes[i].fac = fac;
            if (progid)
                snprintf(g_classes[i].progid, sizeof g_classes[i].progid, "%s",
                         progid);
            return AIRLOCK_OK;
        }
    }
    for (i = 0; i < 32; i++) {
        if (!g_classes[i].used) {
            g_classes[i].clsid = *clsid;
            g_classes[i].fac = fac;
            g_classes[i].used = 1;
            snprintf(g_classes[i].progid, sizeof g_classes[i].progid, "%s",
                     progid ? progid : "");
            return AIRLOCK_OK;
        }
    }
    return AIRLOCK_ERR_OUT_OF_MEMORY;
}

airlock_status_t airlock_com_create(const airlock_guid_t *clsid,
                                  const airlock_guid_t *iid, void **out)
{
    size_t i;
    void *obj = NULL;
    airlock_status_t st;
    if (!clsid || !out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (!g_inited)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    *out = NULL;
    for (i = 0; i < 32; i++) {
        if (!g_classes[i].used || !airlock_guid_eq(&g_classes[i].clsid, clsid))
            continue;
        st = g_classes[i].fac(&obj);
        if (st != AIRLOCK_OK)
            return st;
        if (iid && obj) {
            airlock_iunknown_t *iu = (airlock_iunknown_t *)obj;
            void *qi = NULL;
            if (iu->vtbl->query_interface(iu, iid, &qi) != 0) {
                iu->vtbl->release(iu);
                return AIRLOCK_ERR_NOT_IMPLEMENTED;
            }
            *out = qi;
            return AIRLOCK_OK;
        }
        *out = obj;
        return AIRLOCK_OK;
    }
    return AIRLOCK_ERR_NOT_IMPLEMENTED;
}

airlock_status_t airlock_com_marshal(airlock_iunknown_t *obj,
                                   uint8_t *buf, size_t cap, size_t *out_len)
{
    uintptr_t p;
    if (!obj || !buf || cap < 4 + sizeof p)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memcpy(buf, "COM1", 4);
    p = (uintptr_t)obj;
    memcpy(buf + 4, &p, sizeof p);
    if (out_len)
        *out_len = 4 + sizeof p;
    obj->vtbl->add_ref(obj);
    return AIRLOCK_OK;
}

airlock_status_t airlock_com_unmarshal(const uint8_t *buf, size_t len,
                                     airlock_iunknown_t **out)
{
    uintptr_t p = 0;
    if (!buf || !out || len < 4 + sizeof p || memcmp(buf, "COM1", 4) != 0)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memcpy(&p, buf + 4, sizeof p);
    *out = (airlock_iunknown_t *)p;
    return AIRLOCK_OK;
}

static airlock_status_t null_factory(void **out)
{
    airlock_iunknown_t *o = calloc(1, sizeof *o);
    if (!o)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    o->vtbl = airlock_iunknown_vtbl();
    o->refs = 1;
    *out = o;
    return AIRLOCK_OK;
}

airlock_status_t airlock_com_register_builtins(void)
{
    return airlock_com_register_class(&AIRLOCK_CLSID_NULL, "Airlock.Null",
                                     null_factory);
}
