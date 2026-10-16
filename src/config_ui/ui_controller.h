#ifndef UI_CONTROLLER_H
#define UI_CONTROLLER_H
#include <stdbool.h>
#include <stddef.h>
#include "settings_model.h"
#include "text_input.h"
#include "file_browser.h"

// Display-free controller for the Settings screen: modes, focus and actions.

typedef enum { UI_UP, UI_DOWN, UI_LEFT, UI_RIGHT, UI_SELECT, UI_BACK, UI_RESET, UI_SAVE, UI_ERASE, UI_ACCEPT } UiAction;
typedef enum { UI_RESULT_NONE, UI_RESULT_CLOSE, UI_RESULT_SAVED } UiResult;
typedef enum { MODE_SECTIONS, MODE_ROWS, MODE_CHOICE_LIST, MODE_TEXT, MODE_FILES, MODE_COLOR,
               MODE_CONFIRM_EXIT, MODE_CONFIRM_OVERWRITE, MODE_CONFIRM_RISK, MODE_MESSAGE } UiMode;

#define UI_PALETTE_SIZE 16
extern const char *const ui_palette[UI_PALETTE_SIZE];

typedef struct {
    SettingsModel *model;
    UiMode mode;
    size_t section, row;            // focus
    size_t list_index;              // choice list, dialog button, color cell (0..16; 16 = "Custom hex...")
    char **choices;
    size_t choice_count;
    TextInput *text;
    FileBrowser *files;
    const SettingDef *editing;      // setting being edited in a sub-mode
    UiMode return_mode;             // mode a MESSAGE returns to (also where a save's dialogs return to)
    bool close_after_message;
    bool config_reachable_at_open;  // Settings could be opened with the config as it was loaded
    unsigned risks_accepted;        // risks confirmed with "Save anyway" during the current save
    unsigned risk_shown;            // the risk MODE_CONFIRM_RISK is asking about
    bool input_from_gamepad;        // the last input came from a gamepad (set by config_ui)
    char message[512];              // dialog text (MESSAGE, CONFIRM_*)
    char footer[256];               // transient footer error, cleared on next move
} UiState;

// Takes ownership of the model
void ui_init(UiState *s, SettingsModel *model);
void ui_init_error(UiState *s, const char *message);
void ui_free(UiState *s);
UiResult ui_apply(UiState *s, UiAction a);
// Physical typing; only has an effect in MODE_TEXT
void ui_type(UiState *s, const char *utf8);
// Buttons of the current dialog mode (NULL and 0 for other modes)
const char *const *ui_dialog_buttons(const UiState *s, size_t *count);

#endif
