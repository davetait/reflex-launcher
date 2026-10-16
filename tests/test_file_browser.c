#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <direct.h>
#define MAKE_DIR(p) _mkdir(p)
#else
#include <sys/stat.h>
#define MAKE_DIR(p) mkdir((p), 0755)
#endif
#include <stdbool.h>
#include <SDL.h>
#include "test.h"
#include "config_ui/file_browser.h"
#include "launcher.h"
#include "util.h"

static char root[1001];

static void make_tree(void)
{
    char path[1100];
    test_tmp_path(root, sizeof(root), "fb");
    MAKE_DIR(root);
    snprintf(path, sizeof(path), "%s%sPics", root, PATH_SEPARATOR);
    MAKE_DIR(path);
    snprintf(path, sizeof(path), "%s%sfonts", root, PATH_SEPARATOR);
    MAKE_DIR(path);
    const char *files[] = {"b.png", "A.JPG", "notes.txt", ".hidden.png", "fonts" PATH_SEPARATOR "x.ttf"};
    for (size_t i = 0; i < sizeof(files) / sizeof(files[0]); i++) {
        snprintf(path, sizeof(path), "%s%s%s", root, PATH_SEPARATOR, files[i]);
        test_write_file(path, "x");
    }
}

static size_t find_entry(const FileBrowser *fb, const char *name)
{
    for (size_t i = 0; i < file_browser_count(fb); i++) {
        if (strcmp(file_browser_entry(fb, i).name, name) == 0)
            return i;
    }
    CHECK_STR("entry not found", name);
    return 0;
}

static bool ends_with(const char *s, const char *suffix)
{
    size_t sl = strlen(s), tl = strlen(suffix);
    return sl >= tl && strcmp(s + sl - tl, suffix) == 0;
}

static void test_lists_sorted_filtered(void)
{
    char err[200] = "";
    const char *candidates[] = {root};
    FileBrowser *fb = file_browser_open(candidates, 1, FILTER_IMAGES, err, sizeof(err));
    CHECK(fb != NULL);
    if (fb == NULL)
        return;
    CHECK(file_browser_count(fb) == 5);
    CHECK_STR(file_browser_entry(fb, 0).name, "..");
    CHECK(file_browser_entry(fb, 0).kind == ENTRY_PARENT);
    CHECK_STR(file_browser_entry(fb, 1).name, "fonts");
    CHECK(file_browser_entry(fb, 1).kind == ENTRY_DIR);
    CHECK_STR(file_browser_entry(fb, 2).name, "Pics");
    CHECK_STR(file_browser_entry(fb, 3).name, "A.JPG");
    CHECK(file_browser_entry(fb, 3).kind == ENTRY_FILE);
    CHECK_STR(file_browser_entry(fb, 4).name, "b.png");
    file_browser_free(fb);
}

static void test_dirs_filter_has_use_folder(void)
{
    char err[200] = "", out[1001] = "";
    const char *candidates[] = {root};
    FileBrowser *fb = file_browser_open(candidates, 1, FILTER_DIRS, err, sizeof(err));
    CHECK(fb != NULL);
    if (fb == NULL)
        return;
    CHECK(file_browser_count(fb) == 4);
    CHECK(file_browser_entry(fb, 0).kind == ENTRY_USE_FOLDER);
    CHECK_STR(file_browser_entry(fb, 1).name, "..");
    CHECK_STR(file_browser_entry(fb, 2).name, "fonts");
    CHECK_STR(file_browser_entry(fb, 3).name, "Pics");
    CHECK(file_browser_activate(fb, 0, out, sizeof(out), err, sizeof(err)) == FB_CHOSEN);
    CHECK_STR(out, root);
    file_browser_free(fb);
}

static void test_navigate_and_choose(void)
{
    char err[200] = "", out[1001] = "";
    const char *candidates[] = {root};
    FileBrowser *fb = file_browser_open(candidates, 1, FILTER_FONTS, err, sizeof(err));
    CHECK(fb != NULL);
    if (fb == NULL)
        return;
    CHECK(file_browser_activate(fb, find_entry(fb, "fonts"), out, sizeof(out), err, sizeof(err)) == FB_NAVIGATED);
    CHECK(ends_with(file_browser_dir(fb), "fonts"));
    CHECK(file_browser_activate(fb, find_entry(fb, "x.ttf"), out, sizeof(out), err, sizeof(err)) == FB_CHOSEN);
    CHECK(ends_with(out, "fonts" PATH_SEPARATOR "x.ttf"));
    CHECK(file_browser_activate(fb, find_entry(fb, ".."), out, sizeof(out), err, sizeof(err)) == FB_NAVIGATED);
    CHECK_STR(file_browser_dir(fb), root);
    file_browser_free(fb);
}

static void test_file_candidate_uses_its_folder(void)
{
    char err[200] = "", file[1100];
    snprintf(file, sizeof(file), "%s%sb.png", root, PATH_SEPARATOR);
    const char *candidates[] = {file};
    FileBrowser *fb = file_browser_open(candidates, 1, FILTER_IMAGES, err, sizeof(err));
    CHECK(fb != NULL);
    if (fb == NULL)
        return;
    CHECK_STR(file_browser_dir(fb), root);
    file_browser_free(fb);
}

static void test_open_falls_back_and_fails_cleanly(void)
{
    char err[200] = "";
    const char *a[] = {"/no/such", root};
    FileBrowser *fb = file_browser_open(a, 2, FILTER_ANY, err, sizeof(err));
    CHECK(fb != NULL);
    if (fb != NULL) {
        CHECK_STR(file_browser_dir(fb), root);
        file_browser_free(fb);
    }
    const char *b[] = {"/no/such"};
    err[0] = '\0';
    CHECK(file_browser_open(b, 1, FILTER_ANY, err, sizeof(err)) == NULL);
    CHECK(err[0] != '\0');
}

void suite_file_browser(void)
{
    make_tree();
    test_lists_sorted_filtered();
    test_dirs_filter_has_use_folder();
    test_navigate_and_choose();
    test_file_candidate_uses_its_folder();
    test_open_falls_back_and_fails_cleanly();
}
