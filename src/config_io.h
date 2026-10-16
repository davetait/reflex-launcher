#ifndef CONFIG_IO_H
#define CONFIG_IO_H
#include <stdbool.h>
#include <stddef.h>
#include "ini_doc.h"

typedef enum {
    CONFIG_SAVE_OK = 0,
    CONFIG_SAVE_INVALID,          // a changed value or the menu structure is invalid; nothing written
    CONFIG_SAVE_CHANGED_ON_DISK,  // another program edited the file since it was loaded
    CONFIG_SAVE_WRITE_FAILED      // the file couldn't be written; nothing changed
} ConfigSaveResult;

bool config_check(const IniDoc *doc, char *err, size_t err_len);
// True for any section name that is neither a settings section nor [Hotkeys]
bool config_is_menu_section(const char *section);
bool config_entry_is_valid(const char *value);
ConfigSaveResult config_save(IniDoc *doc, const char *path, bool force, char *err, size_t err_len);

#endif
