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

#include "airlock/airlock.h"
#include "airlock/com.h"
#include "airlock/compat.h"
#include "airlock/db.h"
#include "airlock/debug.h"
#include "airlock/desktop.h"
#include "airlock/inspect.h"
#include "airlock/installer.h"
#include "airlock/loader.h"
#include "airlock/locale.h"
#include "airlock/prefix.h"
#include "airlock/runtime.h"
#include "airlock/security.h"
#include "airlock/shell.h"
#include "airlock/testlab.h"
#include "airlock/win32.h"

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
    airlock_image_t img;
    airlock_inspect_t ins;
    airlock_analysis_t a;
    airlock_db_t db;
    airlock_db_entry_t e, loaded;
    char dbpath[512];

    CHECK(pe != NULL, "build pe");
    CHECK(airlock_image_load_buffer(pe, n, AIRLOCK_LOAD_DEFAULT, &img) == AIRLOCK_OK,
          "load pe");
    CHECK(airlock_inspect_image(&img, "/games/ExampleGame.exe", &ins) == AIRLOCK_OK,
          "inspect");
    CHECK(strcmp(ins.basename, "ExampleGame.exe") == 0, "basename");
    CHECK(ins.is_64bit == 0, "x86");
    CHECK(ins.unique_dll_count == 1, "one imported DLL");
    CHECK(ins.has_tls == 0 && ins.has_com == 0, "no tls/com");
    CHECK(airlock_compat_analyze(&img, ins.basename, &a) == AIRLOCK_OK, "analyze");

    snprintf(dbpath, sizeof dbpath, "%s/compat.db", tmpdir);
    airlock_mkdir_p(tmpdir);
    CHECK(airlock_db_open(&db, dbpath) == AIRLOCK_OK, "db open empty");
    airlock_db_from_analysis(&e, &ins, &a);
    CHECK(strcmp(e.application, "ExampleGame.exe") == 0, "db app name");
    CHECK(e.rating == AIRLOCK_RATING_HIGH, "kernel32-only is HIGH");
    CHECK(airlock_db_put(&db, &e) == AIRLOCK_OK, "db put");
    CHECK(airlock_db_save(&db) == AIRLOCK_OK, "db save");
    airlock_db_close(&db);

    CHECK(airlock_db_open(&db, dbpath) == AIRLOCK_OK, "db reopen");
    CHECK(airlock_db_count(&db) == 1, "one entry");
    CHECK(airlock_db_find(&db, "ExampleGame.exe") != NULL, "find by name");
    loaded = *airlock_db_find(&db, "ExampleGame.exe");
    CHECK(loaded.rating == AIRLOCK_RATING_HIGH, "rating persisted");
    airlock_db_close(&db);

    airlock_image_unload(&img);
    free(pe);
}

