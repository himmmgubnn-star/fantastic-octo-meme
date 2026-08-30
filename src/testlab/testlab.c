/*
 * testlab.c — compatibility test lab.
 *
 * Each case verifies one Windows behavior against Airlock's implementation.
 * `airlock_lab_run` also auto-generates an export-presence test for every
 * function currently registered in the Win32 layer, so the lab grows with
 * the API surface.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <time.h>

#include "airlock/a11y.h"
#include "airlock/airlock.h"
#include "airlock/com.h"
#include "airlock/compat.h"
#include "airlock/desktop.h"
#include "airlock/device.h"
#include "airlock/display.h"
#include "airlock/locale.h"
#include "airlock/notify.h"
#include "airlock/print.h"
#include "airlock/security.h"
#include "airlock/service.h"
#include "airlock/shell.h"
#include "airlock/testlab.h"
#include "airlock/win32.h"

typedef airlock_lab_result_t (*lab_fn)(void);

typedef struct lab_case {
    const char *suite;
    const char *name;
    lab_fn fn;
} lab_case_t;

static airlock_lab_result_t pass_if(int cond)
{
    return cond ? AIRLOCK_LAB_PASS : AIRLOCK_LAB_FAIL;
}

static airlock_lab_result_t t_kernel32_exports(void)
{
    return pass_if(airlock_win32_export_exists("KERNEL32.dll", "ExitProcess") &&
                   airlock_win32_export_exists("KERNEL32.dll", "GetLastError"));
}

static airlock_lab_result_t t_user32_messagebox(void)
{
    return pass_if(airlock_win32_export_exists("USER32.dll", "MessageBoxA"));
}

static airlock_lab_result_t t_ntdll_rtlgetversion(void)
{
    return pass_if(airlock_win32_export_exists("ntdll.dll", "RtlGetVersion"));
}

static airlock_lab_result_t t_advapi_reg(void)
{
    return pass_if(airlock_win32_export_exists("ADVAPI32.dll", "RegOpenKeyExA"));
}

static airlock_lab_result_t t_ws2_startup(void)
{
    return pass_if(airlock_win32_export_exists("ws2_32.dll", "WSAStartup"));
}

static airlock_lab_result_t t_ole32_coinit(void)
{
    return pass_if(airlock_win32_export_exists("ole32.dll", "CoInitializeEx"));
}

static airlock_lab_result_t t_shell32_folder(void)
{
    return pass_if(airlock_win32_export_exists("shell32.dll", "SHGetFolderPathA"));
}

static airlock_lab_result_t t_gdi32_caps(void)
{
    return pass_if(airlock_win32_export_exists("gdi32.dll", "GetDeviceCaps"));
}

static airlock_lab_result_t t_version_ex(void)
{
    return pass_if(airlock_win32_export_exists("version.dll", "GetFileVersionInfoSizeA") ||
                   airlock_win32_export_exists("KERNEL32.dll", "GetVersionExA"));
}

static airlock_lab_result_t t_display_primary(void)
{
    const airlock_monitor_t *m = airlock_display_primary(airlock_display_current());
    return pass_if(m && m->width == 1920 && m->height == 1080 &&
                   airlock_display_scale(m) == 1.0f);
}

static airlock_lab_result_t t_locale_cp1252(void)
{
    const uint8_t in[] = { 'A', 0x80 }; /* A + euro */
    char out[16];
    int n = airlock_cp_to_utf8(AIRLOCK_CP_1252, in, 2, out, sizeof out);
    return pass_if(n > 1 && out[0] == 'A');
}

