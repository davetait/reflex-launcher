#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <ini.h>
#include "test.h"
#include "ini_doc.h"

static void check_round_trip(const char *text)
{
    IniDoc *doc = ini_doc_parse(text);
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, text);
    free(out);
    ini_doc_free(doc);
}

static void test_round_trip(void)
{
    check_round_trip("");
    check_round_trip("[General]\nDefaultMenu=Main\nVSync=true\n");
    check_round_trip("[General]\r\nDefaultMenu=Main\r\n\r\n# comment\r\n");
    check_round_trip("\xEF\xBB\xBF[General]\nDefaultMenu=Main\n");
    check_round_trip("[General]\nDefaultMenu=Main");                 // no trailing newline
    check_round_trip("# lead\n\n[A]\n  key = value ; note\n#Off=1\n;semi\n\tcontinued\nnot a key line\n[B]\n");
}

typedef struct {
    const char *section;
    const char *key;
    char value[256];
    int found;
} LastValue;

// inih handler that remembers the last value reported for one section/key
static int last_value_handler(void *user, const char *section, const char *name, const char *value)
{
    LastValue *lv = user;
    if (strcmp(section, lv->section) == 0 && strcmp(name, lv->key) == 0) {
        snprintf(lv->value, sizeof(lv->value), "%s", value);
        lv->found = 1;
    }
    return 1;
}

static void check_parity(const char *text, const char *section, const char *key)
{
    LastValue lv = { section, key, "", 0 };
    ini_parse_string(text, last_value_handler, &lv);
    IniDoc *doc = ini_doc_parse(text);
    CHECK_STR(ini_doc_get(doc, section, key), lv.found ? lv.value : NULL);
    ini_doc_free(doc);
}

static void test_get_parity_with_inih(void)
{
    const char *corpus =
        "[A]\n"
        "plain=value\n"
        "spaced  =  padded value  \n"
        "inline=kept ; dropped\n"
        "nospace=a;b;c\n"
        "colon: yes\n"
        "#commented=ignored\n"
        "  ;indented comment\n"
        "multi=first\n"
        "  second\n"
        "dup=one\n"
        "dup=two\n"
        "[B]\n"
        "plain=b-value\n"
        "[A]\n"
        "late=merged\n";
    const char *keys[] = {"plain", "spaced", "inline", "nospace", "colon",
                          "commented", "multi", "dup", "late", "missing"};
    for (size_t i = 0; i < sizeof(keys) / sizeof(keys[0]); i++)
        check_parity(corpus, "A", keys[i]);
    check_parity(corpus, "B", "plain");
}

static void test_get_basics(void)
{
    IniDoc *doc = ini_doc_parse("[General]\nVSync=false\n#FPSLimit=60\n");
    CHECK_STR(ini_doc_get(doc, "General", "VSync"), "false");
    CHECK_STR(ini_doc_get(doc, "General", "FPSLimit"), NULL);     // commented out
    CHECK_STR(ini_doc_get(doc, "general", "VSync"), NULL);        // case-sensitive
    CHECK_STR(ini_doc_get(doc, "Clock", "VSync"), NULL);
    ini_doc_free(doc);
}

static void check_set(const char *before, const char *section, const char *key,
                      const char *value, const char *after)
{
    IniDoc *doc = ini_doc_parse(before);
    CHECK(ini_doc_set(doc, section, key, value));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, after);
    CHECK_STR(ini_doc_get(doc, section, key), value);
    free(out);
    ini_doc_free(doc);
}

