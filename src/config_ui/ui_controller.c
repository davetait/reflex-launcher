#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "ui_controller.h"
#include "../platform/fileops.h"

#define VALUE_BUF 512
#define PATH_BUF 1024
#define CUSTOM_CELL UI_PALETTE_SIZE
#define RISK_LOCKOUT 1u
#define RISK_GAMEPAD 2u

const char *const ui_palette[UI_PALETTE_SIZE] = {
    "#FFFFFF", "#C0C0C0", "#808080", "#000000", "#FF3B30", "#FF9500", "#FFCC00", "#34C759",
    "#00C7BE", "#30B0C7", "#007AFF", "#5856D6", "#AF52DE", "#FF2D55", "#A2845E", "#1C1C1E"
};

static const char *const exit_buttons[] = {"Save", "Discard", "Keep editing"};
static const char *const overwrite_buttons[] = {"Overwrite", "Cancel"};
static const char *const risk_buttons[] = {"Keep editing", "Save anyway"};
static const char *const copy_buttons[] = {"Save copy", "Cancel"};
static const char *const lockout_message = "After saving, Settings can't be opened from the home menu. Save anyway?";
static const char *const gamepad_message =
    "These gamepad changes may stop your controller from working in the launcher. Save anyway?";
static const char *const message_buttons[] = {"OK"};

static void put(char *buf, size_t len, const char *s);
static bool equals_ignore_case(const char *a, const char *b);
static const SettingDef *current_def(const UiState *s);
static bool is_steppable(SettingType type);
static void clear_sub_state(UiState *s);
static void show_message(UiState *s, UiMode return_mode, const char *text);
static UiResult save(UiState *s);
static UiResult continue_save(UiState *s, bool force);
static bool gamepad_at_risk(const UiState *s);
static UiResult apply_sections(UiState *s, UiAction a);
static UiResult apply_rows(UiState *s, UiAction a);
static UiResult apply_choice_list(UiState *s, UiAction a);
static UiResult apply_text(UiState *s, UiAction a);
static UiResult apply_files(UiState *s, UiAction a);
static UiResult apply_color(UiState *s, UiAction a);
static UiResult apply_confirm_exit(UiState *s, UiAction a);
static UiResult apply_confirm_overwrite(UiState *s, UiAction a);
static UiResult apply_confirm_risk(UiState *s, UiAction a);
static UiResult apply_confirm_copy(UiState *s, UiAction a);
static bool offer_copy(UiState *s);
static UiResult apply_message(UiState *s, UiAction a);
static void open_editor(UiState *s, const SettingDef *def);
static void open_choice_list(UiState *s, const SettingDef *def);
static void open_text(UiState *s, const SettingDef *def, TextLayout layout);
static void open_color(UiState *s, const SettingDef *def);
static void open_files(UiState *s, const SettingDef *def);
static const char *last_separator(const char *path);
static bool config_folder(const UiState *s, char *buf, size_t len);
static void focus_file(UiState *s, const char *file);
static void leave_to_rows(UiState *s);
static bool move_in_list(UiState *s, UiAction a, size_t count);
static UiResult finish_text(UiState *s);
static UiResult leave_and_save(UiState *s);

void ui_init(UiState *s, SettingsModel *model)
{
    memset(s, 0, sizeof(*s));
    s->model = model;
    s->mode = MODE_SECTIONS;
    s->config_reachable_at_open = model != NULL && settings_model_config_reachable(model);
    if (model != NULL && settings_model_path(model) != NULL && !settings_model_writable(model))
        put(s->footer, sizeof(s->footer), "This config is read-only \xE2\x80\x94 Save will offer to save a copy");
}

void ui_init_error(UiState *s, const char *message)
{
    memset(s, 0, sizeof(*s));
    s->mode = MODE_MESSAGE;
    s->close_after_message = true;
    put(s->message, sizeof(s->message), message);
}

void ui_free(UiState *s)
{
    clear_sub_state(s);
    settings_model_close(s->model);
    s->model = NULL;
}

UiResult ui_apply(UiState *s, UiAction a)
{
    s->footer[0] = '\0';
    switch (s->mode) {
        case MODE_SECTIONS: return apply_sections(s, a);
        case MODE_ROWS: return apply_rows(s, a);
        case MODE_CHOICE_LIST: return apply_choice_list(s, a);
        case MODE_TEXT: return apply_text(s, a);
        case MODE_FILES: return apply_files(s, a);
        case MODE_COLOR: return apply_color(s, a);
        case MODE_CONFIRM_EXIT: return apply_confirm_exit(s, a);
        case MODE_CONFIRM_OVERWRITE: return apply_confirm_overwrite(s, a);
        case MODE_CONFIRM_RISK: return apply_confirm_risk(s, a);
        case MODE_CONFIRM_COPY: return apply_confirm_copy(s, a);
        case MODE_MESSAGE: return apply_message(s, a);
    }
    return UI_RESULT_NONE;
}

