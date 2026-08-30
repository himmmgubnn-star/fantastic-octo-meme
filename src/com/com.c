/*
 * com.c — COM / OLE infrastructure.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "cellar/cellar.h"
#include "cellar/com.h"

const cellar_guid_t CELLAR_IID_IUNKNOWN = {
    0x00000000, 0x0000, 0x0000, { 0xC0, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x46 }
};

const cellar_guid_t CELLAR_CLSID_NULL = {
    0x43454c4c, 0x4152, 0x0001, { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01 }
};

int cellar_guid_eq(const cellar_guid_t *a, const cellar_guid_t *b)
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

cellar_status_t cellar_guid_parse(const char *s, cellar_guid_t *out)
{
    const char *p;
    int i;
    if (!s || !out)
        return CELLAR_ERR_INVALID_ARGUMENT;
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
    return CELLAR_OK;
}

void cellar_guid_format(const cellar_guid_t *g, char *dst, size_t n)
{
    if (!g || !dst || n == 0)
        return;
    snprintf(dst, n,
             "{%08X-%04X-%04X-%02X%02X-%02X%02X%02X%02X%02X%02X}",
             g->data1, g->data2, g->data3,
             g->data4[0], g->data4[1], g->data4[2], g->data4[3],
             g->data4[4], g->data4[5], g->data4[6], g->data4[7]);
}

static int iu_qi(cellar_iunknown_t *self, const cellar_guid_t *iid, void **out)
{
    if (!self || !iid || !out)
        return 1;
    if (!cellar_guid_eq(iid, &CELLAR_IID_IUNKNOWN))
        return 1;
    self->vtbl->add_ref(self);
    *out = self;
    return 0;
}

static uint32_t iu_addref(cellar_iunknown_t *self)
{
    if (!self)
        return 0;
    self->refs++;
    return self->refs;
}

static uint32_t iu_release(cellar_iunknown_t *self)
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

static const cellar_iunknown_vtbl_t k_iu_vtbl = {
    iu_qi, iu_addref, iu_release
};

const cellar_iunknown_vtbl_t *cellar_iunknown_vtbl(void)
{
    return &k_iu_vtbl;
}

static _Thread_local int g_inited;
static _Thread_local cellar_apt_t g_apt;

typedef struct class_ent {
    cellar_guid_t clsid;
    char progid[64];
    cellar_com_factory_t fac;
    int used;
} class_ent_t;

static class_ent_t g_classes[32];

cellar_status_t cellar_com_init(cellar_apt_t apt)
{
    if (g_inited) {
        if (g_apt != apt)
            return CELLAR_ERR_INVALID_ARGUMENT; /* RPC_E_CHANGED_MODE */
        return CELLAR_OK; /* S_FALSE equivalent: already initialized */
    }
    g_apt = apt;
    g_inited = 1;
    return CELLAR_OK;
}

void cellar_com_uninit(void)
{
    g_inited = 0;
}

int cellar_com_inited(void)
{
    return g_inited;
}

cellar_apt_t cellar_com_apt(void)
{
    return g_apt;
}

cellar_status_t cellar_com_register_class(const cellar_guid_t *clsid,
                                          const char *progid,
                                          cellar_com_factory_t fac)
{
    size_t i;
    if (!clsid || !fac)
        return CELLAR_ERR_INVALID_ARGUMENT;
    for (i = 0; i < 32; i++) {
        if (g_classes[i].used && cellar_guid_eq(&g_classes[i].clsid, clsid)) {
            g_classes[i].fac = fac;
            if (progid)
                snprintf(g_classes[i].progid, sizeof g_classes[i].progid, "%s",
                         progid);
            return CELLAR_OK;
        }
    }
    for (i = 0; i < 32; i++) {
        if (!g_classes[i].used) {
            g_classes[i].clsid = *clsid;
            g_classes[i].fac = fac;
            g_classes[i].used = 1;
            snprintf(g_classes[i].progid, sizeof g_classes[i].progid, "%s",
                     progid ? progid : "");
            return CELLAR_OK;
        }
    }
    return CELLAR_ERR_OUT_OF_MEMORY;
}

cellar_status_t cellar_com_create(const cellar_guid_t *clsid,
                                  const cellar_guid_t *iid, void **out)
{
    size_t i;
    void *obj = NULL;
    cellar_status_t st;
    if (!clsid || !out)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (!g_inited)
        return CELLAR_ERR_INVALID_ARGUMENT;
    *out = NULL;
    for (i = 0; i < 32; i++) {
        if (!g_classes[i].used || !cellar_guid_eq(&g_classes[i].clsid, clsid))
            continue;
        st = g_classes[i].fac(&obj);
        if (st != CELLAR_OK)
            return st;
        if (iid && obj) {
            cellar_iunknown_t *iu = (cellar_iunknown_t *)obj;
            void *qi = NULL;
            if (iu->vtbl->query_interface(iu, iid, &qi) != 0) {
                iu->vtbl->release(iu);
                return CELLAR_ERR_NOT_IMPLEMENTED;
            }
            *out = qi;
            return CELLAR_OK;
        }
        *out = obj;
        return CELLAR_OK;
    }
    return CELLAR_ERR_NOT_IMPLEMENTED;
}

cellar_status_t cellar_com_marshal(cellar_iunknown_t *obj,
                                   uint8_t *buf, size_t cap, size_t *out_len)
{
    uintptr_t p;
    if (!obj || !buf || cap < 4 + sizeof p)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memcpy(buf, "COM1", 4);
    p = (uintptr_t)obj;
    memcpy(buf + 4, &p, sizeof p);
    if (out_len)
        *out_len = 4 + sizeof p;
    obj->vtbl->add_ref(obj);
    return CELLAR_OK;
}

cellar_status_t cellar_com_unmarshal(const uint8_t *buf, size_t len,
                                     cellar_iunknown_t **out)
{
    uintptr_t p = 0;
    if (!buf || !out || len < 4 + sizeof p || memcmp(buf, "COM1", 4) != 0)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memcpy(&p, buf + 4, sizeof p);
    *out = (cellar_iunknown_t *)p;
    return CELLAR_OK;
}

static cellar_status_t null_factory(void **out)
{
    cellar_iunknown_t *o = calloc(1, sizeof *o);
    if (!o)
        return CELLAR_ERR_OUT_OF_MEMORY;
    o->vtbl = cellar_iunknown_vtbl();
    o->refs = 1;
    *out = o;
    return CELLAR_OK;
}

cellar_status_t cellar_com_register_builtins(void)
{
    return cellar_com_register_class(&CELLAR_CLSID_NULL, "Cellar.Null",
                                     null_factory);
}
