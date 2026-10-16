#ifndef SETTINGS_SCHEMA_H
#define SETTINGS_SCHEMA_H
#include <stdbool.h>
#include <stddef.h>

typedef enum {
    SET_BOOL,      // true/false
    SET_CHOICE,    // one of `choices`
    SET_INT,       // whole number in [min, max]
    SET_PERCENT,   // "N%" with N in [min, max]
    SET_LENGTH,    // "N%" (0-100) or whole pixels in [0, max]
    SET_DECIMAL,   // number with optional fraction in [min, max]
    SET_COLOR,     // "#RRGGBB"
    SET_TEXT,      // any non-empty single-line text
    SET_FILE,      // path to a file
    SET_DIR,       // path to a directory
    SET_FONT,      // path to a TTF font
    SET_COMMAND,   // a command or :special command
    SET_MENU_REF   // name of a menu section
} SettingType;

typedef struct {
    const char *id;            // SETTING_* macro name, e.g. "SETTING_VSYNC"
    const char *section;       // INI section, e.g. "General"
    const char *key;           // INI key, e.g. "VSync"
    const char *label;         // UI label
    const char *help;          // one-line UI help
    SettingType type;
    int min;
    int max;
    const char *const *choices;  // SET_CHOICE, NULL-terminated
    const char *default_value;   // shipped sample value, or NULL if the sample leaves it unset
} SettingDef;

extern const SettingDef settings_schema[];
extern const size_t settings_schema_count;
extern const char *const schema_sections[];

const SettingDef *schema_find(const char *section, const char *key);
size_t schema_section_settings(const char *section, const SettingDef **out, size_t max);
bool schema_validate(const SettingDef *def, const char *value, char *err, size_t err_len);

#endif