void ui_type(UiState *s, const char *utf8)
{
    if (s->mode != MODE_TEXT || s->text == NULL || utf8 == NULL)
        return;
    s->footer[0] = '\0';
    text_input_type(s->text, utf8);
}

const char *const *ui_dialog_buttons(const UiState *s, size_t *count)
{
    switch (s->mode) {
        case MODE_CONFIRM_EXIT:
            *count = 3;
            return exit_buttons;
        case MODE_CONFIRM_OVERWRITE:
            *count = 2;
            return overwrite_buttons;
        case MODE_CONFIRM_RISK:
            *count = 2;
            return risk_buttons;
        case MODE_CONFIRM_COPY:
            *count = 2;
            return copy_buttons;
        case MODE_MESSAGE:
            *count = 1;
            return message_buttons;
        default:
            *count = 0;
            return NULL;
    }
}

static void put(char *buf, size_t len, const char *s)
{
    if (len == 0)
        return;
    snprintf(buf, len, "%s", s != NULL ? s : "");
}

static bool equals_ignore_case(const char *a, const char *b)
{
    for (; *a != '\0' && *b != '\0'; a++, b++) {
        if (tolower((unsigned char) *a) != tolower((unsigned char) *b))
            return false;
    }
    return *a == *b;
}

static const SettingDef *current_def(const UiState *s)
{
    return settings_model_row(s->section, s->row);
}

static bool is_steppable(SettingType type)
{
    switch (type) {
        case SET_BOOL:
        case SET_CHOICE:
        case SET_MENU_REF:
        case SET_INT:
        case SET_PERCENT:
        case SET_LENGTH:
        case SET_DECIMAL:
            return true;
        default:
            return false;
    }
}

// Frees everything a sub-mode owns
static void clear_sub_state(UiState *s)
{
    for (size_t i = 0; i < s->choice_count; i++)
        free(s->choices[i]);
    free(s->choices);
    s->choices = NULL;
    s->choice_count = 0;
    text_input_free(s->text);
    s->text = NULL;
    file_browser_free(s->files);
    s->files = NULL;
    s->editing = NULL;
}

static void leave_to_rows(UiState *s)
{
    clear_sub_state(s);
    s->mode = MODE_ROWS;
    s->list_index = 0;
}

static void show_message(UiState *s, UiMode return_mode, const char *text)
{
    put(s->message, sizeof(s->message), text);
    s->return_mode = return_mode;
    s->close_after_message = false;
    s->mode = MODE_MESSAGE;
}

// Moves list_index by one within [0, count); returns true if the action was a move
static bool move_in_list(UiState *s, UiAction a, size_t count)
{
    if (a == UI_UP || a == UI_LEFT) {
        if (s->list_index > 0)
            s->list_index--;
        return true;
    }
    if (a == UI_DOWN || a == UI_RIGHT) {
        if (s->list_index + 1 < count)
            s->list_index++;
        return true;
    }
    return false;
}

// Starts a save from the current mode; its dialogs return there
static UiResult save(UiState *s)
{
    s->return_mode = s->mode;
    s->risks_accepted = 0;
    return continue_save(s, false);
}

// Asks about each risk not yet accepted, then writes the file
static UiResult continue_save(UiState *s, bool force)
{
    UiMode origin = s->return_mode;
    unsigned risk = 0;
    if (!(s->risks_accepted & RISK_LOCKOUT) && s->config_reachable_at_open
        && !settings_model_config_reachable(s->model))
        risk = RISK_LOCKOUT;
    else if (!(s->risks_accepted & RISK_GAMEPAD) && gamepad_at_risk(s))
        risk = RISK_GAMEPAD;
    if (risk != 0) {
        put(s->message, sizeof(s->message), risk == RISK_LOCKOUT ? lockout_message : gamepad_message);
        s->risk_shown = risk;
        s->list_index = 0;
        s->mode = MODE_CONFIRM_RISK;
        return UI_RESULT_NONE;
    }

    char err[sizeof(s->message)] = "";
    ConfigSaveResult result = settings_model_save(s->model, force, err, sizeof(err));
    switch (result) {
        case CONFIG_SAVE_OK:
            return UI_RESULT_SAVED;
        case CONFIG_SAVE_CHANGED_ON_DISK:
            put(s->message, sizeof(s->message), err);
            s->list_index = 0;
            s->mode = MODE_CONFIRM_OVERWRITE;
            return UI_RESULT_NONE;
        case CONFIG_SAVE_WRITE_FAILED:
            if (offer_copy(s))
                return UI_RESULT_NONE;
            show_message(s, origin, err[0] != '\0' ? err : "Could not save");
            return UI_RESULT_NONE;
        case CONFIG_SAVE_INVALID:
        default:
            show_message(s, origin, err[0] != '\0' ? err : "Could not save");
            return UI_RESULT_NONE;
    }
}

