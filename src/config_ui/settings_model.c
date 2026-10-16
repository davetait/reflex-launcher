#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#ifdef PLATFORM_POSIX
#include <unistd.h>
#endif
#include <SDL.h>
#include "settings_model.h"
#include "../ini_doc.h"
#include "../launcher.h"
#include <launcher_config.h>
#include "../platform/platform.h"
#include "../platform/fileops.h"

#define VALUE_BUF 512
#define CONFIG_COMMAND ":config"
#define SUBMENU_PREFIX ":submenu "

struct settings_model {
    IniDoc *doc;
    char *path;
    char *user_path;  // where a copy of a read-only config goes, or NULL
};

static char *copy_text(const char *s);
static void put(char *buf, size_t len, const char *s);
static bool equals_ignore_case(const char *a, const char *b);
static bool is_menu_section(const SettingsModel *m, const char *section);
static bool value_is_valid(const SettingsModel *m, const SettingDef *def, const char *value);
static bool step_base(const SettingsModel *m, const SettingDef *def, char *base, size_t len);
static bool parse_whole(const char *s, long *out);
static bool parse_percent(const char *s, long *out);
static bool step_choice(SettingsModel *m, const SettingDef *def, const char *base, bool has_base, int direction);
static bool step_number(const SettingDef *def, const char *base, bool has_base, int direction, char *out, size_t out_len);
static long clamp_long(long value, long min, long max);
static size_t find_menu(const char **sections, size_t count, const char *name);
static char *default_user_config_path(void);

SettingsModel *settings_model_open(const char *config_path, char *err, size_t err_len)
{
    IniDoc *doc = ini_doc_load(config_path);
    if (doc == NULL) {
        if (err != NULL && err_len > 0)
            snprintf(err, err_len, "Could not read %s", config_path);
        return NULL;
    }
    SettingsModel *m = calloc(1, sizeof(*m));
    char *path = copy_text(config_path);
    if (m == NULL || path == NULL) {
        free(m);
        free(path);
        ini_doc_free(doc);
        if (err != NULL && err_len > 0)
            snprintf(err, err_len, "Out of memory");
        return NULL;
    }
    m->doc = doc;
    m->path = path;
    m->user_path = default_user_config_path();
    return m;
}

SettingsModel *settings_model_open_text(const char *text)
{
    SettingsModel *m = calloc(1, sizeof(*m));
    if (m == NULL)
        return NULL;
    m->doc = ini_doc_parse(text);
    if (m->doc == NULL) {
        free(m);
        return NULL;
    }
    return m;
}

void settings_model_close(SettingsModel *m)
{
    if (m == NULL)
        return;
    ini_doc_free(m->doc);
    free(m->path);
    free(m->user_path);
    free(m);
}

size_t settings_model_section_count(void)
{
    size_t count = 0;
    while (schema_sections[count] != NULL)
        count++;
    return count;
}

const char *settings_model_section_name(size_t index)
{
    return index < settings_model_section_count() ? schema_sections[index] : NULL;
}

size_t settings_model_row_count(size_t section)
{
    const char *name = settings_model_section_name(section);
    size_t count = 0;
    for (size_t i = 0; name != NULL && i < settings_schema_count; i++) {
        if (strcmp(settings_schema[i].section, name) == 0)
            count++;
    }
    return count;
}

const SettingDef *settings_model_row(size_t section, size_t row)
{
    const char *name = settings_model_section_name(section);
    if (name == NULL)
        return NULL;
    size_t seen = 0;
    for (size_t i = 0; i < settings_schema_count; i++) {
        if (strcmp(settings_schema[i].section, name) == 0) {
            if (seen == row)
                return &settings_schema[i];
            seen++;
        }
    }
    return NULL;
}

ValueState settings_model_display(const SettingsModel *m, const SettingDef *def, char *buf, size_t len)
{
    const char *value = ini_doc_get(m->doc, def->section, def->key);
    if (value != NULL) {
        put(buf, len, value);
        return value_is_valid(m, def, value) ? VALUE_SET : VALUE_INVALID;
    }
    if (def->default_value != NULL) {
        put(buf, len, def->default_value);
        return VALUE_DEFAULT;
    }
    put(buf, len, "");
    return VALUE_NOT_SET;
}

