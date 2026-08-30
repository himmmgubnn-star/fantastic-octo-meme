/*
 * test_compat.c — tests for the Application Compatibility Analyzer, Windows
 * version behavior profiles, per-application profiles, crash diagnostics, and
 * backend hot-selection.
 *
 * SPDX-License-Identifier: MIT
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cellar/cellar.h"
#include "cellar/compat.h"
#include "cellar/crash.h"
#include "cellar/loader.h"
#include "cellar/plugin.h"
#include "cellar/win32.h"

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

/* ---- minimal PE32 builder with a two-entry import table ------------------ */

typedef struct pebuf { unsigned char *p; size_t n; } pebuf_t;

static void p16(unsigned char *b, size_t o, unsigned v)
{ b[o]=(unsigned char)(v&0xFF); b[o+1]=(unsigned char)((v>>8)&0xFF); }
static void p32(unsigned char *b, size_t o, unsigned long v)
{ size_t i; for(i=0;i<4;i++) b[o+i]=(unsigned char)((v>>(8*i))&0xFF); }

#define PE_SEC_VA 0x1000
#define PE_RAW_OFF 0x200
#define FO(r) (PE_RAW_OFF + ((r) - PE_SEC_VA))

static int build_pe(pebuf_t *b, const char *m1, const char *f1,
                    const char *m2, const char *f2)
{
    size_t opt = 224, sec_off = 0x58 + opt, total = PE_RAW_OFF + 0x600;
    size_t off;
    b->p = calloc(1, total);
    if (!b->p) return 0;
    b->n = total;
    p16(b->p, 0x00, 0x5A4D); p32(b->p, 0x3C, 0x40);
    p32(b->p, 0x40, 0x00004550);
    p16(b->p, 0x44, 0x014C); p16(b->p, 0x46, 1);
    p16(b->p, 0x54, (unsigned short)opt); p16(b->p, 0x56, 0x0002);
    p16(b->p, 0x58, 0x010B);
    p32(b->p, 0x68, PE_SEC_VA); p32(b->p, 0x74, 0x00400000);
    p32(b->p, 0x7C, 0x1000); p32(b->p, 0x80, 0x200);
    p16(b->p, 0x8C, 6); p32(b->p, 0x94, 0x2000); p32(b->p, 0x98, 0x200);
    p16(b->p, 0xA0, 3); p32(b->p, 0xC8, 16);
    p32(b->p, 0xCC + 8, 0x1000); p32(b->p, 0xCC + 8 + 4, 0x60);

    memcpy(b->p + sec_off, ".text", 6);
    p32(b->p, sec_off+8, 0x1000); p32(b->p, sec_off+12, PE_SEC_VA);
    p32(b->p, sec_off+16, 0x600); p32(b->p, sec_off+20, PE_RAW_OFF);
    p32(b->p, sec_off+36, 0x60000020);

    /* Import descriptors: #1 (kernel32) and #2 (user32), then terminator. */
    p32(b->p, FO(0x1000), 0); p32(b->p, FO(0x1004), 0); p32(b->p, FO(0x1008), 0);
    p32(b->p, FO(0x100C), 0x1040); p32(b->p, FO(0x1010), 0x1030); /* kernel32 */
    p32(b->p, FO(0x1014), 0); p32(b->p, FO(0x1018), 0); p32(b->p, FO(0x101C), 0);
    p32(b->p, FO(0x1020), 0x1080); p32(b->p, FO(0x1024), 0x1070); /* user32   */
    for (off = 0x1028; off < 0x1040; off += 4) p32(b->p, FO(off), 0);

    /* kernel32 IAT[0] -> name at 0x1100 ; user32 IAT[0] -> name at 0x1110 */
    p32(b->p, FO(0x1030), 0x1100); p32(b->p, FO(0x1034), 0);
    p32(b->p, FO(0x1070), 0x1110); p32(b->p, FO(0x1074), 0);

    memcpy(b->p + FO(0x1040), m1, strlen(m1)+1);
    memcpy(b->p + FO(0x1080), m2, strlen(m2)+1);
    p16(b->p, FO(0x1100), 0); memcpy(b->p + FO(0x1102), f1, strlen(f1)+1);
    p16(b->p, FO(0x1110), 0); memcpy(b->p + FO(0x1112), f2, strlen(f2)+1);
    return 1;
}

/* ---- analysis ------------------------------------------------------------- */

static void test_analysis(void)
{
    pebuf_t pe;
    cellar_image_t img;
    cellar_analysis_t a;
    cellar_status_t st;
    int i;

    CHECK(build_pe(&pe, "KERNEL32.dll", "ExitProcess",
                   "USER32.dll", "MessageBoxA"), "build pe");

    st = cellar_image_load_buffer(pe.p, pe.n, CELLAR_LOAD_DEFAULT, &img);
    CHECK(st == CELLAR_OK, "load buffer");
    if (st != CELLAR_OK) { free(pe.p); return; }

    st = cellar_compat_analyze(&img, "game.exe", &a);
    CHECK(st == CELLAR_OK, "analyze ok");
    CHECK(a.is_64bit == 0, "x86 detected");
    CHECK(a.missing_count == 1, "one missing API (user32!MessageBoxA)");
    CHECK(a.scores[CELLAR_CAT_SYSTEM].total_imports == 2,
          "both imports classified into Windows-API category");
    CHECK(a.scores[CELLAR_CAT_SYSTEM].supported_imports == 1,
          "ExitProcess supported, MessageBoxA not");
    CHECK(a.scores[CELLAR_CAT_SYSTEM].percent == 50,
          "Windows-API coverage 50%");
    CHECK(a.overall_percent == 50, "overall 50%");
    CHECK(a.missing[0].called_by[0] != '\0', "called_by recorded");

    /* Verify the missing-API diagnostic has all fields. */
    CHECK(strstr(a.missing[0].recommendation, "USER32.dll") != NULL,
          "recommendation mentions the module");

    /* Report renderer must not crash and must mention the exe. */
    cellar_compat_report(&a);

    cellar_image_unload(&img);
    free(pe.p);
}

