/*
 * mod_winmm.c — WINMM.dll (Windows Multimedia) implementation.
 *
 * WINMM is the multimedia DLL games have used for decades — waveOut* for
 * audio output and timeGetTime for timing. Cellar routes waveOut through the
 * audio backend (see <cellar/audio.h>), so a game's audio can go to a WAV
 * file sink (default), ALSA, or a null sink without the Win32 layer knowing
 * anything about the hardware.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/audio.h"
#include "cellar/crash.h"
#include "cellar/perf.h"
#include "cellar/platform.h"
#include "cellar/win32.h"

#ifdef _WIN32
#define WINAPI __stdcall
#else
#define WINAPI
#endif

/* ---- Windows multimedia types -------------------------------------------- */

typedef unsigned short  UINT;
typedef unsigned int    UINT_PTR;
typedef unsigned short  UINT16;
typedef unsigned int    UINT32;
typedef unsigned long   DWORD;
typedef uintptr_t       DWORD_PTR;
typedef void           *HWAVEOUT;
typedef void           *HMODULE;

#define WAVE_FORMAT_PCM 1u

/* Error codes (subset of mmsystem.h). */
#define MMSYSERR_NOERROR      0
#define MMSYSERR_ERROR        1
#define MMSYSERR_BADDEVICEID  2
#define MMSYSERR_NOTENABLED   3
#define MMSYSERR_ALLOCATED    4
#define MMSYSERR_INVALHANDLE  5
#define MMSYSERR_NODRIVER     6
#define MMSYSERR_NOMEM        7
#define MMSYSERR_NOTSUPPORTED 8

#define WAVE_OPEN_QUERY 0x00000001u
#define WAVE_FORMAT_QUERY 0x00000001u

/* WAVEFORMATEX — the format structure passed to waveOutOpen. */
typedef struct WAVEFORMATEX {
    UINT16 wFormatTag;
    UINT16 nChannels;
    DWORD  nSamplesPerSec;
    DWORD  nAvgBytesPerSec;
    UINT16 nBlockAlign;
    UINT16 wBitsPerSample;
    UINT16 cbSize;
} WAVEFORMATEX;

/* WAVEHDR — the buffer descriptor passed to waveOutWrite. Layout matches the
 * public Windows definition on a 64-bit host. */
typedef struct WAVEHDR {
    char      *lpData;
    DWORD      dwBufferLength;
    DWORD      dwBytesRecorded;
    DWORD_PTR  dwUser;
    DWORD      dwFlags;
    DWORD      dwLoops;
    struct WAVEHDR *lpNext;
    DWORD_PTR  reserved;
} WAVEHDR;

#define WHDR_DONE    0x00000001u
#define WHDR_PREPARED 0x00000002u

/* WAVEOUTCAPS. */
typedef struct WAVEOUTCAPS {
    UINT16 wMid;
    UINT16 wPid;
    uint32_t vDriverVersion;
    char   szPname[32];
    DWORD  dwFormats;
    UINT16 wChannels;
    UINT16 wReserved1;
    DWORD  dwSupport;
} WAVEOUTCAPS;

/* ---- Device table --------------------------------------------------------- */

#define CELLAR_MAX_AUDIO_DEVICES 8

typedef struct cellar_mm_device {
    cellar_audio_device_t audio;
    cellar_audio_format_t fmt;
    int  open;
} cellar_mm_device_t;

static cellar_mm_device_t g_devices[CELLAR_MAX_AUDIO_DEVICES];

#define MMDEV_INVALID ((void *)(uintptr_t)-1)

static cellar_mm_device_t *dev_from_handle(HWAVEOUT h)
{
    uintptr_t idx = (uintptr_t)h;
    if (idx == 0 || idx > CELLAR_MAX_AUDIO_DEVICES)
        return NULL;
    return &g_devices[idx - 1];
}

static HWAVEOUT dev_to_handle(const cellar_mm_device_t *d)
{
    return (HWAVEOUT)(uintptr_t)(d - g_devices + 1);
}

/* ---- Implementations ------------------------------------------------------ */

static UINT WINAPI cellar_waveOutGetNumDevs(void)
{
    return 1; /* one logical device backed by the default audio backend */
}

