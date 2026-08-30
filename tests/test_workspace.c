/*
 * test_workspace.c — Winaltor-style workspace manager tests.
 *
 * Covers guided setup, the app library, versioned profiles, profile diffs,
 * snapshots/rollback, launch doctor, support bundles, log diagnostics,
 * permissions, performance modes, controls, shader cache, and device report.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cellar/cellar.h"
#include "cellar/compat.h"
#include "cellar/plugin.h"
#include "cellar/prefix.h"
#include "cellar/win32.h"
#include "cellar/workspace.h"

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

static size_t read_text(const char *path, char *buf, size_t cap)
{
    FILE *f;
    size_t n;
    if (!path || !buf || cap == 0)
        return 0;
    f = fopen(path, "rb");
    if (!f)
        return 0;
    n = fread(buf, 1, cap - 1, f);
    fclose(f);
    buf[n] = '\0';
    return n;
}

static void make_file(const char *path, const char *text)
{
    FILE *f = fopen(path, "w");
    if (!f) { perror("make_file"); return; }
    fputs(text, f);
    fclose(f);
}

static void test_install_list_load(const char *root)
{
    char src[640], exe[640];
    cellar_workspace_t w, listed[8];
    size_t n;

    snprintf(src, sizeof src, "%s/setup.exe", root);
    make_file(src, "fake windows installer workspace test\n");
    snprintf(exe, sizeof exe, "%s/game.exe", root);
    make_file(exe, "fake game launcher\n");

    CHECK(cellar_workspace_install(root, "ExampleGame", CELLAR_SETUP_EXE,
                                   src, exe, &w) == CELLAR_OK, "install exe");
    CHECK(strcmp(w.name, "ExampleGame") == 0, "workspace name");
    CHECK(w.setup == CELLAR_SETUP_EXE, "setup kind");
    CHECK(w.has_shortcut == 1, "shortcut written");
    CHECK(w.install_size > 0, "install size recorded");

    n = cellar_workspace_list(root, listed, 8);
    CHECK(n == 1, "one workspace in library");
    CHECK(strcmp(listed[0].name, "ExampleGame") == 0, "library has app");

    CHECK(cellar_workspace_load(root, "ExampleGame", &w) == CELLAR_OK,
          "load workspace");
    CHECK(strcmp(w.executable, exe) == 0, "executable remembered");
    CHECK(strstr(w.runner, "wine") != NULL, "runner recorded");
    CHECK(w.permissions & CELLAR_PERM_NETWORK, "safe default network allowed");
}

static void test_settings(const char *root)
{
    cellar_workspace_t w;
    char perms[512];
    CHECK(cellar_workspace_set_permissions(root, "ExampleGame",
                                           CELLAR_PERM_SHARED_FILES) == CELLAR_OK,
          "set permissions");
    CHECK(cellar_workspace_set_perf_mode(root, "ExampleGame",
                                         CELLAR_PERF_PERFORMANCE) == CELLAR_OK,
          "set perf mode");
    CHECK(cellar_workspace_set_controls(root, "ExampleGame",
                                        "gamepad:deadzone=0.12,vibration=1") == CELLAR_OK,
          "set controls");
    CHECK(cellar_workspace_set_resolution(root, "ExampleGame", 1280, 720, 96, 1) == CELLAR_OK,
          "set resolution");
    CHECK(cellar_workspace_set(root, "ExampleGame", "favorite", "1") == CELLAR_OK,
          "set favorite");

    CHECK(cellar_workspace_load(root, "ExampleGame", &w) == CELLAR_OK, "reload");
    CHECK(w.permissions == CELLAR_PERM_SHARED_FILES, "permissions persisted");
    CHECK(w.perf_mode == CELLAR_PERF_PERFORMANCE, "perf mode persisted");
    CHECK(strstr(w.controls, "deadzone") != NULL, "controls persisted");
    CHECK(w.resolution_width == 1280 && w.resolution_height == 720, "resolution");
    CHECK(w.virtual_desktop == 1, "virtual desktop");
    CHECK(w.favorite == 1, "favorite persisted");

    cellar_workspace_permissions_text(w.permissions, perms, sizeof perms);
    CHECK(strstr(perms, "cannot use network") != NULL, "permission text denies net");
    CHECK(strstr(perms, "can read selected shared folders") != NULL,
          "permission text allows shared");
}

static void test_profiles(const char *root)
{
    cellar_profile_point_t v1, v2, back;
    char diff[2048];
    int ndiff;
    char exported[640], imported[640];

    memset(&v1, 0, sizeof v1);
    snprintf(v1.label, sizeof v1.label, "v1");
    snprintf(v1.app_name, sizeof v1.app_name, "ExampleGame");
    snprintf(v1.runner, sizeof v1.runner, "winaltor-wine-10.x");
    snprintf(v1.architecture, sizeof v1.architecture, "x86");
    v1.windows_version = CELLAR_WIN_10;
    snprintf(v1.gfx_backend, sizeof v1.gfx_backend, "Vulkan");
    snprintf(v1.audio_backend, sizeof v1.audio_backend, "ALSA");
    snprintf(v1.dependencies, sizeof v1.dependencies, "vcruntime,directx");
    snprintf(v1.dll_overrides, sizeof v1.dll_overrides, "d3d11=native,builtin");
    snprintf(v1.resolution, sizeof v1.resolution, "1280x720");
    snprintf(v1.launch_executable, sizeof v1.launch_executable, "drive_c/Games/ExampleGame/game.exe");
    v1.trust = CELLAR_PROFILE_LOCAL;
    v1.version = 1;

    CHECK(cellar_workspace_profile_apply(root, "ExampleGame", &v1) == CELLAR_OK,
          "apply v1");
    CHECK(cellar_workspace_profile_load(root, "ExampleGame", "v1", &back) == CELLAR_OK,
          "load v1");
    CHECK(strcmp(back.gfx_backend, "Vulkan") == 0, "v1 gfx parsed");
    CHECK(strcmp(back.dll_overrides, "d3d11=native,builtin") == 0,
          "dll overrides parsed");

    v2 = v1;
    snprintf(v2.label, sizeof v2.label, "v2");
    snprintf(v2.gfx_backend, sizeof v2.gfx_backend, "OpenGL");
    v2.version = 2;
    CHECK(cellar_workspace_profile_save(root, "ExampleGame", &v2, NULL, 0) == CELLAR_OK,
          "save v2");
    ndiff = cellar_workspace_profile_diff(root, "ExampleGame", "v1", "v2",
                                          diff, sizeof diff);
    CHECK(ndiff > 0, "diff found changes");
    CHECK(strstr(diff, "graphics.backend") != NULL, "diff shows gfx change");

    {
        char list[8][64];
        size_t c = cellar_workspace_profile_list(root, "ExampleGame", list, 8);
        CHECK(c >= 2, "two saved profiles");
    }

    snprintf(exported, sizeof exported, "%s/example-community.profile", root);
    CHECK(cellar_workspace_profile_export(root, "ExampleGame", exported) == CELLAR_OK,
          "export profile");
    CHECK(cellar_workspace_profile_current(root, "ExampleGame", &back) == CELLAR_OK,
          "current profile exists");
    snprintf(imported, sizeof imported, "%s/imported.community.profile", root);
    CHECK(cellar_workspace_profile_import(root, "ExampleGame", exported) == CELLAR_OK,
          "import profile");
    (void)imported;
    CHECK(cellar_workspace_profile_load(root, "ExampleGame", "example-community",
                                        &back) == CELLAR_OK,
          "imported profile loadable");
    CHECK(back.trust == CELLAR_PROFILE_COMMUNITY, "import marked community");
}

static void test_snapshot_rollback(const char *root)
{
    char snapshot[64];
    size_t n;
    cellar_workspace_t w;

    CHECK(cellar_workspace_snapshot(root, "ExampleGame", "before-change") == CELLAR_OK,
          "snapshot");
    CHECK(cellar_workspace_set_permissions(root, "ExampleGame", 0) == CELLAR_OK,
          "change permissions");
    CHECK(cellar_workspace_rollback(root, "ExampleGame", "before-change") == CELLAR_OK,
          "rollback");
    CHECK(cellar_workspace_load(root, "ExampleGame", &w) == CELLAR_OK, "load after rollback");
    CHECK(w.permissions == CELLAR_PERM_SHARED_FILES, "permissions restored");

    n = cellar_workspace_snapshot_list(root, "ExampleGame", &snapshot, 1);
    CHECK(n >= 1, "snapshot listed");
}

static void test_doctor_support_diagnose(const char *root)
{
    cellar_doctor_report_t dr;
    char bundle[640], logpath[640], diag[2048], safety[2048];
    char buf[2048];
    FILE *f;

    CHECK(cellar_workspace_doctor(root, "ExampleGame", &dr) == CELLAR_OK,
          "doctor run");
    CHECK(dr.count > 5, "doctor produced checks");
    CHECK(dr.ready != 0, "workspace ready (exe exists)");

    snprintf(bundle, sizeof bundle, "%s/support.txt", root);
    CHECK(cellar_workspace_support(root, "ExampleGame", bundle) == CELLAR_OK,
          "support bundle");
    CHECK(read_text(bundle, buf, sizeof buf) > 0, "bundle readable");
    CHECK(strstr(buf, "Winaltor support bundle") != NULL, "bundle header");
    CHECK(strstr(buf, "verdict") != NULL, "bundle has doctor verdict");

    snprintf(logpath, sizeof logpath, "%s/wine.log", root);
    f = fopen(logpath, "w");
    CHECK(f != NULL, "create log");
    if (f) {
        fputs("wine: fixme:vulkan:Vulkan initialization failed\n", f);
        fputs("box64: translation fallback\n", f);
        fclose(f);
    }
    CHECK(cellar_workspace_diagnose(logpath, diag, sizeof diag) == CELLAR_OK,
          "diagnose log");
    CHECK(strstr(diag, "[gfx]") != NULL, "vulkan error classified");
    CHECK(strstr(diag, "[box64]") != NULL, "box64 note classified");

    CHECK(cellar_workspace_safety_report(root, "ExampleGame", safety, sizeof safety) == CELLAR_OK,
          "safety report");
    CHECK(strstr(safety, "is not made safe") != NULL, "malware warning present");

    CHECK(cellar_workspace_repair(root, "ExampleGame") == CELLAR_OK, "repair workspace");
}

static void test_shader_device_size(const char *root)
{
    char ws[640], cache[720], file[720];
    FILE *f;
    cellar_workspace_t w;
    uint64_t sz;

    CHECK(cellar_workspace_load(root, "ExampleGame", &w) == CELLAR_OK, "load");
    snprintf(ws, sizeof ws, "%s", w.path);
    cellar_strlcpy(cache, sizeof cache, ws);
    cellar_strlcat(cache, sizeof cache, "/shadercache");
    cellar_mkdir_p(cache);
    cellar_strlcpy(file, sizeof file, cache);
    cellar_strlcat(file, sizeof file, "/dx11.bin");
    f = fopen(file, "wb");
    CHECK(f != NULL, "create shader blob");
    if (f) {
        unsigned char blob[1024];
        memset(blob, 0xAB, sizeof blob);
        fwrite(blob, 1, sizeof blob, f);
        fclose(f);
    }
    sz = cellar_workspace_shader_size(root, "ExampleGame");
    CHECK(sz >= 1024, "shader cache size counted");
    CHECK(cellar_workspace_shader_clear(root, "ExampleGame") == CELLAR_OK,
          "shader cache clear");
    CHECK(cellar_workspace_shader_size(root, "ExampleGame") == 0,
          "shader cache empty after clear");

    CHECK(cellar_workspace_size(root, "ExampleGame") > 0, "workspace size");
    CHECK(cellar_device_report(file, sizeof file) == CELLAR_OK, "device report");
    CHECK(strstr(file, "OS:") != NULL, "device report has OS");
    CHECK(strstr(file, "Renderer:") != NULL, "device report has renderer");
}

int main(void)
{
    char root[256];
    snprintf(root, sizeof root, "/tmp/cellar-ws-%u", (unsigned)getpid());
    cellar_win32_init();
    cellar_backend_init();
    cellar_mkdir_p(root);

    test_install_list_load(root);
    test_settings(root);
    test_profiles(root);
    test_snapshot_rollback(root);
    test_doctor_support_diagnose(root);
    test_shader_device_size(root);

    if (g_failures == 0) {
        printf("test_workspace: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_workspace: %d test(s) failed\n", g_failures);
    return 1;
}