static void test_prefix_shell_runtime(const char *root)
{
    airlock_prefix_info_t info, listed[4];
    char bak[512], expanded[512];
    airlock_runtime_t rts[8];
    airlock_package_t pkg, back;

    CHECK(airlock_prefix_create(root, "Game") == AIRLOCK_OK, "prefix create");
    CHECK(airlock_prefix_info(root, "Game", &info) == AIRLOCK_OK && info.exists,
          "prefix exists");
    CHECK(airlock_prefix_list(root, listed, 4) >= 1, "prefix list");

    CHECK(airlock_shell_init(info.path) == AIRLOCK_OK, "shell init");
    CHECK(airlock_shell_ensure_dirs() == AIRLOCK_OK, "shell dirs");
    CHECK(strstr(airlock_shell_get(AIRLOCK_ENV_APPDATA), "AppData/Roaming") != NULL,
          "APPDATA mapping");
    CHECK(airlock_shell_expand("%WINDIR%\\system32", expanded, sizeof expanded) == AIRLOCK_OK,
          "expand");
    CHECK(strstr(expanded, "windows") != NULL, "WINDIR expanded");

    CHECK(airlock_runtime_init(info.path) == AIRLOCK_OK, "runtime init");
    CHECK(!airlock_runtime_is_installed(AIRLOCK_RT_VCRUNTIME), "vc not yet");
    CHECK(airlock_runtime_install(AIRLOCK_RT_VCRUNTIME) == AIRLOCK_OK, "install vc");
    CHECK(airlock_runtime_is_installed(AIRLOCK_RT_VCRUNTIME), "vc installed");
    CHECK(airlock_runtime_list(rts, 8) == AIRLOCK_RT_COUNT, "five runtimes");
    CHECK(airlock_runtime_uninstall(AIRLOCK_RT_VCRUNTIME) == AIRLOCK_OK, "uninstall vc");
    CHECK(!airlock_runtime_is_installed(AIRLOCK_RT_VCRUNTIME), "vc gone");

    CHECK(airlock_install_begin(&pkg, "FooApp", "1.2", "Acme") == AIRLOCK_OK, "inst begin");
    CHECK(airlock_install_add(&pkg, AIRLOCK_INST_FILE, "drive_c/FooApp/readme.txt",
                             "hello") == AIRLOCK_OK, "inst add file");
    CHECK(airlock_install_add(&pkg, AIRLOCK_INST_REGISTRY,
                             "HKCU\\Software\\Foo", "1") == AIRLOCK_OK, "inst add reg");
    CHECK(airlock_install_commit(info.path, &pkg) == AIRLOCK_OK, "inst commit");
    CHECK(airlock_install_load(info.path, "FooApp", &back) == AIRLOCK_OK, "inst load");
    CHECK(back.action_count == 2, "two actions");
    CHECK(airlock_install_uninstall(info.path, "FooApp") == AIRLOCK_OK, "uninstall");

    snprintf(bak, sizeof bak, "%s/Game.bak", root);
    CHECK(airlock_prefix_backup(root, "Game", bak) == AIRLOCK_OK, "backup");
    CHECK(airlock_prefix_delete(root, "Game") == AIRLOCK_OK, "delete");
    CHECK(airlock_prefix_info(root, "Game", &info) == AIRLOCK_OK && !info.exists,
          "gone after delete");
    CHECK(airlock_prefix_restore(root, "Game", bak) == AIRLOCK_OK, "restore");
    CHECK(airlock_prefix_info(root, "Game", &info) == AIRLOCK_OK && info.exists,
          "restored");
    airlock_prefix_delete(root, "Game");
}

static void test_prefix_extended(const char *root)
{
    airlock_prefix_info_t info;
    char bak[512], v[64];

    CHECK(airlock_prefix_create_arch(root, "Old32", "win32") == AIRLOCK_OK,
          "create win32 prefix");
    CHECK(airlock_prefix_info(root, "Old32", &info) == AIRLOCK_OK, "info win32");
    CHECK(strcmp(info.arch, "win32") == 0, "arch is win32");

    /* clone */
    CHECK(airlock_prefix_clone(root, "Old32", "Old32Copy") == AIRLOCK_OK, "clone");
    CHECK(airlock_prefix_info(root, "Old32Copy", &info) == AIRLOCK_OK &&
          info.exists, "clone exists");
    CHECK(strcmp(info.arch, "win32") == 0, "clone arch preserved");

    /* container-level settings */
    CHECK(airlock_prefix_set_setting(root, "Old32", "resolution", "1920x1080")
          == AIRLOCK_OK, "set resolution");
    CHECK(airlock_prefix_get_setting(root, "Old32", "resolution", v, sizeof v)
          == AIRLOCK_OK && strcmp(v, "1920x1080") == 0, "get resolution");
    CHECK(airlock_prefix_set_setting(root, "Old32", "resolution", "1280x720")
          == AIRLOCK_OK, "update resolution");
    CHECK(airlock_prefix_get_setting(root, "Old32", "resolution", v, sizeof v)
          == AIRLOCK_OK && strcmp(v, "1280x720") == 0, "updated resolution");
    CHECK(airlock_prefix_get_setting(root, "Old32", "version_mode", v, sizeof v)
          == AIRLOCK_OK && strcmp(v, "2") == 0, "version_mode intact");

    /* export/import round-trip */
    snprintf(bak, sizeof bak, "%s/old32.cbk", root);
    CHECK(airlock_prefix_export(root, "Old32", bak) == AIRLOCK_OK, "export");
    CHECK(airlock_prefix_delete(root, "Old32") == AIRLOCK_OK, "delete old32");
    CHECK(airlock_prefix_import(root, "Old32", bak) == AIRLOCK_OK, "import");
    CHECK(airlock_prefix_info(root, "Old32", &info) == AIRLOCK_OK && info.exists,
          "imported exists");

    airlock_prefix_delete(root, "Old32Copy");
    airlock_prefix_delete(root, "Old32");
}

