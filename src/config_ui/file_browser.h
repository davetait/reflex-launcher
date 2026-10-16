#ifndef FILE_BROWSER_H
#define FILE_BROWSER_H

#include <stddef.h>

typedef enum { FILTER_IMAGES, FILTER_FONTS, FILTER_DIRS, FILTER_ANY } FileFilter;
typedef enum { ENTRY_USE_FOLDER, ENTRY_PARENT, ENTRY_DIR, ENTRY_FILE } EntryKind;
typedef struct {
    const char *name;
    EntryKind kind;
} FileEntry;
typedef enum { FB_NAVIGATED, FB_CHOSEN, FB_ERROR } FileBrowserResult;
typedef struct file_browser FileBrowser;

// Opens the first candidate that can be listed. A candidate that is a file starts in its folder.
// Returns NULL (with a message in err) if none can be listed.
FileBrowser *file_browser_open(const char *const *candidates, size_t candidate_count, FileFilter filter,
                               char *err, size_t err_len);
void file_browser_free(FileBrowser *fb);
const char *file_browser_dir(const FileBrowser *fb);
size_t file_browser_count(const FileBrowser *fb);
// Entry names stay valid until the next activate or free
FileEntry file_browser_entry(const FileBrowser *fb, size_t index);
// Index of the entry called `name`, or (size_t) -1 if there is none
size_t file_browser_find(const FileBrowser *fb, const char *name);
// Folders navigate; files and "Use this folder" write the full path to out_path
FileBrowserResult file_browser_activate(FileBrowser *fb, size_t index, char *out_path, size_t out_len,
                                        char *err, size_t err_len);

#endif
