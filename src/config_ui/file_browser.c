#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include <SDL.h>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif
#include "file_browser.h"
#include "../launcher.h"
#include "../util.h"
#include "../platform/fileops.h"

#define USE_FOLDER_LABEL "Use this folder"
#define PARENT_LABEL ".."

typedef struct {
    char *name; // owned for ENTRY_DIR and ENTRY_FILE, a string literal otherwise
    EntryKind kind;
} Item;

struct file_browser {
    char dir[MAX_PATH_CHARS];
    FileFilter filter;
    Item *items;
    size_t count;
};

static const char *const image_extensions[] = {".png", ".jpg", ".jpeg", ".webp", ".svg", ".bmp", NULL};
static const char *const font_extensions[] = {".ttf", ".otf", NULL};

static bool is_separator(char c);
static bool is_root(const char *path);
static bool strip_last_component(char *path);
static bool join_path(char *out, size_t out_len, const char *dir, const char *name);
static int compare_ignore_case(const char *a, const char *b);
static int compare_dir_entries(const void *a, const void *b);
static bool has_extension(const char *name, const char *const *extensions);
static bool file_matches(FileFilter filter, const char *name);
static void free_items(Item *items, size_t count);
static bool load_directory(FileBrowser *fb, const char *path);
static bool is_existing_file(const char *path);
static void set_error(char *err, size_t err_len, const char *path);

static bool is_separator(char c)
{
#ifdef _WIN32
    return c == '\\' || c == '/';
#else
    return c == '/';
#endif
}

// "/" on POSIX, "X:\" on Windows
static bool is_root(const char *path)
{
#ifdef _WIN32
    return isalpha((unsigned char) path[0]) && path[1] == ':' && is_separator(path[2]) && path[3] == '\0';
#else
    return path[0] == '/' && path[1] == '\0';
#endif
}

// Removes the last path component, leaving the root intact. Returns false if there is nothing to remove.
static bool strip_last_component(char *path)
{
    size_t length = strlen(path);
    while (length > 1 && is_separator(path[length - 1]) && !is_root(path))
        path[--length] = '\0';
    if (is_root(path))
        return false;
    size_t i = length;
    while (i > 0 && !is_separator(path[i - 1]))
        i--;
    if (i == 0)
        return false;
    path[i - 1] = '\0';
    if (path[0] == '\0') { // "/name" -> "/"
        strcpy(path, PATH_SEPARATOR);
    }
#ifdef _WIN32
    else if (path[1] == ':' && path[2] == '\0') { // "C:\name" -> "C:\"
        strcat(path, PATH_SEPARATOR);
    }
#endif
    return true;
}

static bool join_path(char *out, size_t out_len, const char *dir, const char *name)
{
    size_t dir_length = strlen(dir);
    bool needs_separator = dir_length > 0 && !is_separator(dir[dir_length - 1]);
    int written = snprintf(out, out_len, "%s%s%s", dir, needs_separator ? PATH_SEPARATOR : "", name);
    return written >= 0 && (size_t) written < out_len;
}

static int compare_ignore_case(const char *a, const char *b)
{
    while (*a != '\0' && *b != '\0') {
        int ca = tolower((unsigned char) *a), cb = tolower((unsigned char) *b);
        if (ca != cb)
            return ca - cb;
        a++;
        b++;
    }
    return tolower((unsigned char) *a) - tolower((unsigned char) *b);
}

// Folders first, then files, each sorted case-insensitively (exact bytes break ties)
static int compare_dir_entries(const void *a, const void *b)
{
    const DirEntry *x = a, *y = b;
    if (x->is_dir != y->is_dir)
        return x->is_dir ? -1 : 1;
    int result = compare_ignore_case(x->name, y->name);
    return result != 0 ? result : strcmp(x->name, y->name);
}

static bool has_extension(const char *name, const char *const *extensions)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL)
        return false;
    for (size_t i = 0; extensions[i] != NULL; i++) {
        if (compare_ignore_case(dot, extensions[i]) == 0)
            return true;
    }
    return false;
}

static bool file_matches(FileFilter filter, const char *name)
{
    switch (filter) {
        case FILTER_IMAGES:
            return has_extension(name, image_extensions);
        case FILTER_FONTS:
            return has_extension(name, font_extensions);
        case FILTER_ANY:
            return true;
        case FILTER_DIRS:
        default:
            return false;
    }
}

// True if path exists and is not a folder
static bool is_existing_file(const char *path)
{
#ifdef _WIN32
    DWORD attributes = GetFileAttributesA(path);
    return attributes != INVALID_FILE_ATTRIBUTES && !(attributes & FILE_ATTRIBUTE_DIRECTORY);
#else
    struct stat info;
    return stat(path, &info) == 0 && !S_ISDIR(info.st_mode);
#endif
}

static void free_items(Item *items, size_t count)
{
    for (size_t i = 0; i < count; i++) {
        if (items[i].kind == ENTRY_DIR || items[i].kind == ENTRY_FILE)
            free(items[i].name);
    }
    free(items);
}

static void set_error(char *err, size_t err_len, const char *path)
{
    if (err != NULL && err_len > 0)
        snprintf(err, err_len, "Can't open %s", path);
}

