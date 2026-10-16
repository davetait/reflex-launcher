#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <direct.h>
#endif
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

static void test_section_ops_with_doc_pointers(void)
{
    // Names returned by ini_doc_sections point into the doc; removing or renaming
    // with them must not read freed memory
    IniDoc *doc = ini_doc_parse("[General]\nX=1\n[Main]\nEntry1=a;b;c\n[System]\nEntry1=s;s;s\n[Main]\nEntry2=d;e;f\n");
    const char **names = NULL;
    size_t n = ini_doc_sections(doc, &names);
    CHECK(n == 3);
    ini_doc_remove_section(doc, names[1]);   // "Main", owned by its first header
    free(names);
    CHECK(!ini_doc_has_section(doc, "Main"));
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, "[General]\nX=1\n[System]\nEntry1=s;s;s\n");
    free(out);

    n = ini_doc_sections(doc, &names);
    CHECK(n == 2);
    CHECK(ini_doc_rename_section(doc, names[1], names[1]));   // self-rename is a no-op
    free(names);
    out = ini_doc_serialize(doc);
    CHECK_STR(out, "[General]\nX=1\n[System]\nEntry1=s;s;s\n");
    free(out);

    n = ini_doc_sections(doc, &names);
    CHECK(ini_doc_rename_section(doc, names[1], "Power"));
    free(names);
    out = ini_doc_serialize(doc);
    CHECK_STR(out, "[General]\nX=1\n[Power]\nEntry1=s;s;s\n");
    free(out);
    ini_doc_free(doc);
}

static void check_unchanged(IniDoc *doc, const char *expected)
{
    char *out = ini_doc_serialize(doc);
    CHECK_STR(out, expected);
    free(out);
    CHECK(ini_doc_dirty_count(doc) == 0);
}

static void test_section_names_validated(void)
{
    const char *base = "[General]\nX=1\n[Main]\nEntry1=a;b;c\n";
    const char *bad[] = {"a]b", "a\nb", "a\rb", "a ;b", " a", "a ", "", "a\n[General]"};
    const char *list[] = {"T;i;c"};
    IniDoc *doc = ini_doc_parse(base);
    for (size_t i = 0; i < sizeof(bad) / sizeof(bad[0]); i++) {
        CHECK(!ini_doc_section_is_representable(bad[i]));
        CHECK(!ini_doc_add_section(doc, bad[i]));
        CHECK(!ini_doc_rename_section(doc, "Main", bad[i]));
        CHECK(!ini_doc_set(doc, bad[i], "k", "v"));
        CHECK(!ini_doc_set_list(doc, bad[i], "Entry", list, 1));
        check_unchanged(doc, base);
    }
    CHECK(ini_doc_section_is_representable("Main Menu"));
    CHECK(ini_doc_section_is_representable("a;b"));   // ';' without a space before it is kept
    CHECK(!ini_doc_rename_section(doc, "Main", "General"));   // would merge into another section
    check_unchanged(doc, base);
    CHECK(ini_doc_add_section(doc, "Main"));                  // already there: nothing to do
    check_unchanged(doc, base);
    CHECK(ini_doc_add_section(doc, "Games"));
    CHECK(ini_doc_has_section(doc, "Games"));
    ini_doc_free(doc);
}

typedef struct {
    const char *section;
    char values[8][256];
    size_t count;
} SectionValues;

// inih handler that collects every value reported in one section, in order
static int section_values_handler(void *user, const char *section, const char *name, const char *value)
{
    SectionValues *sv = user;
    (void) name;
    if (strcmp(section, sv->section) == 0 && sv->count < 8)
        snprintf(sv->values[sv->count++], sizeof(sv->values[0]), "%s", value);
    return 1;
}

// What inih reads for section/key from the doc as it would be saved
static void check_inih_reads(const IniDoc *doc, const char *section, const char *key, const char *expected)
{
    char *text = ini_doc_serialize(doc);
    LastValue lv = { section, key, "", 0 };
    ini_parse_string(text, last_value_handler, &lv);
    CHECK_STR(lv.found ? lv.value : NULL, expected);
    CHECK_STR(ini_doc_get(doc, section, key), expected);
    free(text);
}

static void check_inih_list(const IniDoc *doc, const char *section, const char *const *expected, size_t n)
{
    char *text = ini_doc_serialize(doc);
    SectionValues sv = { section, {{0}}, 0 };
    ini_parse_string(text, section_values_handler, &sv);
    CHECK(sv.count == n);
    for (size_t i = 0; i < n && i < sv.count; i++)
        CHECK_STR(sv.values[i], expected[i]);
    free(text);
    check_list((IniDoc *) doc, section, expected, n);
}

