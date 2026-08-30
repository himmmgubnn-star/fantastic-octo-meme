/*
 * audio.c — audio backend dispatch + the built-in WAV file sink.
 *
 * The WAV sink is the default backend: it requires no third-party libraries
 * and is fully testable (it writes real RIFF/WAVE files). The optional ALSA
 * backend lives in src/audio/alsa.c and is compiled in only when
 * CELLAR_AUDIO_ALSA is defined.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/audio.h"
#include "cellar/perf.h"
#include "cellar/trace.h"

/* ---- WAV sink implementation --------------------------------------------- */

typedef struct wav_sink {
    FILE             *fp;
    cellar_audio_format_t fmt;
    uint32_t          total_bytes;   /* PCM payload written so far */
} wav_sink_t;

/* Little-endian writers for the RIFF/WAVE header. */
static void wput16(FILE *fp, uint16_t v)
{
    fputc((unsigned char)(v & 0xFF), fp);
    fputc((unsigned char)((v >> 8) & 0xFF), fp);
}

static void wput32(FILE *fp, uint32_t v)
{
    size_t i;
    for (i = 0; i < 4; i++)
        fputc((unsigned char)((v >> (8 * i)) & 0xFF), fp);
}

static const char *wav_output_path(void)
{
    const char *p = getenv("CELLAR_WAV_OUT");
    return (p && *p) ? p : "cellar-out.wav";
}

static cellar_status_t wav_open(struct cellar_audio_device *dev,
                                const cellar_audio_format_t *fmt)
{
    wav_sink_t *s = calloc(1, sizeof *s);
    if (!s)
        return CELLAR_ERR_OUT_OF_MEMORY;
    s->fmt = *fmt;
    s->fp = fopen(wav_output_path(), "wb");
    if (!s->fp) {
        free(s);
        return CELLAR_ERR_INVALID_ARGUMENT;
    }
    /* Write a placeholder RIFF/WAVE header; patched on close. */
    fputs("RIFF", s->fp);
    wput32(s->fp, 0);
    fputs("WAVEfmt ", s->fp);
    wput32(s->fp, 16);
    wput16(s->fp, fmt->format_tag ? fmt->format_tag : 1);
    wput16(s->fp, fmt->channels);
    wput32(s->fp, fmt->sample_rate);
    wput32(s->fp, fmt->avg_bytes_per_sec);
    wput16(s->fp, fmt->block_align);
    wput16(s->fp, fmt->bits_per_sample);
    fputs("data", s->fp);
    wput32(s->fp, 0);

    dev->impl = s;
    return CELLAR_OK;
}

static cellar_status_t wav_write(struct cellar_audio_device *dev,
                                 const void *data, uint32_t bytes)
{
    wav_sink_t *s = dev->impl;
    if (!s || !s->fp)
        return CELLAR_ERR_INVALID_ARGUMENT;
    if (bytes == 0)
        return CELLAR_OK;
    if (fwrite(data, 1, bytes, s->fp) != bytes)
        return CELLAR_ERR_INVALID_ARGUMENT;
    s->total_bytes += bytes;
    return CELLAR_OK;
}

static cellar_status_t wav_reset(struct cellar_audio_device *dev)
{
    /* Nothing buffered to drop in a file sink; position simply rewinds. */
    (void)dev;
    return CELLAR_OK;
}

static uint64_t wav_position_ms(const struct cellar_audio_device *dev)
{
    const wav_sink_t *s = dev->impl;
    if (!s)
        return 0;
    if (s->fmt.avg_bytes_per_sec == 0)
        return 0;
    return (uint64_t)s->total_bytes * 1000u / s->fmt.avg_bytes_per_sec;
}

static cellar_status_t wav_close(struct cellar_audio_device *dev)
{
    wav_sink_t *s = dev->impl;
    if (!s)
        return CELLAR_OK;
    if (s->fp) {
        long data_size = s->total_bytes;
        long file_size = data_size + 44; /* 44-byte header */
        fseek(s->fp, 4, SEEK_SET);
        wput32(s->fp, (uint32_t)file_size - 8);
        fseek(s->fp, 40, SEEK_SET);
        wput32(s->fp, (uint32_t)data_size);
        fclose(s->fp);
        s->fp = NULL;
    }
    free(s);
    dev->impl = NULL;
    return CELLAR_OK;
}

static const cellar_audio_backend_t wav_backend = {
    "wav-sink",
    wav_open,
    wav_write,
    wav_reset,
    wav_close,
    wav_position_ms,
};

#ifdef CELLAR_AUDIO_ALSA
extern const cellar_audio_backend_t cellar_alsa_backend;
#endif

const cellar_audio_backend_t *cellar_audio_default_backend(void)
{
#ifdef CELLAR_AUDIO_ALSA
    return &cellar_alsa_backend;
#else
    return &wav_backend;
#endif
}

/* ---- Dispatch ------------------------------------------------------------- */

cellar_status_t cellar_audio_open(cellar_audio_device_t *dev,
                                  const cellar_audio_backend_t *backend,
                                  const cellar_audio_format_t *fmt)
{
    if (!dev || !backend || !backend->open || !fmt)
        return CELLAR_ERR_INVALID_ARGUMENT;
    memset(dev, 0, sizeof *dev);
    dev->backend = backend;
    return backend->open(dev, fmt);
}

cellar_status_t cellar_audio_write(cellar_audio_device_t *dev,
                                   const void *data, uint32_t bytes)
{
    cellar_status_t st;
    if (!dev || !dev->backend || !dev->backend->write)
        return CELLAR_ERR_INVALID_ARGUMENT;
    st = dev->backend->write(dev, data, bytes);
    if (st == CELLAR_OK) {
        cellar_perf_trace("audio.write", bytes);
        CELLAR_TRACE(CELLAR_TRACE_AUDIO, "write %u bytes -> %s",
                     bytes, dev->backend->name);
    }
    return st;
}

cellar_status_t cellar_audio_reset(cellar_audio_device_t *dev)
{
    if (!dev || !dev->backend || !dev->backend->reset)
        return CELLAR_ERR_INVALID_ARGUMENT;
    return dev->backend->reset(dev);
}

cellar_status_t cellar_audio_close(cellar_audio_device_t *dev)
{
    cellar_status_t st;
    if (!dev || !dev->backend || !dev->backend->close)
        return CELLAR_ERR_INVALID_ARGUMENT;
    st = dev->backend->close(dev);
    return st;
}

uint64_t cellar_audio_position_ms(const cellar_audio_device_t *dev)
{
    if (!dev || !dev->backend || !dev->backend->position_ms)
        return 0;
    return dev->backend->position_ms(dev);
}
