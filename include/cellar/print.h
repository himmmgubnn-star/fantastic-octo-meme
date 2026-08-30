/*
 * print.h — Windows printing compatibility.
 *
 * Applications see Windows-style printers; Cellar writes a job file (and can
 * later hand it to CUPS). The default printer is a virtual "Cellar PDF"
 * device that captures the document bytes.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_PRINT_H
#define CELLAR_PRINT_H

#include <stddef.h>
#include <stdint.h>

#include "cellar.h"

typedef struct cellar_printer {
    char name[64];
    char driver[32]; /* "pdf", "ps", "cups" */
    int  default_printer;
} cellar_printer_t;

typedef struct cellar_print_job {
    uint32_t id;
    char     printer[64];
    char     document[128];
    char     output_path[256];
    size_t   bytes;
    int      completed;
} cellar_print_job_t;

void cellar_print_init(void);
cellar_status_t cellar_print_add_printer(const cellar_printer_t *p);
const cellar_printer_t *cellar_print_default(void);
cellar_status_t cellar_print_job(const char *printer, const char *doc,
                                 const void *data, size_t n,
                                 const char *out_dir,
                                 cellar_print_job_t *out);
size_t cellar_print_list(cellar_printer_t *out, size_t cap);

#endif /* CELLAR_PRINT_H */