static airlock_lab_result_t t_locale_number(void)
{
    char buf[32];
    airlock_locale_t l;
    airlock_locale_english_us(&l);
    airlock_locale_set(&l);
    if (airlock_locale_format_number(1234.5, buf, sizeof buf) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    return pass_if(strstr(buf, "1,234") != NULL);
}

static airlock_lab_result_t t_locale_date(void)
{
    struct tm t;
    char buf[32];
    memset(&t, 0, sizeof t);
    t.tm_year = 126; /* 2026 */
    t.tm_mon = 7;
    t.tm_mday = 30;
    if (airlock_locale_format_date(&t, buf, sizeof buf) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    return pass_if(strcmp(buf, "08/30/2026") == 0);
}

static airlock_lab_result_t t_security_acl(void)
{
    airlock_token_t tok;
    airlock_sd_t sd;
    airlock_sid_t other;
    memset(&sd, 0, sizeof sd);
    airlock_token_default(&tok);
    airlock_sid_make(&sd.owner, 5, 1000);
    airlock_sid_make(&other, 5, 1001);
    airlock_acl_add(&sd.dacl, 1, &tok.user, AIRLOCK_ACCESS_READ);
    if (!airlock_acl_check(&sd, &tok, AIRLOCK_ACCESS_READ))
        return AIRLOCK_LAB_FAIL;
    if (airlock_acl_check(&sd, &tok, AIRLOCK_ACCESS_WRITE))
        return AIRLOCK_LAB_FAIL;
    airlock_token_impersonate(&tok, &other);
    if (airlock_acl_check(&sd, &tok, AIRLOCK_ACCESS_READ))
        return AIRLOCK_LAB_FAIL; /* impersonated as other, no ACE */
    airlock_token_revert(&tok);
    return AIRLOCK_LAB_PASS;
}

static airlock_lab_result_t t_com_refcount(void)
{
    void *obj = NULL;
    airlock_iunknown_t *iu;
    uint32_t r;
    airlock_com_uninit();
    if (airlock_com_init(AIRLOCK_APT_MTA) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    airlock_com_register_builtins();
    if (airlock_com_create(&AIRLOCK_CLSID_NULL, &AIRLOCK_IID_IUNKNOWN, &obj) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    iu = (airlock_iunknown_t *)obj;
    /* factory AddRef(1) + QueryInterface AddRef(2) */
    r = iu->vtbl->add_ref(iu);
    if (r != 3)
        return AIRLOCK_LAB_FAIL;
    if (iu->vtbl->release(iu) != 2)
        return AIRLOCK_LAB_FAIL;
    if (iu->vtbl->release(iu) != 1)
        return AIRLOCK_LAB_FAIL;
    if (iu->vtbl->release(iu) != 0)
        return AIRLOCK_LAB_FAIL;
    airlock_com_uninit();
    return AIRLOCK_LAB_PASS;
}

static airlock_lab_result_t t_com_marshal(void)
{
    void *obj = NULL;
    airlock_iunknown_t *iu, *back = NULL;
    uint8_t buf[32];
    size_t n = 0;
    airlock_com_init(AIRLOCK_APT_MTA);
    airlock_com_register_builtins();
    if (airlock_com_create(&AIRLOCK_CLSID_NULL, &AIRLOCK_IID_IUNKNOWN, &obj) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    iu = (airlock_iunknown_t *)obj;
    if (airlock_com_marshal(iu, buf, sizeof buf, &n) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    if (airlock_com_unmarshal(buf, n, &back) != AIRLOCK_OK || back != iu)
        return AIRLOCK_LAB_FAIL;
    iu->vtbl->release(iu); /* marshal AddRef */
    iu->vtbl->release(iu); /* QueryInterface AddRef */
    iu->vtbl->release(iu); /* factory AddRef */
    airlock_com_uninit();
    return AIRLOCK_LAB_PASS;
}

static airlock_lab_result_t t_guid_parse(void)
{
    airlock_guid_t g;
    char fmt[64];
    if (airlock_guid_parse("{00000000-0000-0000-C000-000000000046}", &g) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    if (!airlock_guid_eq(&g, &AIRLOCK_IID_IUNKNOWN))
        return AIRLOCK_LAB_FAIL;
    airlock_guid_format(&g, fmt, sizeof fmt);
    return pass_if(strstr(fmt, "00000000") != NULL);
}

static airlock_lab_result_t t_notify(void)
{
    airlock_notification_t n, hist[4];
    uint32_t id;
    memset(&n, 0, sizeof n);
    snprintf(n.summary, sizeof n.summary, "hello");
    id = airlock_notify_show(&n);
    if (id == 0 || airlock_notify_history(hist, 4) == 0)
        return AIRLOCK_LAB_FAIL;
    return pass_if(airlock_notify_close(id) == AIRLOCK_OK);
}

static airlock_lab_result_t t_a11y(void)
{
    uint32_t btn, kids[4];
    const airlock_a11y_node_t *node;
    airlock_a11y_reset();
    btn = airlock_a11y_create(0, AIRLOCK_A11Y_BUTTON, "OK");
    if (!btn)
        return AIRLOCK_LAB_FAIL;
    airlock_a11y_set_value(btn, "pressed");
    node = airlock_a11y_get(btn);
    if (!node || strcmp(node->name, "OK") != 0)
        return AIRLOCK_LAB_FAIL;
    return pass_if(airlock_a11y_children(1, kids, 4) >= 1);
}

static airlock_lab_result_t t_print(void)
{
    airlock_print_job_t job;
    const char *doc = "hello";
    if (airlock_print_job(NULL, "test", doc, 5, "/tmp", &job) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    return pass_if(job.completed && job.bytes == 5);
}

static airlock_lab_result_t t_device(void)
{
    uint32_t id = 0;
    airlock_device_reset();
    if (airlock_device_attach(AIRLOCK_DEV_CONTROLLER, "Xbox pad", "XInput", &id) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    if (!airlock_device_get(id))
        return AIRLOCK_LAB_FAIL;
    return pass_if(airlock_device_detach(id) == AIRLOCK_OK);
}

static airlock_lab_result_t t_clipboard(void)
{
    char buf[64];
    airlock_clipboard_set("clip");
    airlock_clipboard_get(buf, sizeof buf);
    return pass_if(strcmp(buf, "clip") == 0);
}

static airlock_lab_result_t t_service(void)
{
    const airlock_service_t *q;
    airlock_svc_register("Spooler", NULL);
    if (airlock_svc_start("Spooler") != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    q = airlock_svc_query("Spooler");
    if (!q || q->state != AIRLOCK_SVC_RUNNING || !q->user_space)
        return AIRLOCK_LAB_FAIL;
    airlock_svc_stop("Spooler");
    q = airlock_svc_query("Spooler");
    return pass_if(q && q->state == AIRLOCK_SVC_STOPPED);
}

static airlock_lab_result_t t_display_multimon(void)
{
    airlock_display_t d;
    airlock_monitor_t m;
    airlock_display_init_default(&d);
    memset(&m, 0, sizeof m);
    snprintf(m.name, sizeof m.name, "DISPLAY2");
    m.width = 1280;
    m.height = 720;
    m.dpi = 144;
    m.refresh_hz = 144;
    m.hdr = 1;
    m.orientation = AIRLOCK_ORIENT_PORTRAIT;
    if (airlock_display_add_monitor(&d, &m) != AIRLOCK_OK)
        return AIRLOCK_LAB_FAIL;
    airlock_display_set_mode(&d, AIRLOCK_WINDOW_FULLSCREEN);
    return pass_if(d.count == 2 && d.mode == AIRLOCK_WINDOW_FULLSCREEN &&
                   airlock_display_scale(&d.monitors[1]) > 1.0f);
}

static airlock_lab_result_t t_sid_system_uid(void)
{
    airlock_sid_t sys;
    airlock_sid_make(&sys, 5, 18);
    return pass_if(airlock_sid_to_uid(&sys) == 0);
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

airlock_status_t airlock_lab_run(airlock_lab_report_t *out)
{
    size_t i, m, e;
    airlock_lab_report_t r;
    memset(&r, 0, sizeof r);

    for (i = 0; i < sizeof k_cases / sizeof k_cases[0]; i++) {
        airlock_lab_result_t res = k_cases[i].fn();
        r.total++;
        if (res == AIRLOCK_LAB_PASS) r.passed++;
        else if (res == AIRLOCK_LAB_SKIP) r.skipped++;
        else {
            r.failed++;
            fprintf(stderr, "lab FAIL %s/%s\n", k_cases[i].suite, k_cases[i].name);
        }
    }

    /* Auto: every registered export is resolvable by name. */
    for (m = 0; m < airlock_win32_module_count(); m++) {
        const airlock_module_t *mod = airlock_win32_module_at(m);
        if (!mod)
            continue;
        for (e = 0; e < mod->count; e++) {
            r.total++;
            if (airlock_win32_lookup(mod->name, mod->exports[e].name))
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
    return AIRLOCK_OK;
}

void airlock_lab_print(const airlock_lab_report_t *r)
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