// After a failed write, offers to save to the user's own config instead (Linux and macOS)
static bool offer_copy(UiState *s)
{
    const char *path = settings_model_path(s->model);
    const char *user_path = settings_model_user_config_path(s->model);
    if (path == NULL || user_path == NULL || strcmp(path, user_path) == 0)
        return false;
    snprintf(s->message, sizeof(s->message), "%s is read-only. Save your changes to %s instead?", path, user_path);
    s->list_index = 0;
    s->mode = MODE_CONFIRM_COPY;
    return true;
}

// Driving the screen with a gamepad while saving changes that could stop it working:
// [Gamepad] Enabled ending up false (as the launcher parses it), or any changed binding
static bool gamepad_at_risk(const UiState *s)
{
    if (!s->input_from_gamepad)
        return false;
    if (settings_model_section_dirty(s->model, "Gamepad"))
        return true;
    for (size_t i = 0; i < settings_schema_count; i++) {
        const SettingDef *def = &settings_schema[i];
        if (strcmp(def->id, "SETTING_GAMEPAD_ENABLED") == 0) {
            char value[VALUE_BUF];
            ValueState state = settings_model_display(s->model, def, value, sizeof(value));
            bool enabled = state != VALUE_INVALID && (strcmp(value, "true") == 0 || strcmp(value, "True") == 0);
            return !enabled;
        }
    }
    return false;
}

// Drops any in-progress pick and saves what is already stored
static UiResult leave_and_save(UiState *s)
{
    leave_to_rows(s);
    return save(s);
}

static UiResult apply_sections(UiState *s, UiAction a)
{
    switch (a) {
        case UI_UP:
            if (s->section > 0)
                s->section--;
            break;
        case UI_DOWN:
            if (s->section + 1 < settings_model_section_count())
                s->section++;
            break;
        case UI_RIGHT:
        case UI_SELECT:
            s->mode = MODE_ROWS;
            s->row = 0;
            break;
        case UI_BACK:
            if (settings_model_dirty(s->model)) {
                s->mode = MODE_CONFIRM_EXIT;
                s->list_index = 0;
                return UI_RESULT_NONE;
            }
            return UI_RESULT_CLOSE;
        case UI_SAVE:
            return save(s);
        default:
            break;
    }
    return UI_RESULT_NONE;
}

static UiResult apply_rows(UiState *s, UiAction a)
{
    const SettingDef *def = current_def(s);
    if (def == NULL) {
        s->mode = MODE_SECTIONS;
        return UI_RESULT_NONE;
    }
    char err[sizeof(s->footer)] = "";
    switch (a) {
        case UI_UP:
            if (s->row > 0)
                s->row--;
            break;
        case UI_DOWN:
            if (s->row + 1 < settings_model_row_count(s->section))
                s->row++;
            break;
        case UI_LEFT:
            if (!is_steppable(def->type)) {
                s->mode = MODE_SECTIONS;
                break;
            }
            if (!settings_model_step(s->model, def, -1, err, sizeof(err)))
                put(s->footer, sizeof(s->footer), err);
            break;
        case UI_RIGHT:
            if (!settings_model_step(s->model, def, 1, err, sizeof(err)))
                put(s->footer, sizeof(s->footer), err);
            break;
        case UI_BACK:
            s->mode = MODE_SECTIONS;
            break;
        case UI_RESET:
            settings_model_reset(s->model, def);
            break;
        case UI_SAVE:
            return save(s);
        case UI_SELECT:
            if (def->type == SET_BOOL) {
                if (!settings_model_step(s->model, def, 1, err, sizeof(err)))
                    put(s->footer, sizeof(s->footer), err);
            }
            else {
                open_editor(s, def);
            }
            break;
        default:
            break;
    }
    return UI_RESULT_NONE;
}