static void test_inih_round_trip_after_edits(void)
{
    // appending a key to a section whose only line is an indented non-key line
    IniDoc *doc = ini_doc_parse("[A]\nk=1\n[B]\n  orphan\n");
    CHECK(ini_doc_set(doc, "B", "x", "v"));
    check_inih_reads(doc, "B", "x", "v");
    check_inih_reads(doc, "A", "k", "1");
    ini_doc_free(doc);

    // un-commenting a key that sits between a key and its continuation line
    const char *between = "[A]\na=1\n#k=old\n  more\n";
    doc = ini_doc_parse(between);
    bool ok = ini_doc_set(doc, "A", "k", "new");
    if (ok) {
        check_inih_reads(doc, "A", "k", "new");
        check_inih_reads(doc, "A", "a", "more");
    }
    else {
        check_unchanged(doc, between);
    }
    ini_doc_free(doc);

    // replacing a key whose continuation is separated by a blank line
    doc = ini_doc_parse("[A]\nk=one\n\n  two\nz=1\n");
    CHECK(ini_doc_set(doc, "A", "k", "new"));
    check_inih_reads(doc, "A", "k", "new");
    check_inih_reads(doc, "A", "z", "1");
    ini_doc_free(doc);

    // unsetting it must not leave the continuation behind for inih or for another key
    doc = ini_doc_parse("[A]\na=1\nk=one\n\n  two\n");
    ini_doc_unset(doc, "A", "k");
    check_inih_reads(doc, "A", "k", NULL);
    check_inih_reads(doc, "A", "a", "1");
    ini_doc_free(doc);

    // writing a list into a section with an indented non-key line
    doc = ini_doc_parse("[Main]\n  orphan\n");
    const char *list[] = {"X;x;x", "Y;y;y"};
    CHECK(ini_doc_set_list(doc, "Main", "Entry", list, 2));
    check_inih_list(doc, "Main", list, 2);
    ini_doc_free(doc);

    // a list whose continuation is separated by a blank line
    doc = ini_doc_parse("[Main]\nEntry1=A;a;a\n\n  B;b;b\n[Other]\nEntry1=O;o;o\n");
    const char *one[] = {"C;c;c"};
    CHECK(ini_doc_set_list(doc, "Main", "Entry", one, 1));
    check_inih_list(doc, "Main", one, 1);
    ini_doc_free(doc);

    // keys that inih would read differently are refused and leave the doc unchanged
    doc = ini_doc_parse("[A]\nk=1\n");
    CHECK(!ini_doc_set(doc, "A", "x\ny", "v"));
    CHECK(!ini_doc_set(doc, "A", "a=b", "v"));
    CHECK(!ini_doc_set(doc, "A", "#x", "v"));
    check_unchanged(doc, "[A]\nk=1\n");
    ini_doc_free(doc);
}

static void test_empty_key_and_bom_parity(void)
{
    // inih never starts a continuation after an empty key ("=v")
    const char *empty_key = "[A]\n=v\n  x=1\n";
    check_parity(empty_key, "A", "");
    check_parity(empty_key, "A", "x");
    const char *bom = "\xEF\xBB\xBF[A]\nk=v\n  w\n";
    check_parity(bom, "A", "k");
}


static void test_load_save(void)
{
    char dir[512], path[512], bak[512], tmp[512];
    test_tmp_path(dir, sizeof(dir), "dir with space");
#ifdef _WIN32
    _mkdir(dir);
#else
    mkdir(dir, 0700);
#endif
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    snprintf(bak, sizeof(bak), "%s.bak", path);
    snprintf(tmp, sizeof(tmp), "%s.tmp", path);
    remove(bak);
    test_write_file(path, "[General]\r\nVSync=true\r\n");

    IniDoc *doc = ini_doc_load(path);
    CHECK(doc != NULL);
    CHECK(!ini_doc_changed_on_disk(doc, path));
    ini_doc_set(doc, "General", "VSync", "false");
    CHECK(ini_doc_save(doc, path) == 0);
    ini_doc_mark_saved(doc, path);
    CHECK(ini_doc_dirty_count(doc) == 0);

    char *saved = test_read_file(path);
    char *backup = test_read_file(bak);
    CHECK_STR(saved, "[General]\r\nVSync=false\r\n");
    CHECK_STR(backup, "[General]\r\nVSync=true\r\n");
    CHECK(test_read_file(tmp) == NULL);
    free(saved);
    free(backup);

    // another program edits the file -> detected (size changes)
    test_write_file(path, "[General]\nVSync=true\nWrapEntries=true\n");
    CHECK(ini_doc_changed_on_disk(doc, path));

    // a doc parsed from memory never reports a disk change
    IniDoc *memory = ini_doc_parse("[A]\n");
    CHECK(!ini_doc_changed_on_disk(memory, path));
    ini_doc_free(memory);

    // saving into a missing directory fails cleanly and leaves no .tmp
    char missing[512];
    test_tmp_path(missing, sizeof(missing), "no/such/dir/config.ini");
    CHECK(ini_doc_save(doc, missing) != 0);
    ini_doc_free(doc);

    CHECK(ini_doc_load(missing) == NULL);
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
    test_load_save();
    test_set_list_reorder_aliasing();
    test_set_list_spread_and_zero();
    test_section_ops_with_doc_pointers();
    test_section_names_validated();
    test_inih_round_trip_after_edits();
    test_empty_key_and_bom_parity();
}
