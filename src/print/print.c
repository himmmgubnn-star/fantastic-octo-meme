/*
 * print.c — Windows printing → file/CUPS.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include "cellar/cellar.h"
#include "cellar/print.h"

#define CELLAR_MAX_PRINTERS 8

static cellar_printer_t g_prn[CELLAR_MAX_PRINTERS];
static size_t g_n;
static uint32_t g_job = 1;
static int g_inited;

void cellar_print_init(void)
{
    cellar_printer_t p;
    if (g_inited)
        return;
    g_inited = 1;
    memset(&p, 0, sizeof p);
    snprintf(p.name, sizeof p.name, "Cellar PDF");
    snprintf(p.driver, sizeof p.driver, "pdf");
    p.default_printer = 1;
    g_prn[0] = p;
    g_n = 1;
}

cellar_status_t cellar_print_add_printer(const cellar_printer_t *p)
{
    cellar_print_init();
    if (!p || !p->name[0])
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (g_n >= CELLAR_MAX_PRINTERS)
        return CELLAR_ERR_OUT_OF_MEMORY;
    g_prn[g_n++] = *p;
    return CELLAR_OK;
}

const cellar_printer_t *cellar_print_default(void)
{
    size_t i;
    cellar_print_init();
    for (i = 0; i < g_n; i++)
        if (g_prn[i].default_printer)
            return &g_prn[i];
    return g_n ? &g_prn[0] : NULL;
}

size_t cellar_print_list(cellar_printer_t *out, size_t cap)
{
    size_t n;
    cellar_print_init();
    if (!out)
        return 0;
    n = g_n < cap ? g_n : cap;
    memcpy(out, g_prn, n * sizeof *out);
    return n;
}

cellar_status_t cellar_print_job(const char *printer, const char *doc,
                                 const void *data, size_t n,
                                 const char *out_dir,
                                 cellar_print_job_t *out)
{
    const cellar_printer_t *p;
    FILE *f;
    char path[512];
    cellar_print_init();
    if (!out)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memset(out, 0, sizeof *out);
    p = cellar_print_default();
    if (printer && *printer) {
        size_t i;
        p = NULL;
        for (i = 0; i < g_n; i++)
            if (strcasecmp(g_prn[i].name, printer) == 0) {
                p = &g_prn[i];
                break;
            }
        if (!p)
            return CELLAR_ERR_INVALID_ARGUMENT;
    }
    if (!p)
        return CELLAR_ERR_INVALID_ARGUMENT;
    out->id = g_job++;
    snprintf(out->printer, sizeof out->printer, "%s", p->name);
    snprintf(out->document, sizeof out->document, "%s", doc ? doc : "untitled");
    snprintf(path, sizeof path, "%s/cellar-print-%u.ps",
             out_dir && *out_dir ? out_dir : "/tmp", out->id);
    cellar_strlcpy(out->output_path, sizeof out->output_path, path);
    f = fopen(path, "wb");
    if (!f)
        return CELLAR_ERR_INVALID_ARGUMENT;
    fprintf(f, "%%!PS-Adobe-3.0\n%% Cellar print job %u (%s)\n",
            out->id, out->document);
    if (data && n)
        fwrite(data, 1, n, f);
    fclose(f);
    out->bytes = n;
    out->completed = 1;
    return CELLAR_OK;
}