static void open_editor(UiState *s, const SettingDef *def)
{
    switch (def->type) {
        case SET_CHOICE:
        case SET_MENU_REF:
            open_choice_list(s, def);
            break;
        case SET_INT:
        case SET_PERCENT:
        case SET_LENGTH:
        case SET_DECIMAL:
            open_text(s, def, LAYOUT_NUMBER);
            break;
        case SET_TEXT:
        case SET_COMMAND:
            open_text(s, def, LAYOUT_TEXT);
            break;
        case SET_COLOR:
            open_color(s, def);
            break;
        case SET_FILE:
        case SET_FONT:
        case SET_DIR:
            open_files(s, def);
            break;
        default:
            break;
    }
}

static void open_choice_list(UiState *s, const SettingDef *def)
{
    char **list = NULL;
    size_t count = settings_model_choices(s->model, def, &list);
    if (count == 0) {
        free(list);
        put(s->footer, sizeof(s->footer), "No choices available");
        return;
    }
    char current[VALUE_BUF];
    settings_model_display(s->model, def, current, sizeof(current));
    s->list_index = 0;
    for (size_t i = 0; i < count; i++) {
        if (equals_ignore_case(list[i], current)) {
            s->list_index = i;
            break;
        }
    }
    s->choices = list;
    s->choice_count = count;
    s->editing = def;
    s->mode = MODE_CHOICE_LIST;
}

static void open_text(UiState *s, const SettingDef *def, TextLayout layout)
{
    char current[VALUE_BUF];
    settings_model_display(s->model, def, current, sizeof(current));
    TextInput *text = text_input_new(current, layout);
    if (text == NULL) {
        put(s->footer, sizeof(s->footer), "Out of memory");
        return;
    }
    s->text = text;
    s->editing = def;
    s->mode = MODE_TEXT;
}

static void open_color(UiState *s, const SettingDef *def)
{
    char current[VALUE_BUF];
    settings_model_display(s->model, def, current, sizeof(current));
    s->list_index = CUSTOM_CELL;
    for (size_t i = 0; i < UI_PALETTE_SIZE; i++) {
        if (equals_ignore_case(ui_palette[i], current)) {
            s->list_index = i;
            break;
        }
    }
    s->editing = def;
    s->mode = MODE_COLOR;
}

// Index of the last path separator in `path`, or NULL
static const char *last_separator(const char *path)
{
    const char *slash = strrchr(path, '/');
#ifdef _WIN32
    const char *backslash = strrchr(path, '\\');
    if (backslash != NULL && (slash == NULL || backslash > slash))
        slash = backslash;
#endif
    return slash;
}

// The folder holding the config file, or false if there is none
static bool config_folder(const UiState *s, char *buf, size_t len)
{
    const char *config_path = settings_model_path(s->model);
    if (config_path == NULL)
        return false;
    // Absolute first, so "config.ini" or "C:config.ini" still has a folder
    char *absolute = absolute_path(config_path);
    const char *path = absolute != NULL ? absolute : config_path;
    const char *slash = last_separator(path);
    bool ok = false;
    if (slash != NULL) {
        size_t folder_len = (size_t) (slash - path);
        if (folder_len == 0)
            folder_len = 1;  // the root
        if (folder_len < len) {
            memcpy(buf, path, folder_len);
            buf[folder_len] = '\0';
            ok = true;
        }
    }
    free(absolute);
    return ok;
}

// Focuses `file` in the browser if the browser is showing the folder it is in
static void focus_file(UiState *s, const char *file)
{
    char *absolute = absolute_path(file);
    if (absolute == NULL)
        return;
    const char *slash = last_separator(absolute);
    if (slash != NULL) {
        const char *dir = file_browser_dir(s->files);
        size_t dir_len = strlen(dir);
        while (dir_len > 0 && (dir[dir_len - 1] == '/' || dir[dir_len - 1] == '\\'))
            dir_len--;
        size_t folder_len = (size_t) (slash - absolute);
        if (folder_len == dir_len && strncmp(absolute, dir, dir_len) == 0) {
            size_t index = file_browser_find(s->files, slash + 1);
            if (index != (size_t) -1)
                s->list_index = index;
        }
    }
    free(absolute);
}

