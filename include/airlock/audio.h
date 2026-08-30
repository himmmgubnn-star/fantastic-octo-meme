/*
 * audio.h — Airlock audio backend API.
 *
 * Games output audio through WinMM (winmm.dll), DirectSound (dsound.dll),
 * XAudio2, or OpenAL. Airlock funnels those into a small set of *backends* so
 * the Win32 API layer never touches hardware directly:
 *
 *   Win32 (winmm.dll)  ->  airlock_audio_device_t  ->  audio backend
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
#ifndef AIRLOCK_AUDIO_H
#define AIRLOCK_AUDIO_H

#include <stdint.h>

#include "airlock.h"

/* PCM format description (mirrors WAVEFORMATEX fields Windows uses). */
typedef struct airlock_audio_format {
    uint16_t format_tag;        /* 1 = PCM (WAVE_FORMAT_PCM)               */
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t avg_bytes_per_sec;
    uint16_t block_align;
    uint16_t bits_per_sample;
} airlock_audio_format_t;

struct airlock_audio_device;

/* One backend implementation. */
typedef struct airlock_audio_backend {
    const char *name;
    airlock_status_t (*open)(struct airlock_audio_device *dev,
                            const airlock_audio_format_t *fmt);
    airlock_status_t (*write)(struct airlock_audio_device *dev,
                             const void *data, uint32_t bytes);
    airlock_status_t (*reset)(struct airlock_audio_device *dev);
    airlock_status_t (*close)(struct airlock_audio_device *dev);
    uint64_t (*position_ms)(const struct airlock_audio_device *dev);
} airlock_audio_backend_t;

/* An open audio stream. `impl` is private to the backend. */
typedef struct airlock_audio_device {
    const airlock_audio_backend_t *backend;
    void *impl;
} airlock_audio_device_t;

/* ---- API ----------------------------------------------------------------- */

/* Select the default backend. Returns the built-in WAV sink when the optional
 * ALSA backend is not compiled in. Never returns NULL. */
const airlock_audio_backend_t *airlock_audio_default_backend(void);

/* Open a device on the given backend with the given PCM format. */
airlock_status_t airlock_audio_open(airlock_audio_device_t *dev,
                                  const airlock_audio_backend_t *backend,
                                  const airlock_audio_format_t *fmt);

/* Push `bytes` of PCM audio (interleaved, per the device format). */
airlock_status_t airlock_audio_write(airlock_audio_device_t *dev,
                                   const void *data, uint32_t bytes);

/* Drop buffered audio (e.g. waveOutReset). */
airlock_status_t airlock_audio_reset(airlock_audio_device_t *dev);

/* Close the device and release resources. */
airlock_status_t airlock_audio_close(airlock_audio_device_t *dev);

/* Playback position in milliseconds from the start of this stream. */
uint64_t airlock_audio_position_ms(const airlock_audio_device_t *dev);

#endif /* AIRLOCK_AUDIO_H */