static void test_set(void)
{
    // 1. replace in place, keeping neighbours and comments
    check_set("[General]\n# c\nVSync=true\nX=1\n", "General", "VSync", "false",
              "[General]\n# c\nVSync=false\nX=1\n");
    // replacing a multi-line value drops its continuation lines
    check_set("[A]\nk=one\n  two\nz=1\n", "A", "k", "new", "[A]\nk=new\nz=1\n");
    // 2. un-comment a commented-out key
    check_set("[General]\n#FPSLimit=\nVSync=true\n", "General", "FPSLimit", "60",
              "[General]\nFPSLimit=60\nVSync=true\n");
    // 3. append after the section's last setting, not after trailing comments
    check_set("[Clock]\nEnabled=true\n\n# Menus\n[Main]\nEntry1=a;b;c\n", "Clock", "ShowDate", "true",
              "[Clock]\nEnabled=true\nShowDate=true\n\n# Menus\n[Main]\nEntry1=a;b;c\n");
    // 3b. create a missing section at the end
    check_set("[General]\nVSync=true\n", "Clock", "Enabled", "true",
              "[General]\nVSync=true\n\n[Clock]\nEnabled=true\n");
    // CRLF files stay CRLF
    check_set("[General]\r\nVSync=true\r\n", "General", "VSync", "false", "[General]\r\nVSync=false\r\n");
}

static void test_set_rejects_lossy_values(void)
{
    IniDoc *doc = ini_doc_parse("[General]\nStartupCmd=old\n");
    char *before = ini_doc_serialize(doc);
    size_t dirty_before = ini_doc_dirty_count(doc);

    CHECK(!ini_doc_set(doc, "General", "StartupCmd", "sh -c \"a ; b\""));  // inline comment
    char *after_reject_1 = ini_doc_serialize(doc);
    CHECK_STR(before, after_reject_1);
    CHECK(ini_doc_dirty_count(doc) == dirty_before);
    free(after_reject_1);

    CHECK(!ini_doc_set(doc, "General", "StartupCmd", "line1\nline2"));
    char *after_reject_2 = ini_doc_serialize(doc);
    CHECK_STR(before, after_reject_2);
    CHECK(ini_doc_dirty_count(doc) == dirty_before);
    free(after_reject_2);

    CHECK(!ini_doc_set(doc, "General", "StartupCmd", " padded"));
    char *after_reject_3 = ini_doc_serialize(doc);
    CHECK_STR(before, after_reject_3);
    CHECK(ini_doc_dirty_count(doc) == dirty_before);
    free(after_reject_3);

    CHECK(ini_doc_set(doc, "General", "StartupCmd", "a;b"));              // ';' without a space is fine
    CHECK(ini_doc_value_is_representable(""));
    CHECK_STR(ini_doc_get(doc, "General", "StartupCmd"), "a;b");
    free(before);
    ini_doc_free(doc);
}

static void test_unset_and_dirty(void)
{
    IniDoc *doc = ini_doc_parse("[General]\nVSync=false\n[Clock]\nEnabled=true\n");
    CHECK(ini_doc_dirty_count(doc) == 0);
    ini_doc_unset(doc, "General", "VSync");
    CHECK_STR(ini_doc_get(doc, "General", "VSync"), NULL);
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "[General]\n#VSync=false\n[Clock]\nEnabled=true\n");
    free(out);
    ini_doc_set(doc, "Clock", "Enabled", "false");
    ini_doc_set(doc, "Clock", "Enabled", "true");   // same key twice -> one dirty entry
    CHECK(ini_doc_dirty_count(doc) == 2);
    const char *section, *key;
    ini_doc_dirty_get(doc, 1, &section, &key);
    CHECK_STR(section, "Clock");
    CHECK_STR(key, "Enabled");
    ini_doc_free(doc);
}

static void test_set_with_aliasing(void)
{
    // Test that setting a key to its own ini_doc_get() value doesn't use-after-free
    IniDoc *doc = ini_doc_parse("[General]\nStartupCmd=old_value\n");
    const char *current_value = ini_doc_get(doc, "General", "StartupCmd");
    CHECK_STR(current_value, "old_value");
    // This should not crash or read freed memory
    CHECK(ini_doc_set(doc, "General", "StartupCmd", current_value));
    CHECK_STR(ini_doc_get(doc, "General", "StartupCmd"), "old_value");
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "[General]\nStartupCmd=old_value\n");
    free(out);
    ini_doc_free(doc);
}