static void open_files(UiState *s, const SettingDef *def)
{
    FileFilter filter = FILTER_IMAGES;
    if (def->type == SET_FONT)
        filter = FILTER_FONTS;
    else if (def->type == SET_DIR)
        filter = FILTER_DIRS;
    else if (strcmp(def->id, "SETTING_GAMEPAD_MAPPINGS_FILE") == 0)
        filter = FILTER_ANY;

    char current[VALUE_BUF], folder[PATH_BUF];
    const char *candidates[3];
    size_t count = 0;
    settings_model_display(s->model, def, current, sizeof(current));
    if (current[0] != '\0')
        candidates[count++] = current;
    if (config_folder(s, folder, sizeof(folder)))
        candidates[count++] = folder;
    const char *home = home_directory();
    if (home != NULL && home[0] != '\0')
        candidates[count++] = home;

    char err[sizeof(s->message)] = "";
    FileBrowser *files = file_browser_open(candidates, count, filter, err, sizeof(err));
    if (files == NULL) {
        show_message(s, MODE_ROWS, err[0] != '\0' ? err : "Could not open a folder to browse");
        return;
    }
    s->files = files;
    s->editing = def;
    s->list_index = 0;
    s->mode = MODE_FILES;
    if (current[0] != '\0')
        focus_file(s, current);
}

static UiResult apply_choice_list(UiState *s, UiAction a)
{
    if (move_in_list(s, a, s->choice_count))
        return UI_RESULT_NONE;
    if (a == UI_SAVE)
        return leave_and_save(s);
    if (a == UI_BACK) {
        leave_to_rows(s);
    }
    else if (a == UI_SELECT && s->list_index < s->choice_count) {
        char err[sizeof(s->footer)] = "";
        char value[VALUE_BUF];
        put(value, sizeof(value), s->choices[s->list_index]);
        bool ok = settings_model_set(s->model, s->editing, value, err, sizeof(err));
        leave_to_rows(s);
        if (!ok)
            put(s->footer, sizeof(s->footer), err);
    }
    return UI_RESULT_NONE;
}

// Stores the text; stays in MODE_TEXT with the error as footer on failure
static UiResult finish_text(UiState *s)
{
    char err[sizeof(s->footer)] = "";
    char value[VALUE_BUF];
    put(value, sizeof(value), text_input_value(s->text));
    if (!settings_model_set(s->model, s->editing, value, err, sizeof(err))) {
        put(s->footer, sizeof(s->footer), err[0] != '\0' ? err : "Invalid value");
        return UI_RESULT_NONE;
    }
    leave_to_rows(s);
    return UI_RESULT_NONE;
}

static UiResult apply_text(UiState *s, UiAction a)
{
    switch (a) {
        case UI_UP:
            text_input_move_focus(s->text, 0, -1);
            break;
        case UI_DOWN:
            text_input_move_focus(s->text, 0, 1);
            break;
        case UI_LEFT:
            text_input_move_focus(s->text, -1, 0);
            break;
        case UI_RIGHT:
            text_input_move_focus(s->text, 1, 0);
            break;
        case UI_ERASE:
            text_input_erase(s->text);
            break;
        case UI_CURSOR_LEFT:
            text_input_move_cursor(s->text, -1);
            break;
        case UI_CURSOR_RIGHT:
            text_input_move_cursor(s->text, 1);
            break;
        case UI_BACK:
            leave_to_rows(s);
            break;
        case UI_ACCEPT:
            return finish_text(s);
        case UI_SAVE:
            finish_text(s);
            if (s->mode == MODE_ROWS)
                return save(s);
            break;
        case UI_SELECT: {
            TextResult result = text_input_press(s->text);
            if (result == TEXT_DONE)
                return finish_text(s);
            if (result == TEXT_CANCEL)
                leave_to_rows(s);
            break;
        }
        default:
            break;
    }
    return UI_RESULT_NONE;
}

static UiResult apply_color(UiState *s, UiAction a)
{
    size_t i = s->list_index;
    switch (a) {
        case UI_LEFT:
            if (i > 0)
                s->list_index = i - 1;
            break;
        case UI_RIGHT:
            if (i < CUSTOM_CELL)
                s->list_index = i + 1;
            break;
        case UI_UP:
            if (i == CUSTOM_CELL)
                s->list_index = UI_PALETTE_SIZE - 4;
            else if (i >= 4)
                s->list_index = i - 4;
            break;
        case UI_DOWN:
            if (i < UI_PALETTE_SIZE - 4)
                s->list_index = i + 4;
            else
                s->list_index = CUSTOM_CELL;
            break;
        case UI_BACK:
            leave_to_rows(s);
            break;
        case UI_SAVE:
            return leave_and_save(s);
        case UI_SELECT:
            if (i < UI_PALETTE_SIZE) {
                char err[sizeof(s->footer)] = "";
                bool ok = settings_model_set(s->model, s->editing, ui_palette[i], err, sizeof(err));
                leave_to_rows(s);
                if (!ok)
                    put(s->footer, sizeof(s->footer), err);
            }
            else {
                const SettingDef *def = s->editing;
                open_text(s, def, LAYOUT_TEXT);
            }
            break;
        default:
            break;
    }
    return UI_RESULT_NONE;
}

