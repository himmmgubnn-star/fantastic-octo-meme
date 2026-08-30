/*
 * test_ecosystem.c — inspector, compatibility database, prefix manager,
 * runtime manager, shell, installer, COM, desktop, and the test lab.
 *
 * SPDX-License-Identifier: MIT
 */
#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cellar/cellar.h"
#include "cellar/com.h"
#include "cellar/compat.h"
#include "cellar/db.h"
#include "cellar/debug.h"
#include "cellar/desktop.h"
#include "cellar/inspect.h"
#include "cellar/installer.h"
#include "cellar/loader.h"
#include "cellar/locale.h"
#include "cellar/prefix.h"
#include "cellar/runtime.h"
#include "cellar/security.h"
#include "cellar/shell.h"
#include "cellar/testlab.h"
#include "cellar/win32.h"

static int g_failures = 0;
#define CHECK(cond, msg) \
    do { if (!(cond)) { g_failures++; \
         fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, msg); } } while (0)

/* ---- tiny PE32 (KERNEL32!ExitProcess) for the inspector ------------------ */

static void p16(unsigned char *b, size_t o, unsigned v)
{ b[o]=(unsigned char)(v&0xFF); b[o+1]=(unsigned char)((v>>8)&0xFF); }
static void p32(unsigned char *b, size_t o, unsigned long v)
{ size_t i; for(i=0;i<4;i++) b[o+i]=(unsigned char)((v>>(8*i))&0xFF); }

#define PE_SEC_VA 0x1000
#define PE_RAW_OFF 0x200
#define FO(r) (PE_RAW_OFF + ((r) - PE_SEC_VA))

static unsigned char *build_pe(size_t *n)
{
    size_t opt = 224, sec_off = 0x58 + opt, total = PE_RAW_OFF + 0x400;
    unsigned char *b = calloc(1, total);
    if (!b) return NULL;
    *n = total;
    p16(b, 0x00, 0x5A4D); p32(b, 0x3C, 0x40);
    p32(b, 0x40, 0x00004550);
    p16(b, 0x44, 0x014C); p16(b, 0x46, 1);
    p16(b, 0x54, (unsigned short)opt); p16(b, 0x56, 0x0002);
    p16(b, 0x58, 0x010B);
    p32(b, 0x68, PE_SEC_VA); p32(b, 0x74, 0x00400000);
    p32(b, 0x7C, 0x1000); p32(b, 0x80, 0x200);
    p16(b, 0x8C, 6); p32(b, 0x94, 0x2000); p32(b, 0x98, 0x200);
    p16(b, 0xA0, 3); p32(b, 0xC8, 16);
    p32(b, 0xCC + 8, 0x1000); p32(b, 0xCC + 8 + 4, 0x40);
    memcpy(b + sec_off, ".text", 6);
    p32(b, sec_off+8, 0x1000); p32(b, sec_off+12, PE_SEC_VA);
    p32(b, sec_off+16, 0x400); p32(b, sec_off+20, PE_RAW_OFF);
    p32(b, sec_off+36, 0x60000020);
    p32(b, FO(0x1000), 0); p32(b, FO(0x1004), 0); p32(b, FO(0x1008), 0);
    p32(b, FO(0x100C), 0x1040); p32(b, FO(0x1010), 0x1030);
    p32(b, FO(0x1030), 0x1060); p32(b, FO(0x1034), 0);
    memcpy(b + FO(0x1040), "KERNEL32.dll", 13);
    p16(b, FO(0x1060), 0); memcpy(b + FO(0x1062), "ExitProcess", 12);
    return b;
}

