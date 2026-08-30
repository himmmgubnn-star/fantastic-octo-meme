/*
 * test_audio.c — unit tests for the audio backend.
 *
 * Exercises the default backend (WAV sink): open a device, write PCM, check
 * position accounting, and confirm the close produces a real RIFF/WAVE file
 * with a correct header.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "airlock/airlock.h"
#include "airlock/audio.h"

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

static uint16_t le16(const void *p)
{
    const unsigned char *b = p;
    return (uint16_t)(b[0] | ((uint16_t)b[1] << 8));
}
static uint32_t le32(const void *p)
{
    const unsigned char *b = p;
    return (uint32_t)b[0] | ((uint32_t)b[1] << 8) |
           ((uint32_t)b[2] << 16) | ((uint32_t)b[3] << 24);
}

int main(void)
{
    const airlock_audio_backend_t *be = airlock_audio_default_backend();
    airlock_audio_device_t dev;
    airlock_audio_format_t fmt;
    airlock_status_t st;
    uint8_t pcm[4096];
    FILE *fp;
    long len;
    unsigned char hdr[44];
    const char *path = "airlock-test-out.wav";

    /* Route the WAV sink to this test's output file. */
    setenv("AIRLOCK_WAV_OUT", path, 1);

    /* Default backend must be non-NULL and have the core ops. */
    CHECK(be != NULL, "default backend exists");
    if (!be)
        return 1;
    CHECK(be->open && be->write && be->close && be->position_ms,
          "backend implements required ops");

    /* Invalid args. */
    st = airlock_audio_open(NULL, be, &fmt);
    CHECK(st == AIRLOCK_ERR_INVALID_ARGUMENT, "open(NULL,...) rejected");

    /* Open a 44100 Hz stereo 16-bit device. */
    memset(&fmt, 0, sizeof fmt);
    fmt.format_tag = 1;
    fmt.channels = 2;
    fmt.sample_rate = 44100;
    fmt.bits_per_sample = 16;
    fmt.block_align = (uint16_t)(2 * fmt.channels);
    fmt.avg_bytes_per_sec = fmt.sample_rate * fmt.block_align;

    st = airlock_audio_open(&dev, be, &fmt);
    CHECK(st == AIRLOCK_OK, "audio open succeeds");
    if (st != AIRLOCK_OK)
        return 1;

    memset(pcm, 0, sizeof pcm);
    st = airlock_audio_write(&dev, pcm, sizeof pcm);
    CHECK(st == AIRLOCK_OK, "audio write succeeds");
    CHECK(airlock_audio_position_ms(&dev) == 4096u * 1000u / fmt.avg_bytes_per_sec,
          "position tracks bytes written");

    st = airlock_audio_close(&dev);
    CHECK(st == AIRLOCK_OK, "audio close succeeds");

    /* The WAV file must be a valid RIFF/WAVE with a 44-byte header. */
    fp = fopen(path, "rb");
    CHECK(fp != NULL, "WAV output file exists");
    if (fp) {
        fseek(fp, 0, SEEK_END);
        len = ftell(fp);
        fseek(fp, 0, SEEK_SET);
        CHECK(len == 44 + (long)sizeof pcm, "file size = header + payload");

        if (len >= 44) {
            if (fread(hdr, 1, 44, fp) == 44) {
                CHECK(memcmp(hdr, "RIFF", 4) == 0, "RIFF tag");
                CHECK(memcmp(hdr + 8, "WAVE", 4) == 0, "WAVE tag");
                CHECK(memcmp(hdr + 12, "fmt ", 4) == 0, "fmt chunk");
                CHECK(le16(hdr + 20) == 1, "PCM format tag");
                CHECK(le16(hdr + 22) == 2, "stereo");
                CHECK(le32(hdr + 24) == 44100, "sample rate 44100");
                CHECK(memcmp(hdr + 36, "data", 4) == 0, "data chunk");
                CHECK(le32(hdr + 40) == (uint32_t)sizeof pcm, "data size");
            }
        }
        fclose(fp);
        remove(path);
    }

    if (g_failures == 0) {
        printf("test_audio: all tests passed (backend=%s)\n", be->name);
        return 0;
    }
    fprintf(stderr, "test_audio: %d test(s) failed\n", g_failures);
    return 1;
}