static UiResult apply_files(UiState *s, UiAction a)
{
    switch (a) {
        case UI_UP:
        case UI_DOWN: {
            size_t count = file_browser_count(s->files);
            if (a == UI_UP && s->list_index > 0)
                s->list_index--;
            else if (a == UI_DOWN && s->list_index + 1 < count)
                s->list_index++;
            break;
        }
        case UI_BACK:
            leave_to_rows(s);
            break;
        case UI_SAVE:
            return leave_and_save(s);
        case UI_SELECT: {
            char path[PATH_BUF], err[sizeof(s->footer)] = "";
            FileBrowserResult result = file_browser_activate(s->files, s->list_index, path, sizeof(path), err, sizeof(err));
            if (result == FB_NAVIGATED) {
                s->list_index = 0;
            }
            else if (result == FB_CHOSEN) {
                if (settings_model_set(s->model, s->editing, path, err, sizeof(err)))
                    leave_to_rows(s);
                else
                    put(s->footer, sizeof(s->footer), err);
            }
            else {
                put(s->footer, sizeof(s->footer), err[0] != '\0' ? err : "Could not open that");
            }
            break;
        }
        default:
            break;
    }
    return UI_RESULT_NONE;
}

static UiResult apply_confirm_exit(UiState *s, UiAction a)
{
    if (move_in_list(s, a, 3))
        return UI_RESULT_NONE;
    if (a == UI_SAVE)
        return save(s);
    if (a == UI_BACK) {
        s->mode = MODE_SECTIONS;
    }
    else if (a == UI_SELECT) {
        if (s->list_index == 0)
            return save(s);
        if (s->list_index == 1)
            return UI_RESULT_CLOSE;
        s->mode = MODE_SECTIONS;
    }
    return UI_RESULT_NONE;
}

static UiResult apply_confirm_overwrite(UiState *s, UiAction a)
{
    if (move_in_list(s, a, 2))
        return UI_RESULT_NONE;
    if (a == UI_BACK || (a == UI_SELECT && s->list_index == 1)) {
        s->mode = s->return_mode;
        s->list_index = 0;
    }
    else if (a == UI_SELECT) {
        return continue_save(s, true);
    }
    return UI_RESULT_NONE;
}

static UiResult apply_confirm_copy(UiState *s, UiAction a)
{
    if (move_in_list(s, a, 2))
        return UI_RESULT_NONE;
    if (a == UI_BACK || (a == UI_SELECT && s->list_index == 1)) {
        s->mode = s->return_mode;
        s->list_index = 0;
    }
    else if (a == UI_SELECT) {
        char user_path[PATH_BUF];
        char err[sizeof(s->message)] = "";
        put(user_path, sizeof(user_path), settings_model_user_config_path(s->model));
        if (settings_model_save_as(s->model, user_path, err, sizeof(err)) == CONFIG_SAVE_OK)
            return UI_RESULT_SAVED;
        show_message(s, s->return_mode, err[0] != '\0' ? err : "Could not save");
    }
    return UI_RESULT_NONE;
}

static UiResult apply_confirm_risk(UiState *s, UiAction a)
{
    if (move_in_list(s, a, 2))
        return UI_RESULT_NONE;
    if (a == UI_BACK || (a == UI_SELECT && s->list_index == 0)) {
        s->mode = s->return_mode;
        s->list_index = 0;
    }
    else if (a == UI_SELECT) {
        s->risks_accepted |= s->risk_shown;
        return continue_save(s, false);
    }
    return UI_RESULT_NONE;
}

static UiResult apply_message(UiState *s, UiAction a)
{
    if (a != UI_SELECT && a != UI_BACK)
        return UI_RESULT_NONE;
    if (s->close_after_message)
        return UI_RESULT_CLOSE;
    s->mode = s->return_mode;
    s->list_index = 0;
    return UI_RESULT_NONE;
}
