#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <errno.h>
#include <ctype.h>
#include "settings_model.h"
#include "../ini_doc.h"

#define VALUE_BUF 512

struct settings_model {
    IniDoc *doc;
    char *path;
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

SettingsModel *settings_model_open(const char *config_path, char *err, size_t err_len)
{
    IniDoc *doc = ini_doc_load(config_path);
    if (doc == NULL) {
        snprintf(err, err_len, "Could not read %s", config_path);
        return NULL;
    }
    SettingsModel *m = calloc(1, sizeof(*m));
    m->doc = doc;
    m->path = copy_text(config_path);
    return m;
}

SettingsModel *settings_model_open_text(const char *text)
{
    SettingsModel *m = calloc(1, sizeof(*m));
    m->doc = ini_doc_parse(text);
    return m;
}

void settings_model_close(SettingsModel *m)
{
    if (m == NULL)
        return;
    ini_doc_free(m->doc);
    free(m->path);
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
    size_t count = 0;
    if (def->type == SET_CHOICE && def->choices != NULL) {
        while (def->choices[count] != NULL)
            count++;
        *out = malloc((count + 1) * sizeof(char *));
        for (size_t i = 0; i < count; i++)
            (*out)[i] = copy_text(def->choices[i]);
        return count;
    }
    if (def->type == SET_MENU_REF) {
        const char **sections = NULL;
        size_t total = ini_doc_sections(m->doc, &sections);
        *out = malloc((total + 1) * sizeof(char *));
        for (size_t i = 0; i < total; i++) {
            if (is_menu_section(m, sections[i]))
                (*out)[count++] = copy_text(sections[i]);
        }
    }
    return count;
}

bool settings_model_dirty(const SettingsModel *m)
{
    return ini_doc_dirty_count(m->doc) > 0;
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

static char *copy_text(const char *s)
{
    size_t len = strlen(s);
    char *copy = malloc(len + 1);
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

// Mirrors config_io: any section that isn't a settings section or [Hotkeys] is a menu
static bool is_menu_section(const SettingsModel *m, const char *section)
{
    if (!ini_doc_has_section(m->doc, section) || strcmp(section, "Hotkeys") == 0)
        return false;
    for (size_t i = 0; schema_sections[i] != NULL; i++) {
        if (strcmp(schema_sections[i], section) == 0)
            return false;
    }
    return true;
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