static void test_unset_multiline(void)
{
    // Test that unsetting a key with continuation lines comments them all out
    IniDoc *doc = ini_doc_parse("[A]\nk=one\n  two\nz=1\n");
    ini_doc_unset(doc, "A", "k");
    // After unset, get should return NULL (continuation lines are now comments)
    CHECK_STR(ini_doc_get(doc, "A", "k"), NULL);
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "[A]\n#k=one\n#  two\nz=1\n");
    free(out);
    ini_doc_free(doc);
}

static void check_list(IniDoc *doc, const char *section, const char *const *expected, size_t n)
{
    const char **values = NULL;
    size_t count = ini_doc_get_list(doc, section, &values);
    CHECK(count == n);
    for (size_t i = 0; i < n && i < count; i++)
        CHECK_STR(values[i], expected[i]);
    free(values);
}

static void test_get_list_mirrors_config_handler(void)
{
    // any key name counts, duplicate sections merge, continuation lines are extra items
    IniDoc *doc = ini_doc_parse(
        "[Main]\nEntry1=A;a.png;cmd-a\nKodi=B;b.png;cmd-b\n#Entry9=off;x;y\n"
        "[Other]\nEntry1=X;x;x\n"
        "[Main]\nEntry3=C;c.png;cmd-c\n  D;d.png;cmd-d\n");
    const char *expected[] = {"A;a.png;cmd-a", "B;b.png;cmd-b", "C;c.png;cmd-c", "D;d.png;cmd-d"};
    check_list(doc, "Main", expected, 4);
    ini_doc_free(doc);
}

static void check_set_list(const char *before, const char *const *values, size_t n, const char *after)
{
    IniDoc *doc = ini_doc_parse(before);
    CHECK(ini_doc_set_list(doc, "Main", "Entry", values, n));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, after);
    free(out);
    check_list(doc, "Main", values, n);
    ini_doc_free(doc);
}

static void test_set_list(void)
{
    const char *base = "[Main]\nEntry1=A;a;a\n# keep me\nEntry2=B;b;b\n\n[System]\nEntry1=S;s;s\n";
    const char *reordered[] = {"B;b;b", "A;a;a"};
    check_set_list(base, reordered, 2,
        "[Main]\nEntry1=B;b;b\n# keep me\nEntry2=A;a;a\n\n[System]\nEntry1=S;s;s\n");
    const char *grown[] = {"A;a;a", "B;b;b", "C;c;c"};
    check_set_list(base, grown, 3,
        "[Main]\nEntry1=A;a;a\n# keep me\nEntry2=B;b;b\nEntry3=C;c;c\n\n[System]\nEntry1=S;s;s\n");
    const char *shrunk[] = {"A;a;a"};
    check_set_list(base, shrunk, 1,
        "[Main]\nEntry1=A;a;a\n# keep me\n\n[System]\nEntry1=S;s;s\n");
    const char *fresh[] = {"N;n;n"};
    check_set_list("[General]\nDefaultMenu=Main\n", fresh, 1,
        "[General]\nDefaultMenu=Main\n\n[Main]\nEntry1=N;n;n\n");

    IniDoc *doc = ini_doc_parse(base);
    const char *bad[] = {"A;a;a", "B;b;cmd ; oops"};
    CHECK(!ini_doc_set_list(doc, "Main", "Entry", bad, 2));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, base);   // unchanged on failure
    free(out);
    ini_doc_free(doc);
}

