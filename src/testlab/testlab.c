/*
 * testlab.c — compatibility test lab.
 *
 * Each case verifies one Windows behavior against Cellar's implementation.
 * `cellar_lab_run` also auto-generates an export-presence test for every
 * function currently registered in the Win32 layer, so the lab grows with
 * the API surface.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "cellar/a11y.h"
#include "cellar/cellar.h"
#include "cellar/com.h"
#include "cellar/compat.h"
#include "cellar/desktop.h"
#include "cellar/device.h"
#include "cellar/display.h"
#include "cellar/locale.h"
#include "cellar/notify.h"
#include "cellar/print.h"
#include "cellar/security.h"
#include "cellar/service.h"
#include "cellar/shell.h"
#include "cellar/testlab.h"
#include "cellar/win32.h"

typedef cellar_lab_result_t (*lab_fn)(void);

typedef struct lab_case {
    const char *suite;
    const char *name;
    lab_fn fn;
} lab_case_t;

static cellar_lab_result_t pass_if(int cond)
{
    return cond ? CELLAR_LAB_PASS : CELLAR_LAB_FAIL;
}

static cellar_lab_result_t t_kernel32_exports(void)
{
    return pass_if(cellar_win32_export_exists("KERNEL32.dll", "ExitProcess") &&
                   cellar_win32_export_exists("KERNEL32.dll", "GetLastError"));
}

static cellar_lab_result_t t_user32_messagebox(void)
{
    return pass_if(cellar_win32_export_exists("USER32.dll", "MessageBoxA"));
}

static cellar_lab_result_t t_ntdll_rtlgetversion(void)
{
    return pass_if(cellar_win32_export_exists("ntdll.dll", "RtlGetVersion"));
}

static cellar_lab_result_t t_advapi_reg(void)
{
    return pass_if(cellar_win32_export_exists("ADVAPI32.dll", "RegOpenKeyExA"));
}

static cellar_lab_result_t t_ws2_startup(void)
{
    return pass_if(cellar_win32_export_exists("ws2_32.dll", "WSAStartup"));
}

static cellar_lab_result_t t_ole32_coinit(void)
{
    return pass_if(cellar_win32_export_exists("ole32.dll", "CoInitializeEx"));
}

static cellar_lab_result_t t_shell32_folder(void)
{
    return pass_if(cellar_win32_export_exists("shell32.dll", "SHGetFolderPathA"));
}

static cellar_lab_result_t t_gdi32_caps(void)
{
    return pass_if(cellar_win32_export_exists("gdi32.dll", "GetDeviceCaps"));
}

static cellar_lab_result_t t_version_ex(void)
{
    return pass_if(cellar_win32_export_exists("version.dll", "GetFileVersionInfoSizeA") ||
                   cellar_win32_export_exists("KERNEL32.dll", "GetVersionExA"));
}

static cellar_lab_result_t t_display_primary(void)
{
    const cellar_monitor_t *m = cellar_display_primary(cellar_display_current());
    return pass_if(m && m->width == 1920 && m->height == 1080 &&
                   cellar_display_scale(m) == 1.0f);
}

static cellar_lab_result_t t_locale_cp1252(void)
{
    const uint8_t in[] = { 'A', 0x80 }; /* A + euro */
    char out[16];
    int n = cellar_cp_to_utf8(CELLAR_CP_1252, in, 2, out, sizeof out);
    return pass_if(n > 1 && out[0] == 'A');
}

