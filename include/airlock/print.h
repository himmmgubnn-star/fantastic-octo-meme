/*
 * print.h — Windows printing compatibility.
 *
 * Applications see Windows-style printers; Airlock writes a job file (and can
 * later hand it to CUPS). The default printer is a virtual "Airlock PDF"
 * device that captures the document bytes.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_PRINT_H
#define AIRLOCK_PRINT_H

#include <stddef.h>
#include <stdint.h>

#include "airlock.h"

typedef struct airlock_printer {
    char name[64];
    char driver[32]; /* "pdf", "ps", "cups" */
    int  default_printer;
} airlock_printer_t;

typedef struct airlock_print_job {
    uint32_t id;
    char     printer[64];
    char     document[128];
    char     output_path[256];
    size_t   bytes;
    int      completed;
} airlock_print_job_t;

void airlock_print_init(void);
airlock_status_t airlock_print_add_printer(const airlock_printer_t *p);
const airlock_printer_t *airlock_print_default(void);
airlock_status_t airlock_print_job(const char *printer, const char *doc,
                                 const void *data, size_t n,
                                 const char *out_dir,
                                 airlock_print_job_t *out);
size_t airlock_print_list(airlock_printer_t *out, size_t cap);

#endif /* AIRLOCK_PRINT_H */
