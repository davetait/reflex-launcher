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

void suite_ini_doc(void)
{
    test_round_trip();
    test_get_basics();
    test_get_parity_with_inih();
}
