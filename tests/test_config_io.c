#include <stdlib.h>
#include "test.h"
#include "ini_doc.h"
#include "config_io.h"

static const char *good =
    "[General]\nDefaultMenu=Main\n"
    "[Hotkeys]\nHotkey1=#1B;:quit\n"
    "[Main]\nEntry1=Kodi;kodi.png;open -a Kodi\nEntry2=System;system.png;:submenu System\n"
    "[System]\nEntry1=Sleep;sleep.png;:sleep\n";

static void check_config(const char *text, bool expected, const char *message_part)
{
    char err[512] = "";
    IniDoc *doc = ini_doc_parse(text);
    bool ok = config_check(doc, err, sizeof(err));
    if (ok != expected || (!ok && strstr(err, message_part) == NULL))
        fprintf(stderr, "  config_check -> %d \"%s\" (expected %d, \"%s\")\n", ok, err, expected, message_part);
    CHECK(ok == expected);
    if (!expected)
        CHECK(strstr(err, message_part) != NULL);
    ini_doc_free(doc);
}

static void test_config_check(void)
{
    check_config(good, true, "");
    check_config("[Main]\nEntry1=A;a;a\n", false, "default menu");
    check_config("[General]\nDefaultMenu=Home\n[Main]\nEntry1=A;a;a\n", false, "\"Home\" does not exist");
    check_config("[General]\nDefaultMenu=Main\n[Main]\n", false, "has no entries");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=OnlyTitle\n", false, "Entry 1 in [Main]");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=T;;cmd\n", false, "Entry 1 in [Main]");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=T;i;:submenu Nope\n", false, "\"Nope\"");
    check_config("[General]\nDefaultMenu=Main\n[Main]\nEntry1=T;i;c\n[Hotkeys]\nHotkey1=1B;:quit\n", false, "Hotkey 1");
}

static void test_entry_validity_mirrors_parser(void)
{
    CHECK(config_entry_is_valid("Title;icon.png;cmd"));
    CHECK(config_entry_is_valid("Title;icon.png;cmd;with;semicolons"));
    CHECK(!config_entry_is_valid("Title;icon.png"));
    CHECK(!config_entry_is_valid("Title;icon.png;:select"));   // parser drops these
    CHECK(!config_entry_is_valid(";;"));
}

static void test_config_save(void)
{
    char path[512], err[512];
    test_tmp_path(path, sizeof(path), "save-test.ini");
    test_write_file(path, good);

    // invalid edited value -> nothing written
    IniDoc *doc = ini_doc_load(path);
    ini_doc_set(doc, "Layout", "IconSize", "5000");
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_INVALID);
    CHECK(strstr(err, "Icon size") != NULL);
    char *unchanged = test_read_file(path);
    CHECK_STR(unchanged, good);
    free(unchanged);

    // a pre-existing bad value that wasn't edited never blocks saving
    ini_doc_free(doc);
    char with_bad[1024];
    snprintf(with_bad, sizeof(with_bad), "%s[Clock]\nOpacity=lots\n", good);
    test_write_file(path, with_bad);
    doc = ini_doc_load(path);
    ini_doc_set(doc, "General", "VSync", "false");
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_OK);
    CHECK(ini_doc_dirty_count(doc) == 0);

    // too-long line (inih reads at most 199 bytes per line)
    char long_cmd[300];
    memset(long_cmd, 'x', sizeof(long_cmd) - 1);
    long_cmd[sizeof(long_cmd) - 1] = '\0';
    ini_doc_set(doc, "General", "StartupCmd", long_cmd);
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_INVALID);
    CHECK(strstr(err, "too long") != NULL);
    ini_doc_unset(doc, "General", "StartupCmd");

    // file changed by someone else -> refuse unless forced
    test_write_file(path, "[General]\nDefaultMenu=Main\n[Main]\nEntry1=A;a;a\n# edited elsewhere\n");
    ini_doc_set(doc, "General", "VSync", "true");
    CHECK(config_save(doc, path, false, err, sizeof(err)) == CONFIG_SAVE_CHANGED_ON_DISK);
    CHECK(config_save(doc, path, true, err, sizeof(err)) == CONFIG_SAVE_OK);
    ini_doc_free(doc);

    // unwritable target
    char missing[512];
    test_tmp_path(missing, sizeof(missing), "no/such/dir/config.ini");
    doc = ini_doc_parse(good);
    CHECK(config_save(doc, missing, false, err, sizeof(err)) == CONFIG_SAVE_WRITE_FAILED);
    CHECK(strstr(err, "Could not write") != NULL);
    ini_doc_free(doc);
}

void suite_config_io(void)
{
    test_config_check();
    test_entry_validity_mirrors_parser();
    test_config_save();
}
