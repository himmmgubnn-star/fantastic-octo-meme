/*
 * audio.h — Cellar audio backend API.
 *
 * Games output audio through WinMM (winmm.dll), DirectSound (dsound.dll),
 * XAudio2, or OpenAL. Cellar funnels those into a small set of *backends* so
 * the Win32 API layer never touches hardware directly:
 *
 *   Win32 (winmm.dll)  ->  cellar_audio_device_t  ->  audio backend
 *                                                       |-> WAV file sink (default)
 *                                                       |-> ALSA (optional)
 *                                                       `-> null sink
 *
 * A "device" is one open audio stream with a format. Writing a buffer pushes
 * audio to the backend. The default WAV sink is dependency-free and testable;
 * an optional ALSA backend can be compiled in (see CMakeLists.txt).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef CELLAR_AUDIO_H
#define CELLAR_AUDIO_H

#include <stdint.h>

#include "cellar.h"

/* PCM format description (mirrors WAVEFORMATEX fields Windows uses). */
typedef struct cellar_audio_format {
    uint16_t format_tag;        /* 1 = PCM (WAVE_FORMAT_PCM)               */
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t avg_bytes_per_sec;
    uint16_t block_align;
    uint16_t bits_per_sample;
} cellar_audio_format_t;

struct cellar_audio_device;

/* One backend implementation. */
typedef struct cellar_audio_backend {
    const char *name;
    cellar_status_t (*open)(struct cellar_audio_device *dev,
                            const cellar_audio_format_t *fmt);
    cellar_status_t (*write)(struct cellar_audio_device *dev,
                             const void *data, uint32_t bytes);
    cellar_status_t (*reset)(struct cellar_audio_device *dev);
    cellar_status_t (*close)(struct cellar_audio_device *dev);
    uint64_t (*position_ms)(const struct cellar_audio_device *dev);
} cellar_audio_backend_t;

/* An open audio stream. `impl` is private to the backend. */
typedef struct cellar_audio_device {
    const cellar_audio_backend_t *backend;
    void *impl;
} cellar_audio_device_t;

/* ---- API ----------------------------------------------------------------- */

/* Select the default backend. Returns the built-in WAV sink when the optional
 * ALSA backend is not compiled in. Never returns NULL. */
const cellar_audio_backend_t *cellar_audio_default_backend(void);

/* Open a device on the given backend with the given PCM format. */
cellar_status_t cellar_audio_open(cellar_audio_device_t *dev,
                                  const cellar_audio_backend_t *backend,
                                  const cellar_audio_format_t *fmt);

/* Push `bytes` of PCM audio (interleaved, per the device format). */
cellar_status_t cellar_audio_write(cellar_audio_device_t *dev,
                                   const void *data, uint32_t bytes);

/* Drop buffered audio (e.g. waveOutReset). */
cellar_status_t cellar_audio_reset(cellar_audio_device_t *dev);

/* Close the device and release resources. */
cellar_status_t cellar_audio_close(cellar_audio_device_t *dev);

/* Playback position in milliseconds from the start of this stream. */
uint64_t cellar_audio_position_ms(const cellar_audio_device_t *dev);

#endif /* CELLAR_AUDIO_H */