static cellar_lab_result_t t_locale_number(void)
{
    char buf[32];
    cellar_locale_t l;
    cellar_locale_english_us(&l);
    cellar_locale_set(&l);
    if (cellar_locale_format_number(1234.5, buf, sizeof buf) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    return pass_if(strstr(buf, "1,234") != NULL);
}

static cellar_lab_result_t t_locale_date(void)
{
    struct tm t;
    char buf[32];
    memset(&t, 0, sizeof t);
    t.tm_year = 126; /* 2026 */
    t.tm_mon = 7;
    t.tm_mday = 30;
    if (cellar_locale_format_date(&t, buf, sizeof buf) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    return pass_if(strcmp(buf, "08/30/2026") == 0);
}

static cellar_lab_result_t t_security_acl(void)
{
    cellar_token_t tok;
    cellar_sd_t sd;
    cellar_sid_t other;
    memset(&sd, 0, sizeof sd);
    cellar_token_default(&tok);
    cellar_sid_make(&sd.owner, 5, 1000);
    cellar_sid_make(&other, 5, 1001);
    cellar_acl_add(&sd.dacl, 1, &tok.user, CELLAR_ACCESS_READ);
    if (!cellar_acl_check(&sd, &tok, CELLAR_ACCESS_READ))
        return CELLAR_LAB_FAIL;
    if (cellar_acl_check(&sd, &tok, CELLAR_ACCESS_WRITE))
        return CELLAR_LAB_FAIL;
    cellar_token_impersonate(&tok, &other);
    if (cellar_acl_check(&sd, &tok, CELLAR_ACCESS_READ))
        return CELLAR_LAB_FAIL; /* impersonated as other, no ACE */
    cellar_token_revert(&tok);
    return CELLAR_LAB_PASS;
}

static cellar_lab_result_t t_com_refcount(void)
{
    void *obj = NULL;
    cellar_iunknown_t *iu;
    uint32_t r;
    cellar_com_uninit();
    if (cellar_com_init(CELLAR_APT_MTA) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    cellar_com_register_builtins();
    if (cellar_com_create(&CELLAR_CLSID_NULL, &CELLAR_IID_IUNKNOWN, &obj) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    iu = (cellar_iunknown_t *)obj;
    /* factory AddRef(1) + QueryInterface AddRef(2) */
    r = iu->vtbl->add_ref(iu);
    if (r != 3)
        return CELLAR_LAB_FAIL;
    if (iu->vtbl->release(iu) != 2)
        return CELLAR_LAB_FAIL;
    if (iu->vtbl->release(iu) != 1)
        return CELLAR_LAB_FAIL;
    if (iu->vtbl->release(iu) != 0)
        return CELLAR_LAB_FAIL;
    cellar_com_uninit();
    return CELLAR_LAB_PASS;
}

static cellar_lab_result_t t_com_marshal(void)
{
    void *obj = NULL;
    cellar_iunknown_t *iu, *back = NULL;
    uint8_t buf[32];
    size_t n = 0;
    cellar_com_init(CELLAR_APT_MTA);
    cellar_com_register_builtins();
    if (cellar_com_create(&CELLAR_CLSID_NULL, &CELLAR_IID_IUNKNOWN, &obj) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    iu = (cellar_iunknown_t *)obj;
    if (cellar_com_marshal(iu, buf, sizeof buf, &n) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    if (cellar_com_unmarshal(buf, n, &back) != CELLAR_OK || back != iu)
        return CELLAR_LAB_FAIL;
    iu->vtbl->release(iu); /* marshal AddRef */
    iu->vtbl->release(iu); /* QueryInterface AddRef */
    iu->vtbl->release(iu); /* factory AddRef */
    cellar_com_uninit();
    return CELLAR_LAB_PASS;
}

static cellar_lab_result_t t_guid_parse(void)
{
    cellar_guid_t g;
    char fmt[64];
    if (cellar_guid_parse("{00000000-0000-0000-C000-000000000046}", &g) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    if (!cellar_guid_eq(&g, &CELLAR_IID_IUNKNOWN))
        return CELLAR_LAB_FAIL;
    cellar_guid_format(&g, fmt, sizeof fmt);
    return pass_if(strstr(fmt, "00000000") != NULL);
}

static cellar_lab_result_t t_notify(void)
{
    cellar_notification_t n, hist[4];
    uint32_t id;
    memset(&n, 0, sizeof n);
    snprintf(n.summary, sizeof n.summary, "hello");
    id = cellar_notify_show(&n);
    if (id == 0 || cellar_notify_history(hist, 4) == 0)
        return CELLAR_LAB_FAIL;
    return pass_if(cellar_notify_close(id) == CELLAR_OK);
}

static cellar_lab_result_t t_a11y(void)
{
    uint32_t btn, kids[4];
    const cellar_a11y_node_t *node;
    cellar_a11y_reset();
    btn = cellar_a11y_create(0, CELLAR_A11Y_BUTTON, "OK");
    if (!btn)
        return CELLAR_LAB_FAIL;
    cellar_a11y_set_value(btn, "pressed");
    node = cellar_a11y_get(btn);
    if (!node || strcmp(node->name, "OK") != 0)
        return CELLAR_LAB_FAIL;
    return pass_if(cellar_a11y_children(1, kids, 4) >= 1);
}

static cellar_lab_result_t t_print(void)
{
    cellar_print_job_t job;
    const char *doc = "hello";
    if (cellar_print_job(NULL, "test", doc, 5, "/tmp", &job) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    return pass_if(job.completed && job.bytes == 5);
}

static cellar_lab_result_t t_device(void)
{
    uint32_t id = 0;
    cellar_device_reset();
    if (cellar_device_attach(CELLAR_DEV_CONTROLLER, "Xbox pad", "XInput", &id) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    if (!cellar_device_get(id))
        return CELLAR_LAB_FAIL;
    return pass_if(cellar_device_detach(id) == CELLAR_OK);
}

static cellar_lab_result_t t_clipboard(void)
{
    char buf[64];
    cellar_clipboard_set("clip");
    cellar_clipboard_get(buf, sizeof buf);
    return pass_if(strcmp(buf, "clip") == 0);
}

static cellar_lab_result_t t_service(void)
{
    const cellar_service_t *q;
    cellar_svc_register("Spooler", NULL);
    if (cellar_svc_start("Spooler") != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    q = cellar_svc_query("Spooler");
    if (!q || q->state != CELLAR_SVC_RUNNING || !q->user_space)
        return CELLAR_LAB_FAIL;
    cellar_svc_stop("Spooler");
    q = cellar_svc_query("Spooler");
    return pass_if(q && q->state == CELLAR_SVC_STOPPED);
}

static cellar_lab_result_t t_display_multimon(void)
{
    cellar_display_t d;
    cellar_monitor_t m;
    cellar_display_init_default(&d);
    memset(&m, 0, sizeof m);
    snprintf(m.name, sizeof m.name, "DISPLAY2");
    m.width = 1280;
    m.height = 720;
    m.dpi = 144;
    m.refresh_hz = 144;
    m.hdr = 1;
    m.orientation = CELLAR_ORIENT_PORTRAIT;
    if (cellar_display_add_monitor(&d, &m) != CELLAR_OK)
        return CELLAR_LAB_FAIL;
    cellar_display_set_mode(&d, CELLAR_WINDOW_FULLSCREEN);
    return pass_if(d.count == 2 && d.mode == CELLAR_WINDOW_FULLSCREEN &&
                   cellar_display_scale(&d.monitors[1]) > 1.0f);
}

static cellar_lab_result_t t_sid_system_uid(void)
{
    cellar_sid_t sys;
    cellar_sid_make(&sys, 5, 18);
    return pass_if(cellar_sid_to_uid(&sys) == 0);
}

static const lab_case_t k_cases[] = {
    { "kernel32",  "ExitProcess/GetLastError exports", t_kernel32_exports },
    { "user32",    "MessageBoxA export",               t_user32_messagebox },
    { "ntdll",     "RtlGetVersion export",             t_ntdll_rtlgetversion },
    { "advapi32",  "RegOpenKeyExA export",             t_advapi_reg },
    { "ws2_32",    "WSAStartup export",                t_ws2_startup },
    { "ole32",     "CoInitializeEx export",            t_ole32_coinit },
    { "shell32",   "SHGetFolderPathA export",          t_shell32_folder },
    { "gdi32",     "GetDeviceCaps export",             t_gdi32_caps },
    { "version",   "version APIs",                     t_version_ex },
    { "display",   "primary 1920x1080 @ 96dpi",        t_display_primary },
    { "display",   "multimon + HDR + DPI scale",       t_display_multimon },
    { "locale",    "CP1252 to UTF-8",                  t_locale_cp1252 },
    { "locale",    "US number grouping",               t_locale_number },
    { "locale",    "US date format",                   t_locale_date },
    { "security",  "ACL allow/deny + impersonation",   t_security_acl },
    { "security",  "SYSTEM SID maps to uid 0",         t_sid_system_uid },
    { "com",       "IUnknown refcount",                t_com_refcount },
    { "com",       "same-process marshal",             t_com_marshal },
    { "com",       "GUID parse/format",                t_guid_parse },
    { "notify",    "notification history",             t_notify },
    { "a11y",      "accessibility tree",               t_a11y },
    { "print",     "virtual PDF printer",              t_print },
    { "device",    "controller attach/detach",         t_device },
    { "desktop",   "clipboard round-trip",             t_clipboard },
    { "service",   "user-space service start/stop",    t_service },
};

cellar_status_t cellar_lab_run(cellar_lab_report_t *out)
{
    size_t i, m, e;
    cellar_lab_report_t r;
    memset(&r, 0, sizeof r);

    for (i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++) {
        cellar_lab_result_t res = k_cases[i].fn();
        r.total++;
        if (res == CELLAR_LAB_PASS) r.passed++;
        else if (res == CELLAR_LAB_SKIP) r.skipped++;
        else {
            r.failed++;
            fprintf(stderr, "lab FAIL %s/%s\n", k_cases[i].suite, k_cases[i].name);
        }
    }

    /* Auto: every registered export is resolvable by name. */
    for (m = 0; m < cellar_win32_module_count(); m++) {
        const cellar_module_t *mod = cellar_win32_module_at(m);
        if (!mod)
            continue;
        for (e = 0; e < mod->count; e++) {
            r.total++;
            if (cellar_win32_lookup(mod->name, mod->exports[e].name))
                r.passed++;
            else {
                r.failed++;
                fprintf(stderr, "lab FAIL export %s!%s\n",
                        mod->name, mod->exports[e].name);
            }
        }
    }

    r.percent = r.total ? (100.0 * (double)r.passed / (double)r.total) : 100.0;
    if (out)
        *out = r;
    return CELLAR_OK;
}

void cellar_lab_print(const cellar_lab_report_t *r)
{
    if (!r)
        return;
    printf("Tests:       %u\n", r->total);
    printf("Passed:      %u\n", r->passed);
    printf("Failed:      %u\n", r->failed);
    printf("Skipped:     %u\n", r->skipped);
    printf("\n");
    printf("Compatibility: %.1f%%\n", r->percent);
}