static UINT WINAPI cellar_waveOutGetDevCaps(UINT uDeviceID, void *caps,
                                            UINT cbCaps)
{
    WAVEOUTCAPS *c = caps;
    if (uDeviceID != 0 || !caps || cbCaps < sizeof(WAVEOUTCAPS))
        return MMSYSERR_BADDEVICEID;
    memset(c, 0, sizeof *c);
    c->wMid = (UINT16)0xFFFE;
    c->wChannels = (UINT16)2;
    snprintf(c->szPname, sizeof c->szPname, "Cellar Audio (default)");
    return MMSYSERR_NOERROR;
}

static UINT WINAPI cellar_waveOutOpen(HWAVEOUT *phwo, UINT uDeviceID,
                                      const WAVEFORMATEX *pwfx,
                                      void *dwCallback, DWORD dwInstance,
                                      DWORD fdwOpen)
{
    cellar_mm_device_t *d;
    const cellar_audio_backend_t *backend;
    UINT i;
    (void)dwCallback; (void)dwInstance;

    if (!phwo)
        return MMSYSERR_INVALHANDLE;
    if (uDeviceID != 0)
        return MMSYSERR_BADDEVICEID;
    if (!pwfx || pwfx->wFormatTag != WAVE_FORMAT_PCM)
        return MMSYSERR_NOTSUPPORTED;
    if (fdwOpen & (WAVE_OPEN_QUERY | WAVE_FORMAT_QUERY))
        return MMSYSERR_NOERROR; /* format is valid */

    /* find a free device slot */
    d = NULL;
    for (i = 0; i < CELLAR_MAX_AUDIO_DEVICES; i++) {
        if (!g_devices[i].open) { d = &g_devices[i]; break; }
    }
    if (!d)
        return MMSYSERR_ALLOCATED;

    d->fmt.format_tag = pwfx->wFormatTag;
    d->fmt.channels = pwfx->nChannels;
    d->fmt.sample_rate = (uint32_t)pwfx->nSamplesPerSec;
    d->fmt.bits_per_sample = pwfx->wBitsPerSample;
    d->fmt.block_align = pwfx->nBlockAlign;
    d->fmt.avg_bytes_per_sec = (uint32_t)pwfx->nAvgBytesPerSec;

    backend = cellar_audio_default_backend();
    if (cellar_audio_open(&d->audio, backend, &d->fmt) != CELLAR_OK)
        return MMSYSERR_NODRIVER;

    d->open = 1;
    *phwo = dev_to_handle(d);
    CELLAR_LOG_INFO("WINMM.waveOutOpen: %lu Hz, %u ch, %u-bit -> %s",
                    (unsigned long)pwfx->nSamplesPerSec, pwfx->nChannels,
                    pwfx->wBitsPerSample, backend->name);
    return MMSYSERR_NOERROR;
}

static UINT WINAPI cellar_waveOutClose(HWAVEOUT hwo)
{
    cellar_mm_device_t *d = dev_from_handle(hwo);
    if (!d || !d->open)
        return MMSYSERR_INVALHANDLE;
    cellar_audio_close(&d->audio);
    memset(&d->audio, 0, sizeof d->audio);
    d->open = 0;
    return MMSYSERR_NOERROR;
}

static UINT WINAPI cellar_waveOutPrepareHeader(HWAVEOUT hwo, WAVEHDR *pwh,
                                               UINT cbwh)
{
    cellar_mm_device_t *d = dev_from_handle(hwo);
    (void)cbwh;
    if (!d || !d->open)
        return MMSYSERR_INVALHANDLE;
    if (!pwh)
        return MMSYSERR_ERROR;
    pwh->dwFlags |= WHDR_PREPARED;
    return MMSYSERR_NOERROR;
}

static UINT WINAPI cellar_waveOutUnprepareHeader(HWAVEOUT hwo, WAVEHDR *pwh,
                                                 UINT cbwh)
{
    cellar_mm_device_t *d = dev_from_handle(hwo);
    (void)cbwh;
    if (!d || !d->open)
        return MMSYSERR_INVALHANDLE;
    if (pwh)
        pwh->dwFlags &= ~(WHDR_PREPARED);
    return MMSYSERR_NOERROR;
}

