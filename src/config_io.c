#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ini.h>
#include "config_io.h"
#include "settings_schema.h"

// inih reads lines with fgets(INI_MAX_LINE): the text, CR/LF, and NUL must fit
#define MAX_LINE_CHARS (INI_MAX_LINE - 3)

#ifdef _WIN32
#define USER_CONFIG_HINT "the folder that contains flex-launcher.exe"
#else
#define USER_CONFIG_HINT "~/.config/flex-launcher/config.ini"
#endif

static bool fail(char *err, size_t err_len, const char *format, ...);

static bool fail(char *err, size_t err_len, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(err, err_len, format, args);
    va_end(args);
    return false;
}

// Mirrors config_handler(): any section that isn't a settings section or [Hotkeys] is a menu
bool config_is_menu_section(const char *section)
{
    if (section[0] == '\0' || strcmp(section, "Hotkeys") == 0)
        return false;
    for (const char *const *s = schema_sections; *s != NULL; s++) {
        if (strcmp(*s, section) == 0)
            return false;
    }
    return true;
}

// Mirrors config_handler()'s strtok parsing: the first two non-empty ';' tokens are the
// title and icon, the remainder is the command. Returns a malloc'd command or NULL.
char *config_entry_command(const char *value)
{
    char *copy = strdup(value);
    char *title = strtok(copy, ";");
    char *icon = title != NULL ? strtok(NULL, ";") : NULL;
    char *cmd = icon != NULL ? strtok(NULL, "") : NULL;
    char *result = cmd != NULL ? strdup(cmd) : NULL;
    free(copy);
    return result;
}

bool config_entry_is_valid(const char *value)
{
    char *cmd = config_entry_command(value);
    bool valid = cmd != NULL && strcmp(cmd, ":select") != 0;
    free(cmd);
    return valid;
}

// Mirrors the [Hotkeys] branch of config_handler() and add_hotkey()
char *config_hotkey_command(const char *value)
{
    char *copy = strdup(value);
    char *keycode = strtok(copy, ";");
    char *cmd = keycode != NULL ? strtok(NULL, "") : NULL;
    bool valid = keycode != NULL && keycode[0] == '#' && keycode[1] != '\0' && cmd != NULL;
    char *result = valid ? strdup(cmd) : NULL;
    free(copy);
    return result;
}

bool config_check(const IniDoc *doc, char *err, size_t err_len)
{
    const char *default_menu = ini_doc_get(doc, "General", "DefaultMenu");
    if (default_menu == NULL)
        return fail(err, err_len, "No default menu is set ([General] DefaultMenu)");
    if (!ini_doc_has_section(doc, default_menu))
        return fail(err, err_len, "The default menu \"%s\" does not exist", default_menu);
    if (!config_is_menu_section(default_menu))
        return fail(err, err_len, "The default menu \"%s\" is a settings section, not a menu", default_menu);

    const char **sections = NULL;
    size_t section_count = ini_doc_sections(doc, &sections);
    bool ok = true;
    for (size_t s = 0; ok && s < section_count; s++) {
        const char *section = sections[s];
        const char **values = NULL;
        size_t count = ini_doc_get_list(doc, section, &values);
        if (strcmp(section, "Hotkeys") == 0) {
            for (size_t i = 0; ok && i < count; i++) {
                char *cmd = config_hotkey_command(values[i]);
                if (cmd == NULL)
                    ok = fail(err, err_len, "Hotkey %zu in [Hotkeys] must look like #1B;command", i + 1);
                free(cmd);
            }
        }
        else if (config_is_menu_section(section)) {
            if (count == 0 && strcmp(section, default_menu) == 0)
                ok = fail(err, err_len, "The default menu \"%s\" has no entries", section);
            for (size_t i = 0; ok && i < count; i++) {
                char *cmd = config_entry_command(values[i]);
                if (cmd == NULL || strcmp(cmd, ":select") == 0)
                    ok = fail(err, err_len, "Entry %zu in [%s] needs a title, an icon and a command", i + 1, section);
                else if (strncmp(cmd, ":submenu ", 9) == 0 && !ini_doc_has_section(doc, cmd + 9))
                    ok = fail(err, err_len, "Entry %zu in [%s] opens submenu \"%s\", which does not exist", i + 1, section, cmd + 9);
                free(cmd);
            }
        }
        free(values);
    }
    free(sections);
    return ok;
}

ConfigSaveResult config_save(IniDoc *doc, const char *path, bool force, char *err, size_t err_len)
{
    // Validate only what the user changed; untouched bad values are ignored by the parser as before
    for (size_t i = 0; i < ini_doc_dirty_count(doc); i++) {
        const char *section, *key;
        ini_doc_dirty_get(doc, i, &section, &key);
        const SettingDef *def = schema_find(section, key);
        if (def != NULL) {
            const char *value = ini_doc_get(doc, section, key);
            if (value == NULL)
                continue;
            if (!schema_validate(def, value, err, err_len))
                return CONFIG_SAVE_INVALID;
            if (strlen(key) + 1 + strlen(value) > MAX_LINE_CHARS) {
                fail(err, err_len, "%s is too long (max %d characters)", def->label, (int) (MAX_LINE_CHARS - strlen(key) - 1));
                return CONFIG_SAVE_INVALID;
            }
        }
        else {
            const char **values = NULL;
            size_t count = ini_doc_get_list(doc, section, &values);
            bool too_long = false;
            for (size_t j = 0; j < count && !too_long; j++)
                too_long = strlen(key) + 4 + strlen(values[j]) > MAX_LINE_CHARS;
            free(values);
            if (too_long) {
                fail(err, err_len, "An entry in [%s] is too long (max about %d characters)", section, (int) (MAX_LINE_CHARS - strlen(key) - 4));
                return CONFIG_SAVE_INVALID;
            }
        }
    }
    if (!config_check(doc, err, err_len))
        return CONFIG_SAVE_INVALID;
    if (!force && ini_doc_changed_on_disk(doc, path)) {
        fail(err, err_len, "%s was changed by another program since it was opened", path);
        return CONFIG_SAVE_CHANGED_ON_DISK;
    }
    if (ini_doc_save(doc, path) != 0) {
        fail(err, err_len, "Could not write %s. If it is a system-wide file, copy it to " USER_CONFIG_HINT " and edit that copy.", path);
        return CONFIG_SAVE_WRITE_FAILED;
    }
    ini_doc_mark_saved(doc, path);
    return CONFIG_SAVE_OK;
}
