/*
 * testlab.h — compatibility test lab.
 *
 * A growing suite of tiny Windows-behavior tests (kernel32, user32, ntdll,
 * advapi32, COM, filesystem, threading, …). `airlock test` runs them and
 * prints pass/fail/skip plus an overall compatibility percentage. Individual
 * tests live in src/testlab and call into Airlock's own implementations.
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef AIRLOCK_TESTLAB_H
#define AIRLOCK_TESTLAB_H

#include <stdint.h>

#include "airlock.h"

typedef enum airlock_lab_result {
    AIRLOCK_LAB_PASS = 0,
    AIRLOCK_LAB_FAIL,
    AIRLOCK_LAB_SKIP
} airlock_lab_result_t;

typedef struct airlock_lab_report {
    uint32_t total;
    uint32_t passed;
    uint32_t failed;
    uint32_t skipped;
    double   percent;
} airlock_lab_report_t;

airlock_status_t airlock_lab_run(airlock_lab_report_t *out);
void airlock_lab_print(const airlock_lab_report_t *r);

#endif /* AIRLOCK_TESTLAB_H */