static UINT WINAPI cellar_waveOutWrite(HWAVEOUT hwo, WAVEHDR *pwh, UINT cbwh)
{
    cellar_mm_device_t *d = dev_from_handle(hwo);
    (void)cbwh;
    cellar_crash_set_current_api("audio", "winmm!waveOutWrite");
    if (!d || !d->open)
        return MMSYSERR_INVALHANDLE;
    if (!pwh || !pwh->lpData || pwh->dwBufferLength == 0)
        return MMSYSERR_ERROR;

    if (cellar_audio_write(&d->audio, pwh->lpData,
                           (uint32_t)pwh->dwBufferLength) != CELLAR_OK)
        return MMSYSERR_ERROR;

    cellar_perf_count_audio((uint64_t)pwh->dwBufferLength);
    pwh->dwFlags |= WHDR_DONE;
    return MMSYSERR_NOERROR;
}

static UINT WINAPI cellar_waveOutReset(HWAVEOUT hwo)
{
    cellar_mm_device_t *d = dev_from_handle(hwo);
    if (!d || !d->open)
        return MMSYSERR_INVALHANDLE;
    cellar_audio_reset(&d->audio);
    return MMSYSERR_NOERROR;
}

static UINT WINAPI cellar_waveOutGetPosition(HWAVEOUT hwo, void *lpmmt,
                                             UINT cbmmt)
{
    cellar_mm_device_t *d = dev_from_handle(hwo);
    uint64_t pos;
    (void)cbmmt;
    if (!d || !d->open)
        return MMSYSERR_INVALHANDLE;
    pos = cellar_audio_position_ms(&d->audio);
    if (cbmmt >= 8) {
        uint32_t *mm = lpmmt;
        mm[0] = 0;              /* TIME_MS */
        mm[1] = (uint32_t)pos;  /* milliseconds */
    }
    return MMSYSERR_NOERROR;
}

static int WINAPI cellar_PlaySoundA(const char *pszSound, HMODULE hmod,
                                    DWORD fdwSound)
{
    (void)pszSound; (void)hmod; (void)fdwSound;
    CELLAR_LOG_INFO("WINMM.PlaySoundA(\"%s\") — stub",
                    pszSound ? pszSound : "(null)");
    return 1; /* TRUE */
}

static DWORD WINAPI cellar_timeGetTime(void)
{
    return (DWORD)cellar_monotonic_ms();
}

static void WINAPI cellar_timeBeginPeriod(UINT uPeriod)
{
    (void)uPeriod; /* timing resolution is best-effort on POSIX */
}

static void WINAPI cellar_timeEndPeriod(UINT uPeriod)
{
    (void)uPeriod;
}

/* ---- Registered export table --------------------------------------------- */

static const cellar_export_entry_t k_winmm_exports[] = {
    { "waveOutGetNumDevs",     (void *)&cellar_waveOutGetNumDevs },
    { "waveOutGetDevCaps",     (void *)&cellar_waveOutGetDevCaps },
    { "waveOutOpen",           (void *)&cellar_waveOutOpen },
    { "waveOutClose",          (void *)&cellar_waveOutClose },
    { "waveOutPrepareHeader",  (void *)&cellar_waveOutPrepareHeader },
    { "waveOutUnprepareHeader",(void *)&cellar_waveOutUnprepareHeader },
    { "waveOutWrite",          (void *)&cellar_waveOutWrite },
    { "waveOutReset",          (void *)&cellar_waveOutReset },
    { "waveOutGetPosition",    (void *)&cellar_waveOutGetPosition },
    { "PlaySoundA",            (void *)&cellar_PlaySoundA },
    { "timeGetTime",           (void *)&cellar_timeGetTime },
    { "timeBeginPeriod",       (void *)&cellar_timeBeginPeriod },
    { "timeEndPeriod",         (void *)&cellar_timeEndPeriod },
};

static const cellar_module_t k_winmm_module = {
    "WINMM.dll",
    k_winmm_exports,
    sizeof k_winmm_exports / sizeof k_winmm_exports[0],
};

const cellar_module_t *cellar_win32_module_winmm(void)
{
    return &k_winmm_module;
}