bool settings_model_step(SettingsModel *m, const SettingDef *def, int direction, char *err, size_t err_len)
{
    char base[VALUE_BUF];
    bool has_base = step_base(m, def, base, sizeof(base));
    char next[VALUE_BUF];

    switch (def->type) {
        case SET_BOOL:
            if (has_base && equals_ignore_case(base, "true"))
                put(next, sizeof(next), "false");
            else
                put(next, sizeof(next), "true");
            return settings_model_set(m, def, next, err, err_len);

        case SET_CHOICE:
        case SET_MENU_REF:
            if (!step_choice(m, def, base, has_base, direction)) {
                put(err, err_len, "No options to choose from");
                return false;
            }
            return true;

        case SET_INT:
        case SET_PERCENT:
        case SET_LENGTH:
        case SET_DECIMAL:
            if (!step_number(def, base, has_base, direction, next, sizeof(next))) {
                put(err, err_len, "Can't change this value");
                return false;
            }
            return settings_model_set(m, def, next, err, err_len);

        default:
            put(err, err_len, "Select this setting to edit it");
            return false;
    }
}

bool settings_model_set(SettingsModel *m, const SettingDef *def, const char *value, char *err, size_t err_len)
{
    if (!schema_validate(def, value, err, err_len))
        return false;
    if (def->type == SET_MENU_REF && !is_menu_section(m, value)) {
        snprintf(err, err_len, "\"%s\" is not a menu", value);
        return false;
    }
    if (!ini_doc_set(m->doc, def->section, def->key, value)) {
        put(err, err_len, "This value can't be stored in config.ini");
        return false;
    }
    return true;
}

void settings_model_reset(SettingsModel *m, const SettingDef *def)
{
    ini_doc_unset(m->doc, def->section, def->key);
}

size_t settings_model_choices(const SettingsModel *m, const SettingDef *def, char ***out)
{
    *out = NULL;
    const char *const *source = NULL;
    const char **sections = NULL;
    size_t total = 0;
    if (def->type == SET_CHOICE && def->choices != NULL) {
        source = def->choices;
        while (source[total] != NULL)
            total++;
    }
    else if (def->type == SET_MENU_REF) {
        total = ini_doc_sections(m->doc, &sections);
        source = sections;
    }
    if (source == NULL)
        return 0;

    char **list = malloc((total + 1) * sizeof(char *));
    if (list == NULL)
        return 0;
    size_t count = 0;
    for (size_t i = 0; i < total; i++) {
        if (def->type == SET_MENU_REF && !is_menu_section(m, source[i]))
            continue;
        list[count] = copy_text(source[i]);
        if (list[count] == NULL) {
            while (count > 0)
                free(list[--count]);
            free(list);
            return 0;
        }
        count++;
    }
    *out = list;
    return count;
}

bool settings_model_dirty(const SettingsModel *m)
{
    return ini_doc_dirty_count(m->doc) > 0;
}

bool settings_model_section_dirty(const SettingsModel *m, const char *section)
{
    for (size_t i = 0; i < ini_doc_dirty_count(m->doc); i++) {
        const char *dirty_section, *key;
        ini_doc_dirty_get(m->doc, i, &dirty_section, &key);
        if (strcmp(dirty_section, section) == 0)
            return true;
    }
    return false;
}

bool settings_model_config_reachable(const SettingsModel *m)
{
    bool found = false;

    // Menus: breadth-first from the home menu through ":submenu" entries
    const char **sections = NULL;
    size_t section_count = ini_doc_sections(m->doc, &sections);
    bool *queued = calloc(section_count + 1, sizeof(bool));
    size_t *queue = malloc((section_count + 1) * sizeof(size_t));
    size_t head = 0, tail = 0;
    if (sections != NULL && queued != NULL && queue != NULL) {
        size_t start = find_menu(sections, section_count, ini_doc_get(m->doc, "General", "DefaultMenu"));
        if (start < section_count) {
            queued[start] = true;
            queue[tail++] = start;
        }
    }
    while (!found && head < tail) {
        const char **values = NULL;
        size_t count = ini_doc_get_list(m->doc, sections[queue[head++]], &values);
        for (size_t i = 0; !found && i < count; i++) {
            char *cmd = config_entry_command(values[i]);
            if (cmd != NULL && strcmp(cmd, CONFIG_COMMAND) == 0) {
                found = true;
            }
            else if (cmd != NULL && strncmp(cmd, SUBMENU_PREFIX, strlen(SUBMENU_PREFIX)) == 0) {
                size_t next = find_menu(sections, section_count, cmd + strlen(SUBMENU_PREFIX));
                if (next < section_count && !queued[next]) {
                    queued[next] = true;
                    queue[tail++] = next;
                }
            }
            free(cmd);
        }
        free(values);
    }
    free(queue);
    free(queued);
    free(sections);

    // Hotkeys
    const char **hotkeys = NULL;
    size_t hotkey_count = ini_doc_get_list(m->doc, "Hotkeys", &hotkeys);
    for (size_t i = 0; !found && i < hotkey_count; i++) {
        char *cmd = config_hotkey_command(hotkeys[i]);
        found = cmd != NULL && strcmp(cmd, CONFIG_COMMAND) == 0;
        free(cmd);
    }
    free(hotkeys);

    // Gamepad bindings
    for (size_t i = 0; !found && i < settings_schema_count; i++) {
        const SettingDef *def = &settings_schema[i];
        if (def->type == SET_COMMAND && strcmp(def->section, "Gamepad") == 0) {
            const char *value = ini_doc_get(m->doc, def->section, def->key);
            found = value != NULL && strcmp(value, CONFIG_COMMAND) == 0;
        }
    }
    return found;
}