static void test_inspect_and_db(const char *tmpdir)
{
    size_t n = 0;
    unsigned char *pe = build_pe(&n);
    cellar_image_t img;
    cellar_inspect_t ins;
    cellar_analysis_t a;
    cellar_db_t db;
    cellar_db_entry_t e, loaded;
    char dbpath[512];

    CHECK(pe != NULL, "build pe");
    CHECK(cellar_image_load_buffer(pe, n, CELLAR_LOAD_DEFAULT, &img) == CELLAR_OK,
          "load pe");
    CHECK(cellar_inspect_image(&img, "/games/ExampleGame.exe", &ins) == CELLAR_OK,
          "inspect");
    CHECK(strcmp(ins.basename, "ExampleGame.exe") == 0, "basename");
    CHECK(ins.is_64bit == 0, "x86");
    CHECK(ins.unique_dll_count == 1, "one imported DLL");
    CHECK(ins.has_tls == 0 && ins.has_com == 0, "no tls/com");
    CHECK(cellar_compat_analyze(&img, ins.basename, &a) == CELLAR_OK, "analyze");

    snprintf(dbpath, sizeof dbpath, "%s/compat.db", tmpdir);
    cellar_mkdir_p(tmpdir);
    CHECK(cellar_db_open(&db, dbpath) == CELLAR_OK, "db open empty");
    cellar_db_from_analysis(&e, &ins, &a);
    CHECK(strcmp(e.application, "ExampleGame.exe") == 0, "db app name");
    CHECK(e.rating == CELLAR_RATING_HIGH, "kernel32-only is HIGH");
    CHECK(cellar_db_put(&db, &e) == CELLAR_OK, "db put");
    CHECK(cellar_db_save(&db) == CELLAR_OK, "db save");
    cellar_db_close(&db);

    CHECK(cellar_db_open(&db, dbpath) == CELLAR_OK, "db reopen");
    CHECK(cellar_db_count(&db) == 1, "one entry");
    CHECK(cellar_db_find(&db, "ExampleGame.exe") != NULL, "find by name");
    loaded = *cellar_db_find(&db, "ExampleGame.exe");
    CHECK(loaded.rating == CELLAR_RATING_HIGH, "rating persisted");
    cellar_db_close(&db);

    cellar_image_unload(&img);
    free(pe);
}

static void test_prefix_shell_runtime(const char *root)
{
    cellar_prefix_info_t info, listed[4];
    char bak[512], expanded[512];
    cellar_runtime_t rts[8];
    cellar_package_t pkg, back;

    CHECK(cellar_prefix_create(root, "Game") == CELLAR_OK, "prefix create");
    CHECK(cellar_prefix_info(root, "Game", &info) == CELLAR_OK && info.exists,
          "prefix exists");
    CHECK(cellar_prefix_list(root, listed, 4) >= 1, "prefix list");

    CHECK(cellar_shell_init(info.path) == CELLAR_OK, "shell init");
    CHECK(cellar_shell_ensure_dirs() == CELLAR_OK, "shell dirs");
    CHECK(strstr(cellar_shell_get(CELLAR_ENV_APPDATA), "AppData/Roaming") != NULL,
          "APPDATA mapping");
    CHECK(cellar_shell_expand("%WINDIR%\\system32", expanded, sizeof expanded) == CELLAR_OK,
          "expand");
    CHECK(strstr(expanded, "windows") != NULL, "WINDIR expanded");

    CHECK(cellar_runtime_init(info.path) == CELLAR_OK, "runtime init");
    CHECK(!cellar_runtime_is_installed(CELLAR_RT_VCRUNTIME), "vc not yet");
    CHECK(cellar_runtime_install(CELLAR_RT_VCRUNTIME) == CELLAR_OK, "install vc");
    CHECK(cellar_runtime_is_installed(CELLAR_RT_VCRUNTIME), "vc installed");
    CHECK(cellar_runtime_list(rts, 8) == CELLAR_RT_COUNT, "five runtimes");
    CHECK(cellar_runtime_uninstall(CELLAR_RT_VCRUNTIME) == CELLAR_OK, "uninstall vc");
    CHECK(!cellar_runtime_is_installed(CELLAR_RT_VCRUNTIME), "vc gone");

    CHECK(cellar_install_begin(&pkg, "FooApp", "1.2", "Acme") == CELLAR_OK, "inst begin");
    CHECK(cellar_install_add(&pkg, CELLAR_INST_FILE, "drive_c/FooApp/readme.txt",
                             "hello") == CELLAR_OK, "inst add file");
    CHECK(cellar_install_add(&pkg, CELLAR_INST_REGISTRY,
                             "HKCU\\Software\\Foo", "1") == CELLAR_OK, "inst add reg");
    CHECK(cellar_install_commit(info.path, &pkg) == CELLAR_OK, "inst commit");
    CHECK(cellar_install_load(info.path, "FooApp", &back) == CELLAR_OK, "inst load");
    CHECK(back.action_count == 2, "two actions");
    CHECK(cellar_install_uninstall(info.path, "FooApp") == CELLAR_OK, "uninstall");

    snprintf(bak, sizeof bak, "%s/Game.bak", root);
    CHECK(cellar_prefix_backup(root, "Game", bak) == CELLAR_OK, "backup");
    CHECK(cellar_prefix_delete(root, "Game") == CELLAR_OK, "delete");
    CHECK(cellar_prefix_info(root, "Game", &info) == CELLAR_OK && !info.exists,
          "gone after delete");
    CHECK(cellar_prefix_restore(root, "Game", bak) == CELLAR_OK, "restore");
    CHECK(cellar_prefix_info(root, "Game", &info) == CELLAR_OK && info.exists,
          "restored");
    cellar_prefix_delete(root, "Game");
}