// Lists `path` and, only if that works, makes it the current folder
static bool load_directory(FileBrowser *fb, const char *path)
{
    DirEntry *entries;
    size_t entry_count;
    if (strlen(path) >= sizeof(fb->dir) || list_directory(path, &entries, &entry_count) != 0)
        return false;
    if (entry_count > 1)
        qsort(entries, entry_count, sizeof(DirEntry), compare_dir_entries);

    Item *items = malloc((entry_count + 2) * sizeof(Item));
    if (items == NULL) {
        free_dir_entries(entries, entry_count);
        return false;
    }
    size_t count = 0;
    if (fb->filter == FILTER_DIRS) {
        items[count].name = USE_FOLDER_LABEL;
        items[count++].kind = ENTRY_USE_FOLDER;
    }
    if (!is_root(path)) {
        items[count].name = PARENT_LABEL;
        items[count++].kind = ENTRY_PARENT;
    }
    // Take ownership of the names we keep and free the rest
    for (size_t i = 0; i < entry_count; i++) {
        if (entries[i].is_dir || file_matches(fb->filter, entries[i].name)) {
            items[count].name = entries[i].name;
            items[count++].kind = entries[i].is_dir ? ENTRY_DIR : ENTRY_FILE;
        } else {
            free(entries[i].name);
        }
    }
    free(entries);

    free_items(fb->items, fb->count);
    fb->items = items;
    fb->count = count;
    strcpy(fb->dir, path);
    return true;
}

FileBrowser *file_browser_open(const char *const *candidates, size_t candidate_count, FileFilter filter,
                               char *err, size_t err_len)
{
    FileBrowser *fb = calloc(1, sizeof(FileBrowser));
    if (fb == NULL) {
        if (err != NULL && err_len > 0)
            snprintf(err, err_len, "Out of memory");
        return NULL;
    }
    fb->filter = filter;
    for (size_t i = 0; i < candidate_count; i++) {
        if (candidates[i] == NULL || candidates[i][0] == '\0')
            continue;
        // Absolute, so ".." can always go up (a relative "." has no parent to strip)
        char *absolute = absolute_path(candidates[i]);
        const char *candidate = absolute != NULL ? absolute : candidates[i];
        char path[MAX_PATH_CHARS];
        bool fits = strlen(candidate) < sizeof(path);
        if (fits)
            strcpy(path, candidate);
        free(absolute);
        if (!fits)
            continue;
        if (load_directory(fb, path))
            return fb;
        // A file path starts in its folder. A folder that can't be listed moves on to the next candidate.
        if (is_existing_file(path) && strip_last_component(path) && load_directory(fb, path))
            return fb;
    }
    if (err != NULL && err_len > 0) {
        if (candidate_count > 0 && candidates[0] != NULL && candidates[0][0] != '\0')
            set_error(err, err_len, candidates[0]);
        else
            snprintf(err, err_len, "No folder to open");
    }
    free(fb);
    return NULL;
}

void file_browser_free(FileBrowser *fb)
{
    if (fb == NULL)
        return;
    free_items(fb->items, fb->count);
    free(fb);
}

const char *file_browser_dir(const FileBrowser *fb)
{
    return fb->dir;
}

size_t file_browser_count(const FileBrowser *fb)
{
    return fb->count;
}

FileEntry file_browser_entry(const FileBrowser *fb, size_t index)
{
    FileEntry entry = {"", ENTRY_FILE};
    if (index < fb->count) {
        entry.name = fb->items[index].name;
        entry.kind = fb->items[index].kind;
    }
    return entry;
}

size_t file_browser_find(const FileBrowser *fb, const char *name)
{
    for (size_t i = 0; i < fb->count; i++) {
        if (strcmp(fb->items[i].name, name) == 0)
            return i;
    }
    return (size_t) -1;
}

FileBrowserResult file_browser_activate(FileBrowser *fb, size_t index, char *out_path, size_t out_len,
                                        char *err, size_t err_len)
{
    if (index >= fb->count) {
        if (err != NULL && err_len > 0)
            snprintf(err, err_len, "No such entry");
        return FB_ERROR;
    }
    char path[MAX_PATH_CHARS];
    switch (fb->items[index].kind) {
        case ENTRY_USE_FOLDER:
            if (out_path == NULL || strlen(fb->dir) >= out_len) {
                if (err != NULL && err_len > 0)
                    snprintf(err, err_len, "Path too long");
                return FB_ERROR;
            }
            strcpy(out_path, fb->dir);
            return FB_CHOSEN;
        case ENTRY_FILE:
            if (out_path == NULL || !join_path(path, sizeof(path), fb->dir, fb->items[index].name)
                || strlen(path) >= out_len) {
                if (err != NULL && err_len > 0)
                    snprintf(err, err_len, "Path too long");
                return FB_ERROR;
            }
            strcpy(out_path, path);
            return FB_CHOSEN;
        case ENTRY_PARENT:
            strcpy(path, fb->dir);
            strip_last_component(path);
            break;
        case ENTRY_DIR:
        default:
            if (!join_path(path, sizeof(path), fb->dir, fb->items[index].name)) {
                if (err != NULL && err_len > 0)
                    snprintf(err, err_len, "Path too long");
                return FB_ERROR;
            }
            break;
    }
    if (!load_directory(fb, path)) {
        set_error(err, err_len, path);
        return FB_ERROR;
    }
    return FB_NAVIGATED;
}