/* ---- version profiles ----------------------------------------------------- */

static void test_version_profiles(void)
{
    const cellar_version_profile_t *w7 = cellar_version_profile(CELLAR_WIN_7);
    const cellar_version_profile_t *w11 = cellar_version_profile(CELLAR_WIN_11);

    CHECK(w7 != NULL && w11 != NULL, "profiles exist");
    CHECK(w7->major == 6 && w7->minor == 1, "Win7 is 6.1");
    CHECK(w11->major == 10 && w11->build > w7->build, "Win11 build > Win7 build");
    /* Behavioral differences must actually differ, not just the version. */
    CHECK(!w7->high_dpi_aware_by_default && w11->high_dpi_aware_by_default,
          "DPI-awareness behavior differs between modes");
    CHECK(!w7->touch_input_available && w11->touch_input_available,
          "touch-input behavior differs");
    CHECK(!w7->modern_threadpool && w11->modern_threadpool,
          "threadpool behavior differs");
    CHECK(!w7->arm_translation && w11->arm_translation,
          "ARM translation behavior differs");
}

/* ---- per-app profiles ----------------------------------------------------- */

static void test_app_profiles(void)
{
    cellar_app_profile_t p, back;
    cellar_status_t st;
    char dir[512];
    snprintf(dir, sizeof dir, "%s/cellar-test-prefix", getenv("HOME")?getenv("HOME"):".");

    memset(&p, 0, sizeof p);
    snprintf(p.app_name, sizeof p.app_name, "game.exe");
    p.version_mode = CELLAR_WIN_10;
    snprintf(p.gfx_backend, sizeof p.gfx_backend, "Vulkan");
    snprintf(p.audio_backend, sizeof p.audio_backend, "ALSA");
    snprintf(p.dll_overrides, sizeof p.dll_overrides, "d3d9=native");
    p.low_latency_sync = 1;
    p.shader_cache_enabled = 1;
    p.last_good = 0;

    st = cellar_profile_save(dir, &p);
    CHECK(st == CELLAR_OK, "profile saved");
    st = cellar_profile_load(dir, "game.exe", &back);
    CHECK(st == CELLAR_OK, "profile loaded");
    CHECK(strcmp(back.gfx_backend, "Vulkan") == 0, "gfx backend remembered");
    CHECK(strcmp(back.audio_backend, "ALSA") == 0, "audio backend remembered");
    CHECK(back.version_mode == CELLAR_WIN_10, "version mode remembered");
    CHECK(back.low_latency_sync == 1, "sync setting remembered");
    CHECK(back.shader_cache_enabled == 1, "shader cache remembered");

    /* Remember a working configuration. */
    st = cellar_profile_mark_last_good(dir, &p);
    CHECK(st == CELLAR_OK, "mark last-good saved");
    st = cellar_profile_load(dir, "game.exe", &back);
    CHECK(back.last_good == 1, "last-good flag persisted");
}

/* ---- crash diagnostics ---------------------------------------------------- */

static void test_crash(void)
{
    cellar_crash_info_t ci;
    char buf[1024];
    cellar_crash_set_current_api("audio", "winmm!waveOutWrite");
    cellar_crash_fill(&ci, 11, (uintptr_t)0x1234);
    cellar_crash_format(&ci, buf, sizeof buf);
    CHECK(strstr(buf, "EXCEPTION") != NULL, "crash report has EXCEPTION");
    CHECK(strstr(buf, "winmm!waveOutWrite") != NULL, "crash report has API call");
    CHECK(ci.thread_id != 0, "crash report has thread id");
}

/* ---- backend hot-selection ------------------------------------------------ */

static void test_backends(void)
{
    cellar_backend_init();
    CHECK(strcmp(cellar_backend_pick(CELLAR_BACKEND_GRAPHICS), "Vulkan") == 0,
          "graphics picks Vulkan first");
    cellar_backend_set_available("Vulkan", 0);
    CHECK(strcmp(cellar_backend_pick(CELLAR_BACKEND_GRAPHICS), "OpenGL") == 0,
          "falls back to OpenGL");
    cellar_backend_set_available("OpenGL", 0);
    CHECK(strcmp(cellar_backend_pick(CELLAR_BACKEND_GRAPHICS), "Software") == 0,
          "emergency fallback Software");
}

int main(void)
{
    cellar_win32_init();
    test_analysis();
    test_version_profiles();
    test_app_profiles();
    test_crash();
    test_backends();

    if (g_failures == 0) {
        printf("test_compat: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_compat: %d test(s) failed\n", g_failures);
    return 1;
}