const char *settings_model_path(const SettingsModel *m)
{
    return m->path;
}

ConfigSaveResult settings_model_save(SettingsModel *m, bool force, char *err, size_t err_len)
{
    if (m->path == NULL) {
        put(err, err_len, "No config file to save to");
        return CONFIG_SAVE_WRITE_FAILED;
    }
    return config_save(m->doc, m->path, force, err, err_len);
}

ConfigSaveResult settings_model_save_as(SettingsModel *m, const char *path, char *err, size_t err_len)
{
    char *new_path = copy_text(path);
    if (new_path == NULL) {
        put(err, err_len, "Out of memory");
        return CONFIG_SAVE_WRITE_FAILED;
    }
#ifdef PLATFORM_POSIX
    char *folder = copy_text(path);
    char *slash = folder != NULL ? strrchr(folder, '/') : NULL;
    if (slash != NULL && slash != folder) {
        *slash = '\0';
        make_directory(folder);
    }
    free(folder);
#endif
    ConfigSaveResult result = config_save(m->doc, new_path, true, err, err_len);
    if (result == CONFIG_SAVE_OK) {
        free(m->path);
        m->path = new_path;
    }
    else
        free(new_path);
    return result;
}

bool settings_model_writable(const SettingsModel *m)
{
#ifdef PLATFORM_POSIX
    if (m->path == NULL)
        return true;
    // Saving replaces the file through a temporary file in its folder, so both must be writable
    char *folder = copy_text(m->path);
    if (folder == NULL)
        return true;
    char *slash = strrchr(folder, '/');
    if (slash == folder)
        folder[1] = '\0';
    else if (slash != NULL)
        *slash = '\0';
    bool writable = access(m->path, W_OK) == 0 && access(slash != NULL ? folder : ".", W_OK) == 0;
    free(folder);
    return writable;
#else
    (void) m;
    return true;
#endif
}

const char *settings_model_user_config_path(const SettingsModel *m)
{
    return m->user_path;
}

void settings_model_set_user_config_path(SettingsModel *m, const char *path)
{
    free(m->user_path);
    m->user_path = path != NULL ? copy_text(path) : NULL;
}

static char *copy_text(const char *s)
{
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
    if (copy == NULL)
        return NULL;
    memcpy(copy, s, len + 1);
    return copy;
}

static void put(char *buf, size_t len, const char *s)
{
    if (buf == NULL || len == 0)
        return;
    snprintf(buf, len, "%s", s);
}

static bool equals_ignore_case(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char) *a) != tolower((unsigned char) *b))
            return false;
    }
    return *a == *b;
}

static bool is_menu_section(const SettingsModel *m, const char *section)
{
    return ini_doc_has_section(m->doc, section) && config_is_menu_section(section);
}

static bool value_is_valid(const SettingsModel *m, const SettingDef *def, const char *value)
{
    char ignored[256];
    if (!schema_validate(def, value, ignored, sizeof(ignored)))
        return false;
    return def->type != SET_MENU_REF || is_menu_section(m, value);
}

