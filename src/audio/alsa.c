/*
 * alsa.c — optional ALSA backend (only compiled when CELLAR_AUDIO_ALSA is
 * defined, i.e. `-DCELLAR_AUDIO_ALSA` and linking -lasound).
 *
 * This gives real low-latency output on desktop Linux. It is intentionally
 * separate from audio.c so the default build stays dependency-free (Android
 * uses the null/WAV sink or an AudioTrack port later).
 *
 * SPDX-License-Identifier: MIT
 */
#ifdef CELLAR_AUDIO_ALSA

#include <alsa/asoundlib.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/audio.h"

typedef struct alsa_impl {
    snd_pcm_t             *pcm;
    cellar_audio_format_t  fmt;
    uint64_t               bytes_written;
} alsa_impl_t;

static cellar_status_t alsa_open(struct cellar_audio_device *dev,
                                 const cellar_audio_format_t *fmt)
{
    alsa_impl_t *ai = calloc(1, sizeof *ai);
    snd_pcm_hw_params_t *params;
    int err;
    unsigned int rate = fmt->sample_rate;
    unsigned int periods = 4;

    if (!ai)
        return CELLAR_ERR_OUT_OF_MEMORY;
    ai->fmt = *fmt;

    err = snd_pcm_open(&ai->pcm, "default", SND_PCM_STREAM_PLAYBACK, 0);
    if (err < 0) {
        free(ai);
        return CELLAR_ERR_INVALID_ARGUMENT;
    }

    snd_pcm_hw_params_alloca(&params);
    if (snd_pcm_hw_params_any(ai->pcm, params) < 0 ||
        snd_pcm_hw_params_set_access(ai->pcm, params,
                                     SND_PCM_ACCESS_RW_INTERLEAVED) < 0 ||
        snd_pcm_hw_params_set_format(ai->pcm, params,
                                     (snd_pcm_format_t)fmt->bits_per_sample == 16
                                         ? SND_PCM_FORMAT_S16_LE
                                         : SND_PCM_FORMAT_U8) < 0 ||
        snd_pcm_hw_params_set_channels(ai->pcm, params, fmt->channels) < 0 ||
        snd_pcm_hw_params_set_rate_near(ai->pcm, params, &rate, 0) < 0 ||
        snd_pcm_hw_params_set_periods_near(ai->pcm, params, &periods, 0) < 0 ||
        snd_pcm_hw_params(ai->pcm, params) < 0) {
        snd_pcm_close(ai->pcm);
        free(ai);
        return CELLAR_ERR_INVALID_ARGUMENT;
    }

    dev->impl = ai;
    return CELLAR_OK;
}

static cellar_status_t alsa_write(struct cellar_audio_device *dev,
                                  const void *data, uint32_t bytes)
{
    alsa_impl_t *ai = dev->impl;
    snd_pcm_sframes_t frames;
    if (!ai || !ai->pcm)
        return CELLAR_ERR_INVALID_ARGUMENT;

    frames = snd_pcm_writei(ai->pcm, data,
                            bytes / (uint32_t)ai->fmt.block_align);
    if (frames < 0)
        snd_pcm_recover(ai->pcm, (int)frames, 1);
    ai->bytes_written += bytes;
    return CELLAR_OK;
}

static cellar_status_t alsa_reset(struct cellar_audio_device *dev)
{
    alsa_impl_t *ai = dev->impl;
    if (ai && ai->pcm)
        snd_pcm_drop(ai->pcm);
    return CELLAR_OK;
}

static uint64_t alsa_position_ms(const struct cellar_audio_device *dev)
{
    const alsa_impl_t *ai = dev->impl;
    if (!ai || ai->fmt.avg_bytes_per_sec == 0)
        return 0;
    return ai->bytes_written * 1000u / ai->fmt.avg_bytes_per_sec;
}

static cellar_status_t alsa_close(struct cellar_audio_device *dev)
{
    alsa_impl_t *ai = dev->impl;
    if (ai) {
        if (ai->pcm)
            snd_pcm_drain(ai->pcm), snd_pcm_close(ai->pcm);
        free(ai);
    }
    dev->impl = NULL;
    return CELLAR_OK;
}

const cellar_audio_backend_t cellar_alsa_backend = {
    "alsa",
    alsa_open,
    alsa_write,
    alsa_reset,
    alsa_close,
    alsa_position_ms,
};

#endif /* CELLAR_AUDIO_ALSA */