static void test_desktop_debug(const char *dir)
{
    airlock_desktop_entry_t de;
    airlock_debug_snapshot_t snap;
    char clip[32];

    memset(&de, 0, sizeof de);
    snprintf(de.name, sizeof de.name, "Example Game");
    snprintf(de.exec, sizeof de.exec, "airlock prefix launch Game game.exe");
    snprintf(de.categories, sizeof de.categories, "Game;");
    CHECK(airlock_desktop_write_shortcut(dir, &de) == AIRLOCK_OK, "shortcut");
    CHECK(airlock_desktop_write_mime(dir, "application/x-ms-dos-executable",
                                    "Example-Game.desktop") == AIRLOCK_OK, "mime");
    airlock_clipboard_set("hello");
    airlock_clipboard_get(clip, sizeof clip);
    CHECK(strcmp(clip, "hello") == 0, "clipboard");
    airlock_desktop_open_url("https://example.test/");
    CHECK(strstr(airlock_desktop_last_url(), "example.test") != NULL, "url");

    airlock_debug_begin("game.exe");
    airlock_debug_note_module();
    airlock_debug_note_module();
    airlock_debug_note_handle();
    airlock_debug_note_api("VirtualProtect()", "Memory Manager");
    airlock_debug_exception("STATUS_ACCESS_VIOLATION", "game.exe", 0x38192A);
    airlock_debug_snapshot(&snap);
    CHECK(snap.module_count == 2, "debug modules");
    CHECK(strstr(snap.last_api, "VirtualProtect") != NULL, "debug api");
    CHECK(snap.pid != 0, "debug pid");
}

/* ---- archive format, backward compatibility, and extraction safety ------ */

static void wr_u32_le(FILE *f, unsigned long v)
{
    unsigned char b[4];
    b[0] = (unsigned char)(v & 0xFF);
    b[1] = (unsigned char)((v >> 8) & 0xFF);
    b[2] = (unsigned char)((v >> 16) & 0xFF);
    b[3] = (unsigned char)((v >> 24) & 0xFF);
    fwrite(b, 1, 4, f);
}

/* Write a one-member archive by hand so tests can inject hostile paths. */
static void write_raw_archive(const char *path, const char *magic,
                              const char *member, const char *payload)
{
    FILE *f = fopen(path, "wb");
    CHECK(f != NULL, "raw archive: open for write");
    if (!f)
        return;
    fwrite(magic, 1, 4, f);
    wr_u32_le(f, (unsigned long)strlen(member));
    fwrite(member, 1, strlen(member), f);
    wr_u32_le(f, (unsigned long)strlen(payload));
    fwrite(payload, 1, strlen(payload), f);
    wr_u32_le(f, 0); /* terminator */
    fclose(f);
}

static void rewrite_magic(const char *path, const char *magic)
{
    FILE *f = fopen(path, "r+b");
    CHECK(f != NULL, "archive: reopen for magic rewrite");
    if (f) {
        fwrite(magic, 1, 4, f);
        fclose(f);
    }
}

