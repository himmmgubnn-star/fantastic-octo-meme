/*
 * print.c — Windows printing → file/CUPS.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "airlock/airlock.h"
#include "airlock/print.h"

#define AIRLOCK_MAX_PRINTERS 8

static airlock_printer_t g_prn[AIRLOCK_MAX_PRINTERS];
static size_t g_n;
static uint32_t g_job = 1;
static int g_inited;

void airlock_print_init(void)
{
    airlock_printer_t p;
    if (g_inited)
        return;
    g_inited = 1;
    memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "Airlock PDF");
    snprintf(p.driver, sizeof p.driver, "pdf");
    p.default_printer = 1;
    g_prn[0] = p;
    g_n = 1;
}

airlock_status_t airlock_print_add_printer(const airlock_printer_t *p)
{
    airlock_print_init();
    if (!p || !p->name[0])
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    if (g_n >= AIRLOCK_MAX_PRINTERS)
        return AIRLOCK_ERR_OUT_OF_MEMORY;
    g_prn[g_n++] = *p;
    return AIRLOCK_OK;
}

const airlock_printer_t *airlock_print_default(void)
{
    size_t i;
    airlock_print_init();
    for (i = 0; i < g_n; i++)
        if (g_prn[i].default_printer)
            return &g_prn[i];
    return g_n ? &g_prn[0] : NULL;
}

size_t airlock_print_list(airlock_printer_t *out, size_t cap)
{
    size_t n;
    airlock_print_init();
    if (!out)
        return 0;
    n = g_n < cap ? g_n : cap;
    memcpy(out, g_prn, n * sizeof *out);
    return n;
}

airlock_status_t airlock_print_job(const char *printer, const char *doc,
                                 const void *data, size_t n,
                                 const char *out_dir,
                                 airlock_print_job_t *out)
{
    const airlock_printer_t *p;
    FILE *f;
    char path[512];
    airlock_print_init();
    if (!out)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    p = airlock_print_default();
    if (printer && *printer) {
        size_t i;
        p = NULL;
        for (i = 0; i < g_n; i++)
            if (strcasecmp(g_prn[i].name, printer) == 0) {
                p = &g_prn[i];
                break;
            }
        if (!p)
            return AIRLOCK_ERR_INVALID_ARGUMENT;
    }
    if (!p)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    out->id = g_job++;
    snprintf(out->printer, sizeof out->printer, "%s", p->name);
    snprintf(out->document, sizeof out->document, "%s", doc ? doc : "untitled");
    snprintf(path, sizeof path, "%s/airlock-print-%u.ps",
             out_dir && *out_dir ? out_dir : "/tmp", out->id);
    airlock_strlcpy(out->output_path, sizeof out->output_path, path);
    f = fopen(path, "wb");
    if (!f)
        return AIRLOCK_ERR_INVALID_ARGUMENT;
    fprintf(f, "%%!PS-Adobe-3.0\n%% Airlock print job %u (%s)\n",
            out->id, out->document);
    if (data && n)
        fwrite(data, 1, n, f);
    fclose(f);
    out->bytes = n;
    out->completed = 1;
    return AIRLOCK_OK;
}