static void test_desktop_debug(const char *dir)
{
    cellar_desktop_entry_t de;
    cellar_debug_snapshot_t snap;
    char clip[32];

    memset(&de, 0, sizeof de);
    snprintf(de.name, sizeof de.name, "Example Game");
    snprintf(de.exec, sizeof de.exec, "cellar prefix launch Game game.exe");
    snprintf(de.categories, sizeof de.categories, "Game;");
    CHECK(cellar_desktop_write_shortcut(dir, &de) == CELLAR_OK, "shortcut");
    CHECK(cellar_desktop_write_mime(dir, "application/x-ms-dos-executable",
                                    "Example-Game.desktop") == CELLAR_OK, "mime");
    cellar_clipboard_set("hello");
    cellar_clipboard_get(clip, sizeof clip);
    CHECK(strcmp(clip, "hello") == 0, "clipboard");
    cellar_desktop_open_url("https://example.test/");
    CHECK(strstr(cellar_desktop_last_url(), "example.test") != NULL, "url");

    cellar_debug_begin("game.exe");
    cellar_debug_note_module();
    cellar_debug_note_module();
    cellar_debug_note_handle();
    cellar_debug_note_api("VirtualProtect()", "Memory Manager");
    cellar_debug_exception("STATUS_ACCESS_VIOLATION", "game.exe", 0x38192A);
    cellar_debug_snapshot(&snap);
    CHECK(snap.module_count == 2, "debug modules");
    CHECK(strstr(snap.last_api, "VirtualProtect") != NULL, "debug api");
    CHECK(snap.pid != 0, "debug pid");
}

static void test_lab(void)
{
    cellar_lab_report_t r;
    CHECK(cellar_lab_run(&r) == CELLAR_OK, "lab run");
    CHECK(r.total > 20, "lab has a meaningful number of tests");
    CHECK(r.failed == 0, "lab all passing");
    CHECK(r.percent > 99.0, "lab ~100%");
}

int main(void)
{
    char tmp[256];
    snprintf(tmp, sizeof tmp, "/tmp/cellar-eco-%u", (unsigned)getpid());
    cellar_mkdir_p(tmp);
    cellar_win32_init();

    test_inspect_and_db(tmp);
    test_prefix_shell_runtime(tmp);
    test_desktop_debug(tmp);
    test_lab();

    if (g_failures == 0) {
        printf("test_ecosystem: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_ecosystem: %d test(s) failed\n", g_failures);
    return 1;
}
