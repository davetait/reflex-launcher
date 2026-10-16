#ifndef SETTINGS_MODEL_H
#define SETTINGS_MODEL_H
#include <stdbool.h>
#include <stddef.h>
#include "../settings_schema.h"
#include "../config_io.h"

// Display-free model behind the Settings screen: the config.ini document plus
// the schema, with display, step, set, reset and save operations.

typedef struct settings_model SettingsModel;

typedef enum {
    VALUE_SET,      // the file holds a valid value
    VALUE_DEFAULT,  // unset; the launcher uses the shipped default
    VALUE_NOT_SET,  // unset and there is no default
    VALUE_INVALID   // the file holds a value the launcher would reject
} ValueState;

// Returns NULL and fills err if the file can't be read
SettingsModel *settings_model_open(const char *config_path, char *err, size_t err_len);
// For tests: no file path, so saving fails
SettingsModel *settings_model_open_text(const char *text);
void settings_model_close(SettingsModel *m);

size_t settings_model_section_count(void);
const char *settings_model_section_name(size_t index);
size_t settings_model_row_count(size_t section);
const SettingDef *settings_model_row(size_t section, size_t row);

// Writes the value to show (the active value, the default, or "") and returns its state
ValueState settings_model_display(const SettingsModel *m, const SettingDef *def, char *buf, size_t len);
// Moves a bool, choice, menu or number one step (direction -1 or +1), clamped
bool settings_model_step(SettingsModel *m, const SettingDef *def, int direction, char *err, size_t err_len);
// Validates and stores a value; the document is unchanged on failure
bool settings_model_set(SettingsModel *m, const SettingDef *def, const char *value, char *err, size_t err_len);
void settings_model_reset(SettingsModel *m, const SettingDef *def);
// Malloc'd copies of the choices; the caller frees each string and the array
size_t settings_model_choices(const SettingsModel *m, const SettingDef *def, char ***out);

bool settings_model_dirty(const SettingsModel *m);
const char *settings_model_path(const SettingsModel *m);
ConfigSaveResult settings_model_save(SettingsModel *m, bool force, char *err, size_t err_len);

#endif
