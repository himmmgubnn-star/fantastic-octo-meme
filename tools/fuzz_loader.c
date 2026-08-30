/*
 * fuzz_loader.c — deterministic fuzz harness for the PE loader.
 *
 * Starts from a valid PE seed and applies random byte mutations, then loads
 * the result and verifies the loader never crashes, never leaks the image, and
 * always reports a typed status. This is a lightweight in-process fuzzer (the
 * kind of thing you'd run in CI); it is deliberately deterministic with a
 * seed so failures reproduce.
 *
 * Usage: fuzz_loader <seed.pe> [iterations] [seed]
 *   make fuzz   (runs a short fuzz campaign against samples/hello.exe)
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/loader.h"
#include "cellar/win32.h"

static uint32_t rng_state;

static uint32_t next_rand(void)
{
    /* xorshift32 */
    uint32_t x = rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    rng_state = x;
    return x;
}

int main(int argc, char **argv)
{
    const char *path = argc > 1 ? argv[1] : "samples/hello.exe";
    unsigned long iters = argc > 2 ? strtoul(argv[2], NULL, 10) : 2000;
    unsigned long seed = argc > 3 ? strtoul(argv[3], NULL, 10) : 12345;
    cellar_image_t img;
    cellar_status_t st;
    FILE *f;
    long len;
    unsigned char *seedbuf;
    unsigned char *mut;
    unsigned long i, j;

    cellar_win32_init();

    f = fopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open seed %s\n", path);
        return 1;
    }
    fseek(f, 0, SEEK_END);
    len = ftell(f);
    fseek(f, 0, SEEK_SET);
    seedbuf = malloc((size_t)len);
    mut = malloc((size_t)len);
    if (!seedbuf || !mut) return 2;
    if (fread(seedbuf, 1, (size_t)len, f) != (size_t)len) return 3;
    fclose(f);

    rng_state = (uint32_t)seed;

    for (i = 0; i < iters; i++) {
        memcpy(mut, seedbuf, (size_t)len);
        /* Apply 1..8 random byte mutations. */
        unsigned int nmut = 1 + next_rand() % 8;
        for (j = 0; j < nmut; j++)
            mut[next_rand() % (unsigned)len] = (unsigned char)next_rand();

        st = cellar_image_load_buffer(mut, (size_t)len,
                                      CELLAR_LOAD_DEFAULT |
                                      CELLAR_LOAD_PARSE_EXPORTS |
                                      CELLAR_LOAD_PARSE_RELOCS, &img);
        /* Loader must return a typed status and never crash. On success it
         * must also clean up cleanly (ASan validates no leak). */
        if (st == CELLAR_OK)
            cellar_image_unload(&img);
    }

    printf("fuzz_loader: %lu iterations against %s passed (seed %lu)\n",
           iters, path, seed);
    free(seedbuf);
    free(mut);
    return 0;
}