static void test_sections(void)
{
    IniDoc *doc = ini_doc_parse("top=1\n[General]\nX=1\n[Main]\nEntry1=a;b;c\n[System]\nEntry1=s;s;s\n[Main]\nEntry2=d;e;f\n");
    const char **names = NULL;
    size_t n = ini_doc_sections(doc, &names);
    CHECK(n == 3);
    if (n == 3) {
        CHECK_STR(names[0], "General");
        CHECK_STR(names[1], "Main");
        CHECK_STR(names[2], "System");
    }
    free(names);

    ini_doc_rename_section(doc, "Main", "Home");
    CHECK(!ini_doc_has_section(doc, "Main"));
    const char *home[] = {"a;b;c", "d;e;f"};
    check_list(doc, "Home", home, 2);

    ini_doc_remove_section(doc, "System");
    CHECK(!ini_doc_has_section(doc, "System"));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "top=1\n[General]\nX=1\n[Home]\nEntry1=a;b;c\n[Home]\nEntry2=d;e;f\n");
    free(out);
    ini_doc_free(doc);
}

static void test_set_list_reorder_aliasing(void)
{
    // Reproduces use-after-free when set_list is called with get_list output
    IniDoc *doc = ini_doc_parse("[Main]\nEntry1=A;a;a\nEntry2=B;b;b\n");
    const char **values = NULL;
    size_t count = ini_doc_get_list(doc, "Main", &values);
    CHECK(count == 2);

    // Reorder the values by swapping them
    const char *reordered[] = {values[1], values[0]};
    CHECK(ini_doc_set_list(doc, "Main", "Entry", reordered, 2));

    // Verify the list is correctly reordered
    const char *expected[] = {"B;b;b", "A;a;a"};
    check_list(doc, "Main", expected, 2);

    // Verify serialization
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "[Main]\nEntry1=B;b;b\nEntry2=A;a;a\n");
    free(out);
    free(values);
    ini_doc_free(doc);
}

static void test_set_list_spread_and_zero(void)
{
    // List spread over duplicate [Main] block with continuation line and non-EntryN key
    IniDoc *doc = ini_doc_parse(
        "[Main]\nEntry1=A;a;a\nKodi=B;b;b\n"
        "[Other]\nEntry1=X;x;x\n"
        "[Main]\nEntry3=C;c;c\n  D;d;d\n");

    // Set to 2 values
    const char *new_values[] = {"P;p;p", "Q;q;q"};
    CHECK(ini_doc_set_list(doc, "Main", "Entry", new_values, 2));

    // Verify list contains exactly 2 values
    const char **gotten = NULL;
    size_t gotten_count = ini_doc_get_list(doc, "Main", &gotten);
    CHECK(gotten_count == 2);
    CHECK_STR(gotten[0], "P;p;p");
    CHECK_STR(gotten[1], "Q;q;q");
    free(gotten);

    // Verify serialization: Entry1 and Entry2 reuse/replace the 4 existing list lines,
    // Kodi becomes Entry2
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "[Main]\nEntry1=P;p;p\nEntry2=Q;q;q\n[Other]\nEntry1=X;x;x\n[Main]\n");
    free(out);
    ini_doc_free(doc);

    // Test count == 0: remove all list items, keep header
    doc = ini_doc_parse("[Main]\nEntry1=A;a;a\nEntry2=B;b;b\n# keep\n[Other]\nEntry1=X;x;x\n");
    CHECK(ini_doc_set_list(doc, "Main", "Entry", NULL, 0));
    out = ini_doc_serialize(doc);
    CHECK_STR(out, "[Main]\n# keep\n[Other]\nEntry1=X;x;x\n");
    free(out);
    ini_doc_free(doc);
}

void suite_ini_doc(void)
{
    test_round_trip();
    test_get_basics();
    test_get_parity_with_inih();
    test_set();
    test_set_rejects_lossy_values();
    test_unset_and_dirty();
    test_set_with_aliasing();
    test_unset_multiline();
    test_get_list_mirrors_config_handler();
    test_set_list();
    test_sections();
    test_set_list_reorder_aliasing();
    test_set_list_spread_and_zero();
}