// The value stepping starts from: the active value if valid, else the default, else none
static bool step_base(const SettingsModel *m, const SettingDef *def, char *base, size_t len)
{
    const char *value = ini_doc_get(m->doc, def->section, def->key);
    if (value != NULL && value_is_valid(m, def, value)) {
        put(base, len, value);
        return true;
    }
    if (def->default_value != NULL) {
        put(base, len, def->default_value);
        return true;
    }
    put(base, len, "");
    return false;
}

static bool parse_whole(const char *s, long *out)
{
    if (*s == '\0')
        return false;
    char *end;
    errno = 0;
    long value = strtol(s, &end, 10);
    if (*end != '\0' || errno != 0)
        return false;
    *out = value;
    return true;
}

// "N%" -> N
static bool parse_percent(const char *s, long *out)
{
    size_t len = strlen(s);
    if (len < 2 || s[len - 1] != '%')
        return false;
    char number[VALUE_BUF];
    put(number, sizeof(number), s);
    number[len - 1] = '\0';
    return parse_whole(number, out);
}

// ~/.config/flex-launcher/config.ini, where the launcher looks before the system-wide config
static char *default_user_config_path(void)
{
#ifdef PLATFORM_POSIX
    const char *home = home_directory();
    if (home == NULL)
        return NULL;
    size_t len = strlen(home) + sizeof("/.config/" EXECUTABLE_TITLE "/" FILENAME_DEFAULT_CONFIG);
    char *path = malloc(len);
    if (path != NULL)
        snprintf(path, len, "%s/.config/" EXECUTABLE_TITLE "/" FILENAME_DEFAULT_CONFIG, home);
    return path;
#else
    return NULL;
#endif
}

// Index of the menu section `name` in `sections`, or `count` if it isn't one
static size_t find_menu(const char **sections, size_t count, const char *name)
{
    if (name == NULL || !config_is_menu_section(name))
        return count;
    for (size_t i = 0; i < count; i++) {
        if (strcmp(sections[i], name) == 0)
            return i;
    }
    return count;
}

static long clamp_long(long value, long min, long max)
{
    return value < min ? min : (value > max ? max : value);
}

static bool step_choice(SettingsModel *m, const SettingDef *def, const char *base, bool has_base, int direction)
{
    char **choices;
    size_t count = settings_model_choices(m, def, &choices);
    bool ok = false;
    if (count > 0) {
        size_t target = 0;
        if (has_base) {
            size_t current = count;
            for (size_t i = 0; i < count; i++) {
                if (strcmp(choices[i], base) == 0)
                    current = i;
            }
            if (current < count)
                target = direction < 0 ? (current + count - 1) % count : (current + 1) % count;
        }
        char err[256];
        ok = settings_model_set(m, def, choices[target], err, sizeof(err));
    }
    for (size_t i = 0; i < count; i++)
        free(choices[i]);
    free(choices);
    return ok;
}

static bool step_number(const SettingDef *def, const char *base, bool has_base, int direction, char *out, size_t out_len)
{
    int sign = direction < 0 ? -1 : 1;
    long n;

    if (def->type == SET_DECIMAL) {
        if (!has_base) {
            snprintf(out, out_len, "%g", (double) def->min);
            return true;
        }
        double d = strtod(base, NULL) + 0.5 * sign;
        d = d < def->min ? def->min : (d > def->max ? def->max : d);
        snprintf(out, out_len, "%g", d);
        return true;
    }

    if (def->type == SET_PERCENT) {
        if (!has_base || !parse_percent(base, &n))
            snprintf(out, out_len, "0%%");
        else
            snprintf(out, out_len, "%ld%%", clamp_long(n + 5 * sign, def->min, def->max));
        return true;
    }

    if (def->type == SET_LENGTH) {
        if (!has_base)
            snprintf(out, out_len, "0");
        else if (parse_percent(base, &n))
            snprintf(out, out_len, "%ld%%", clamp_long(n + 5 * sign, 0, 100));
        else if (parse_whole(base, &n))
            snprintf(out, out_len, "%ld", clamp_long(n + 5 * sign, 0, def->max));
        else
            return false;
        return true;
    }

    // SET_INT
    if (!has_base || !parse_whole(base, &n)) {
        snprintf(out, out_len, "%d", def->min);
        return true;
    }
    int range = def->max - def->min;
    long step = range <= 100 ? 1 : (range <= 1000 ? 10 : 100);
    snprintf(out, out_len, "%ld", clamp_long(n + step * sign, def->min, def->max));
    return true;
}
