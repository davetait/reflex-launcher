#ifndef FILEOPS_H
#define FILEOPS_H

#include <stdbool.h>
#include <stddef.h>

// Atomically move `from` onto `to`, replacing `to` if it exists. Returns 0 on success.
int replace_file(const char *from, const char *to);


typedef struct {
    char *name;
    bool is_dir;
} DirEntry;

// List the entries of a directory, skipping "." and ".." and names that start with '.'
// (on Windows, hidden files too). is_dir follows symlinks. Returns 0 on success; the caller
// frees the result with free_dir_entries(). On failure *entries is NULL and *count is 0.
int list_directory(const char *path, DirEntry **entries, size_t *count);
void free_dir_entries(DirEntry *entries, size_t count);

// `path` as an absolute path (POSIX realpath(), so it must exist; Windows _fullpath()).
// Returns a malloc'd string the caller frees, or NULL on failure.
char *absolute_path(const char *path);

// The user's home directory ($HOME or %USERPROFILE%), or NULL if unknown
const char *home_directory(void);

#endif