static void test_prefix_archive(const char *root)
{
    char bak[300], evil[300], marker[400], outside[400];
    char magic[5] = {0}, buf[64];
    airlock_prefix_info_t info;
    FILE *f;

    snprintf(bak, sizeof bak, "%s/arch.alk", root);
    snprintf(evil, sizeof evil, "%s/evil.alk", root);

    /* 1. A new backup is written in the current ALK1 format. */
    CHECK(airlock_prefix_create(root, "Arch") == AIRLOCK_OK, "archive: create");
    snprintf(marker, sizeof marker, "%s/Arch/drive_c/marker.txt", root);
    f = fopen(marker, "wb");
    CHECK(f != NULL, "archive: write marker");
    if (f) {
        fputs("hello", f);
        fclose(f);
    }
    CHECK(airlock_prefix_backup(root, "Arch", bak) == AIRLOCK_OK,
          "archive: backup");
    f = fopen(bak, "rb");
    CHECK(f != NULL, "archive: reopen backup");
    if (f) {
        CHECK(fread(magic, 1, 4, f) == 4, "archive: read magic");
        fclose(f);
    }
    CHECK(strcmp(magic, "ALK1") == 0, "archive: new backups use ALK1");

    /* 2. Round-trip preserves content. */
    CHECK(airlock_prefix_delete(root, "Arch") == AIRLOCK_OK, "archive: delete");
    CHECK(airlock_prefix_restore(root, "Arch", bak) == AIRLOCK_OK,
          "archive: restore");
    f = fopen(marker, "rb");
    CHECK(f != NULL, "archive: marker came back");
    if (f) {
        size_t n = fread(buf, 1, sizeof buf - 1, f);
        buf[n] = '\0';
        fclose(f);
        CHECK(strcmp(buf, "hello") == 0, "archive: marker content intact");
    }

    /* 3. Pre-rebrand CBK1 archives still restore. */
    rewrite_magic(bak, "CBK1");
    CHECK(airlock_prefix_delete(root, "Arch") == AIRLOCK_OK,
          "archive: delete before legacy restore");
    CHECK(airlock_prefix_restore(root, "Arch", bak) == AIRLOCK_OK,
          "archive: legacy CBK1 still restores");
    airlock_prefix_delete(root, "Arch");

    /* 4. An unknown magic is refused and creates nothing. */
    rewrite_magic(bak, "XXXX");
    CHECK(airlock_prefix_restore(root, "Arch", bak) != AIRLOCK_OK,
          "archive: unknown magic rejected");
    CHECK(airlock_prefix_info(root, "Arch", &info) == AIRLOCK_OK && !info.exists,
          "archive: rejected archive created nothing");

    /* 5. Path traversal is refused and nothing escapes the prefix root. */
    write_raw_archive(evil, "ALK1", "../escape.txt", "pwned");
    snprintf(outside, sizeof outside, "%s/escape.txt", root);
    remove(outside);
    CHECK(airlock_prefix_restore(root, "Evil", evil) != AIRLOCK_OK,
          "archive: traversal member rejected");
    f = fopen(outside, "rb");
    CHECK(f == NULL, "archive: no file written outside the prefix");
    if (f)
        fclose(f);
    airlock_prefix_delete(root, "Evil");

    /* 6. A legitimate filename that merely contains ".." is accepted. */
    write_raw_archive(evil, "ALK1", "save..bak", "ok");
    CHECK(airlock_prefix_restore(root, "Dotted", evil) == AIRLOCK_OK,
          "archive: 'save..bak' accepted");
    snprintf(outside, sizeof outside, "%s/Dotted/save..bak", root);
    f = fopen(outside, "rb");
    CHECK(f != NULL, "archive: 'save..bak' extracted");
    if (f)
        fclose(f);
    airlock_prefix_delete(root, "Dotted");

    remove(bak);
    remove(evil);
}

static void test_lab(void)
{
    airlock_lab_report_t r;
    CHECK(airlock_lab_run(&r) == AIRLOCK_OK, "lab run");
    CHECK(r.total > 20, "lab has a meaningful number of tests");
    CHECK(r.failed == 0, "lab all passing");
    CHECK(r.percent > 99.0, "lab ~100%");
}

int main(void)
{
    char tmp[256];
    snprintf(tmp, sizeof tmp, "/tmp/airlock-eco-%u", (unsigned)getpid());
    airlock_mkdir_p(tmp);
    airlock_win32_init();

    test_inspect_and_db(tmp);
    test_prefix_shell_runtime(tmp);
    test_prefix_extended(tmp);
    test_prefix_archive(tmp);
    test_desktop_debug(tmp);
    test_lab();

    if (g_failures == 0) {
        printf("test_ecosystem: all tests passed\n");
        return 0;
    }
    fprintf(stderr, "test_ecosystem: %d test(s) failed\n", g_failures);
    return 1;
}
