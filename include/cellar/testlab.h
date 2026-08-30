/*
 * testlab.h — compatibility test lab.
 *
 * A growing suite of tiny Windows-behavior tests (kernel32, user32, ntdll,
 * advapi32, COM, filesystem, threading, …). `cellar test` runs them and
 * prints pass/fail/skip plus an overall compatibility percentage. Individual
 * tests live in src/testlab and call into Cellar's own implementations.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_TESTLAB_H
#define CELLAR_TESTLAB_H

#include <stdint.h>

#include "cellar.h"

typedef enum cellar_lab_result {
    CELLAR_LAB_PASS = 0,
    CELLAR_LAB_FAIL,
    CELLAR_LAB_SKIP
} cellar_lab_result_t;

typedef struct cellar_lab_report {
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    double   percent;
} cellar_lab_report_t;

cellar_status_t cellar_lab_run(cellar_lab_report_t *out);
void cellar_lab_print(const cellar_lab_report_t *r);

#endif /* CELLAR_TESTLAB_H */
