/*
 * test_workspace.c — Airlock-style workspace manager tests.
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

#include "airlock/airlock.h"
#include "airlock/compat.h"
#include "airlock/plugin.h"
#include "airlock/prefix.h"
#include "airlock/win32.h"
#include "airlock/workspace.h"

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
    airlock_workspace_t w, listed[8];
    size_t n;

    snprintf(src, sizeof src, "%s/setup.exe", root);
    make_file(src, "fake windows installer workspace test\n");
    snprintf(exe, sizeof exe, "%s/game.exe", root);
    make_file(exe, "fake game launcher\n");

    CHECK(airlock_workspace_install(root, "ExampleGame", AIRLOCK_SETUP_EXE,
                                   src, exe, &w) == AIRLOCK_OK, "install exe");
    CHECK(strcmp(w.name, "ExampleGame") == 0, "workspace name");
    CHECK(w.setup == AIRLOCK_SETUP_EXE, "setup kind");
    CHECK(w.has_shortcut == 1, "shortcut written");
    CHECK(w.install_size > 0, "install size recorded");

    n = airlock_workspace_list(root, listed, 8);
    CHECK(n == 1, "one workspace in library");
    CHECK(strcmp(listed[0].name, "ExampleGame") == 0, "library has app");

    CHECK(airlock_workspace_load(root, "ExampleGame", &w) == AIRLOCK_OK,
          "load workspace");
    CHECK(strcmp(w.executable, exe) == 0, "executable remembered");
    CHECK(strstr(w.runner, "wine") != NULL, "runner recorded");
    CHECK(w.permissions & AIRLOCK_PERM_NETWORK, "safe default network allowed");
}

static void test_settings(const char *root)
{
    airlock_workspace_t w;
    char perms[512];
    CHECK(airlock_workspace_set_permissions(root, "ExampleGame",
                                           AIRLOCK_PERM_SHARED_FILES) == AIRLOCK_OK,
          "set permissions");
    CHECK(airlock_workspace_set_perf_mode(root, "ExampleGame",
                                         AIRLOCK_PERF_PERFORMANCE) == AIRLOCK_OK,
          "set perf mode");
    CHECK(airlock_workspace_set_controls(root, "ExampleGame",
                                        "gamepad:deadzone=0.12,vibration=1") == AIRLOCK_OK,
          "set controls");
    CHECK(airlock_workspace_set_resolution(root, "ExampleGame", 1280, 720, 96, 1) == AIRLOCK_OK,
          "set resolution");
    CHECK(airlock_workspace_set(root, "ExampleGame", "favorite", "1") == AIRLOCK_OK,
          "set favorite");

    CHECK(airlock_workspace_load(root, "ExampleGame", &w) == AIRLOCK_OK, "reload");
    CHECK(w.permissions == AIRLOCK_PERM_SHARED_FILES, "permissions persisted");
    CHECK(w.perf_mode == AIRLOCK_PERF_PERFORMANCE, "perf mode persisted");
    CHECK(strstr(w.controls, "deadzone") != NULL, "controls persisted");
    CHECK(w.resolution_width == 1280 && w.resolution_height == 720, "resolution");
    CHECK(w.virtual_desktop == 1, "virtual desktop");
    CHECK(w.favorite == 1, "favorite persisted");

    airlock_workspace_permissions_text(w.permissions, perms, sizeof perms);
    CHECK(strstr(perms, "cannot use network") != NULL, "permission text denies net");
    CHECK(strstr(perms, "can read selected shared folders") != NULL,
          "permission text allows shared");
}

static void test_profiles(const char *root)
{
    airlock_profile_point_t v1, v2, back;
    char diff[2048];
    int ndiff;
    char exported[640], imported[640];

    memset(&v1, 0, sizeof v1);
    snprintf(v1.label, sizeof v1.label, "v1");
    snprintf(v1.app_name, sizeof v1.app_name, "ExampleGame");
    snprintf(v1.runner, sizeof v1.runner, "airlock-wine-10.x");
    snprintf(v1.architecture, sizeof v1.architecture, "x86");
    v1.windows_version = AIRLOCK_WIN_10;
    snprintf(v1.gfx_backend, sizeof v1.gfx_backend, "Vulkan");
    snprintf(v1.audio_backend, sizeof v1.audio_backend, "ALSA");
    snprintf(v1.dependencies, sizeof v1.dependencies, "vcruntime,directx");
    snprintf(v1.dll_overrides, sizeof v1.dll_overrides, "d3d11=native,builtin");
    snprintf(v1.resolution, sizeof v1.resolution, "1280x720");
    snprintf(v1.launch_executable, sizeof v1.launch_executable, "drive_c/Games/ExampleGame/game.exe");
    v1.trust = AIRLOCK_PROFILE_LOCAL;
    v1.version = 1;

    CHECK(airlock_workspace_profile_apply(root, "ExampleGame", &v1) == AIRLOCK_OK,
          "apply v1");
    CHECK(airlock_workspace_profile_load(root, "ExampleGame", "v1", &back) == AIRLOCK_OK,
          "load v1");
    CHECK(strcmp(back.gfx_backend, "Vulkan") == 0, "v1 gfx parsed");
    CHECK(strcmp(back.dll_overrides, "d3d11=native,builtin") == 0,
          "dll overrides parsed");

    v2 = v1;
    snprintf(v2.label, sizeof v2.label, "v2");
    snprintf(v2.gfx_backend, sizeof v2.gfx_backend, "OpenGL");
    v2.version = 2;
    CHECK(airlock_workspace_profile_save(root, "ExampleGame", &v2, NULL, 0) == AIRLOCK_OK,
          "save v2");
    ndiff = airlock_workspace_profile_diff(root, "ExampleGame", "v1", "v2",
                                          diff, sizeof diff);
    CHECK(ndiff > 0, "diff found changes");
    CHECK(strstr(diff, "graphics.backend") != NULL, "diff shows gfx change");

    {
        char list[8][64];
        size_t c = airlock_workspace_profile_list(root, "ExampleGame", list, 8);
        CHECK(c >= 2, "two saved profiles");
    }

    snprintf(exported, sizeof exported, "%s/example-community.profile", root);
    CHECK(airlock_workspace_profile_export(root, "ExampleGame", exported) == AIRLOCK_OK,
          "export profile");
    CHECK(airlock_workspace_profile_current(root, "ExampleGame", &back) == AIRLOCK_OK,
          "current profile exists");
    snprintf(imported, sizeof imported, "%s/imported.community.profile", root);
    CHECK(airlock_workspace_profile_import(root, "ExampleGame", exported) == AIRLOCK_OK,
          "import profile");
    (void)imported;
    CHECK(airlock_workspace_profile_load(root, "ExampleGame", "example-community",
                                        &back) == AIRLOCK_OK,
          "imported profile loadable");
    CHECK(back.trust == AIRLOCK_PROFILE_COMMUNITY, "import marked community");
}

static void test_snapshot_rollback(const char *root)
{
    char snapshot[64];
    size_t n;
    airlock_workspace_t w;

    CHECK(airlock_workspace_snapshot(root, "ExampleGame", "before-change") == AIRLOCK_OK,
          "snapshot");
    CHECK(airlock_workspace_set_permissions(root, "ExampleGame", 0) == AIRLOCK_OK,
          "change permissions");
    CHECK(airlock_workspace_rollback(root, "ExampleGame", "before-change") == AIRLOCK_OK,
          "rollback");
    CHECK(airlock_workspace_load(root, "ExampleGame", &w) == AIRLOCK_OK, "load after rollback");
    CHECK(w.permissions == AIRLOCK_PERM_SHARED_FILES, "permissions restored");

    n = airlock_workspace_snapshot_list(root, "ExampleGame", &snapshot, 1);
    CHECK(n >= 1, "snapshot listed");
}

static void test_doctor_support_diagnose(const char *root)
{
    airlock_doctor_report_t dr;
    char bundle[640], logpath[640], diag[2048], safety[2048];
    char buf[2048];
    FILE *f;

    CHECK(airlock_workspace_doctor(root, "ExampleGame", &dr) == AIRLOCK_OK,
          "doctor run");
    CHECK(dr.count > 5, "doctor produced checks");
    CHECK(dr.ready != 0, "workspace ready (exe exists)");

    snprintf(bundle, sizeof bundle, "%s/support.txt", root);
    CHECK(airlock_workspace_support(root, "ExampleGame", bundle) == AIRLOCK_OK,
          "support bundle");
    CHECK(read_text(bundle, buf, sizeof buf) > 0, "bundle readable");
    CHECK(strstr(buf, "Airlock support bundle") != NULL, "bundle header");
    CHECK(strstr(buf, "verdict") != NULL, "bundle has doctor verdict");

    snprintf(logpath, sizeof logpath, "%s/wine.log", root);
    f = fopen(logpath, "w");
    CHECK(f != NULL, "create log");
    if (f) {
        fputs("wine: fixme:vulkan:Vulkan initialization failed\n", f);
        fputs("box64: translation fallback\n", f);
        fclose(f);
    }
    CHECK(airlock_workspace_diagnose(logpath, diag, sizeof diag) == AIRLOCK_OK,
          "diagnose log");
    CHECK(strstr(diag, "[gfx]") != NULL, "vulkan error classified");
    CHECK(strstr(diag, "[box64]") != NULL, "box64 note classified");

    CHECK(airlock_workspace_safety_report(root, "ExampleGame", safety, sizeof safety) == AIRLOCK_OK,
          "safety report");
    CHECK(strstr(safety, "is not made safe") != NULL, "malware warning present");

    CHECK(airlock_workspace_repair(root, "ExampleGame") == AIRLOCK_OK, "repair workspace");
}

static void test_shader_device_size(const char *root)
{
    char ws[640], cache[720], file[720];
    FILE *f;
    airlock_workspace_t w;
    uint64_t sz;

    CHECK(airlock_workspace_load(root, "ExampleGame", &w) == AIRLOCK_OK, "load");
    snprintf(ws, sizeof ws, "%s", w.path);
    airlock_strlcpy(cache, sizeof cache, ws);
    airlock_strlcat(cache, sizeof cache, "/shadercache");
    airlock_mkdir_p(cache);
    airlock_strlcpy(file, sizeof file, cache);
    airlock_strlcat(file, sizeof file, "/dx11.bin");
    f = fopen(file, "wb");
    CHECK(f != NULL, "create shader blob");
    if (f) {
        unsigned char blob[1024];
        memset(blob, 0xAB, sizeof blob);
        fwrite(blob, 1, sizeof blob, f);
        fclose(f);
    }
    sz = airlock_workspace_shader_size(root, "ExampleGame");
    CHECK(sz >= 1024, "shader cache size counted");
    CHECK(airlock_workspace_shader_clear(root, "ExampleGame") == AIRLOCK_OK,
          "shader cache clear");
    CHECK(airlock_workspace_shader_size(root, "ExampleGame") == 0,
          "shader cache empty after clear");

    CHECK(airlock_workspace_size(root, "ExampleGame") > 0, "workspace size");
    CHECK(airlock_device_report(file, sizeof file) == AIRLOCK_OK, "device report");
    CHECK(strstr(file, "OS:") != NULL, "device report has OS");
    CHECK(strstr(file, "Renderer:") != NULL, "device report has renderer");
}

int main(void)
{
    char root[256];
    snprintf(root, sizeof root, "/tmp/airlock-ws-%u", (unsigned)getpid());
    airlock_win32_init();
    airlock_backend_init();
    airlock_mkdir_p(root);

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
