#include <stdlib.h>
#include <string.h>
#include "test.h"
#include "settings_schema.h"
#include "config_io.h"
#include "config_ui/ui_controller.h"
#ifdef PLATFORM_POSIX
#include <sys/stat.h>
#include <unistd.h>
#endif

static const char *sample =
    "[General]\nDefaultMenu=Main\nVSync=true\n#FPSLimit=\nOnLaunch=Blank\n"
    "[Layout]\nIconSize=256\nIconSpacing=5%\nVCenter=50%\n"
    "[Background]\nSlideshowTransitionTime=3\n"
    "[Clock]\nOpacity=lots\n"
    "[Main]\nEntry1=A;a.png;cmd\n[System]\nEntry1=S;s.png;:sleep\n";

static const SettingDef *find(const char *section, const char *key)
{
    const SettingDef *def = schema_find(section, key);
    CHECK(def != NULL);
    return def;
}

static size_t section_index(const char *name)
{
    for (size_t i = 0; i < settings_model_section_count(); i++) {
        if (strcmp(settings_model_section_name(i), name) == 0)
            return i;
    }
    CHECK(0);
    return 0;
}

// Focuses the setting in ROWS mode (from any mode, via a fresh state)
static void focus(UiState *s, const char *section, const char *key)
{
    const SettingDef *def = find(section, key);
    s->mode = MODE_ROWS;
    s->section = section_index(section);
    for (size_t r = 0; r < settings_model_row_count(s->section); r++) {
        if (settings_model_row(s->section, r) == def)
            s->row = r;
    }
}

static const char *shown(UiState *s, const char *section, const char *key)
{
    static char buf[512];
    settings_model_display(s->model, find(section, key), buf, sizeof(buf));
    return buf;
}

static void open_sample(UiState *s)
{
    ui_init(s, settings_model_open_text(sample));
}

static void test_section_navigation(void)
{
    UiState s;
    open_sample(&s);
    CHECK(s.mode == MODE_SECTIONS);
    for (int i = 0; i < 20; i++)
        CHECK(ui_apply(&s, UI_DOWN) == UI_RESULT_NONE);
    CHECK(s.section == 8);
    CHECK(ui_apply(&s, UI_RIGHT) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_ROWS);
    CHECK(s.row == 0);
    ui_apply(&s, UI_BACK);
    CHECK(s.mode == MODE_SECTIONS);
    ui_free(&s);
}

static void test_back_clean_closes(void)
{
    UiState s;
    open_sample(&s);
    CHECK(ui_apply(&s, UI_BACK) == UI_RESULT_CLOSE);
    ui_free(&s);
}

static void test_back_with_changes_asks(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    CHECK(ui_apply(&s, UI_BACK) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_SECTIONS);
    CHECK(ui_apply(&s, UI_BACK) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_CONFIRM_EXIT);
    size_t count = 0;
    const char *const *buttons = ui_dialog_buttons(&s, &count);
    CHECK(count == 3);
    if (count == 3) {
        CHECK_STR(buttons[0], "Save");
        CHECK_STR(buttons[1], "Discard");
        CHECK_STR(buttons[2], "Keep editing");
    }
    CHECK(ui_apply(&s, UI_BACK) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_SECTIONS);
    ui_apply(&s, UI_BACK);
    CHECK(s.mode == MODE_CONFIRM_EXIT);
    ui_apply(&s, UI_RIGHT);
    CHECK(ui_apply(&s, UI_SELECT) == UI_RESULT_CLOSE);
    ui_free(&s);
}

static void test_bool_select_toggles(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    CHECK_STR(shown(&s, "General", "VSync"), "false");
    CHECK(s.mode == MODE_ROWS);
    ui_free(&s);
}

static void test_left_right_step(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "Layout", "IconSize");
    ui_apply(&s, UI_RIGHT);
    CHECK_STR(shown(&s, "Layout", "IconSize"), "266");
    CHECK(s.mode == MODE_ROWS);
    focus(&s, "Background", "Color");
    ui_apply(&s, UI_RIGHT);
    CHECK(s.mode == MODE_ROWS);
    CHECK(s.footer[0] != '\0');
    ui_apply(&s, UI_LEFT);
    CHECK(s.mode == MODE_SECTIONS);
    ui_free(&s);
}

static void test_choice_list(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "General", "OnLaunch");
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_CHOICE_LIST);
    CHECK(s.list_index == 0);
    CHECK(s.choice_count > 1);
    if (s.choice_count > 0)
        CHECK_STR(s.choices[0], "Blank");
    ui_apply(&s, UI_DOWN);
    ui_apply(&s, UI_SELECT);
    CHECK_STR(shown(&s, "General", "OnLaunch"), "None");
    CHECK(s.mode == MODE_ROWS);
    // Back leaves the value alone
    ui_apply(&s, UI_SELECT);
    ui_apply(&s, UI_DOWN);
    ui_apply(&s, UI_BACK);
    CHECK(s.mode == MODE_ROWS);
    CHECK_STR(shown(&s, "General", "OnLaunch"), "None");
    ui_free(&s);
}

static void test_text_edit_done_and_invalid(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "Layout", "IconSize");
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_TEXT);
    CHECK(s.text != NULL);
    if (s.text == NULL) {
        ui_free(&s);
        return;
    }
    CHECK_STR(text_input_value(s.text), "256");
    for (int i = 0; i < 3; i++)
        ui_apply(&s, UI_ERASE);
    ui_type(&s, "5000");
    CHECK(ui_apply(&s, UI_ACCEPT) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_TEXT);
    CHECK(s.footer[0] != '\0');
    CHECK_STR(shown(&s, "Layout", "IconSize"), "256");
    for (int i = 0; i < 4; i++)
        ui_apply(&s, UI_ERASE);
    ui_type(&s, "128");
    ui_apply(&s, UI_ACCEPT);
    CHECK(s.mode == MODE_ROWS);
    CHECK_STR(shown(&s, "Layout", "IconSize"), "128");
    ui_free(&s);
}

static void test_text_cancel_and_keys(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "Layout", "IconSize");
    ui_apply(&s, UI_SELECT);
    ui_apply(&s, UI_RIGHT);
    ui_apply(&s, UI_DOWN);
    ui_apply(&s, UI_LEFT);
    ui_apply(&s, UI_UP);
    ui_apply(&s, UI_BACK);
    CHECK(s.mode == MODE_ROWS);
    CHECK(s.text == NULL);
    CHECK_STR(shown(&s, "Layout", "IconSize"), "256");
    ui_free(&s);
}

static void test_color_picker(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "Background", "Color");
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_COLOR);
    CHECK(s.list_index == 3);
    CHECK_STR(ui_palette[3], "#000000");
    ui_apply(&s, UI_RIGHT);
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_ROWS);
    CHECK_STR(shown(&s, "Background", "Color"), "#FF3B30");
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_COLOR);
    CHECK(s.list_index == 4);
    for (int i = 0; i < 4; i++)
        ui_apply(&s, UI_DOWN);
    CHECK(s.list_index == 16);
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_TEXT);
    if (s.text != NULL) {
        CHECK_STR(text_input_value(s.text), "#FF3B30");
        for (int i = 0; i < 7; i++)
            ui_apply(&s, UI_ERASE);
        ui_type(&s, "#102030");
        ui_apply(&s, UI_ACCEPT);
        CHECK(s.mode == MODE_ROWS);
        CHECK_STR(shown(&s, "Background", "Color"), "#102030");
    }
    // A color outside the palette opens on "Custom hex..."
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_COLOR);
    CHECK(s.list_index == 16);
    ui_apply(&s, UI_UP);
    CHECK(s.list_index == 12);
    ui_apply(&s, UI_BACK);
    CHECK(s.mode == MODE_ROWS);
    ui_free(&s);
}

static void test_reset(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    ui_apply(&s, UI_RESET);
    char buf[64];
    CHECK(settings_model_display(s.model, find("General", "VSync"), buf, sizeof(buf)) == VALUE_DEFAULT);
    ui_free(&s);
}

static void test_files_mode(void)
{
    UiState s;
    open_sample(&s);
    focus(&s, "Background", "Image");
    ui_apply(&s, UI_SELECT);
    // No config path, but the home folder (or a message) must always be handled
    CHECK(s.mode == MODE_FILES || s.mode == MODE_MESSAGE);
    if (s.mode == MODE_FILES) {
        CHECK(s.files != NULL);
        ui_apply(&s, UI_UP);
        CHECK(s.list_index == 0);
        ui_apply(&s, UI_BACK);
        CHECK(s.mode == MODE_ROWS);
        CHECK(s.files == NULL);
    }
    else {
        CHECK(s.message[0] != '\0');
        ui_apply(&s, UI_SELECT);
        CHECK(s.mode == MODE_ROWS);
    }
    ui_free(&s);
}

static void open_file_model(UiState *s, const char *name, const char *text)
{
    char path[512], err[256] = "";
    test_tmp_path(path, sizeof(path), name);
    test_write_file(path, text);
    SettingsModel *m = settings_model_open(path, err, sizeof(err));
    CHECK(m != NULL);
    ui_init(s, m);
}

static void test_save_success(void)
{
    UiState s;
    open_file_model(&s, "ui_save.ini", sample);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    CHECK(ui_apply(&s, UI_SAVE) == UI_RESULT_SAVED);
    char path[512];
    test_tmp_path(path, sizeof(path), "ui_save.ini");
    char *text = test_read_file(path);
    CHECK(text != NULL && strstr(text, "VSync=false") != NULL);
    free(text);
    ui_free(&s);
}

static void test_save_from_exit_dialog(void)
{
    UiState s;
    open_file_model(&s, "ui_exit_save.ini", sample);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    ui_apply(&s, UI_BACK);
    ui_apply(&s, UI_BACK);
    CHECK(s.mode == MODE_CONFIRM_EXIT);
    CHECK(ui_apply(&s, UI_SELECT) == UI_RESULT_SAVED);
    ui_free(&s);
}

static void test_save_write_failure_keeps_screen(void)
{
    // A model with no file behind it fails the same way a read-only file does
    UiState s;
    open_sample(&s);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    CHECK(ui_apply(&s, UI_SAVE) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_MESSAGE);
    CHECK(s.message[0] != '\0');
    size_t count = 0;
    const char *const *buttons = ui_dialog_buttons(&s, &count);
    CHECK(count == 1 && buttons != NULL && strcmp(buttons[0], "OK") == 0);
    CHECK(ui_apply(&s, UI_SELECT) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_ROWS);
    CHECK(settings_model_dirty(s.model));
    ui_free(&s);

#ifdef PLATFORM_POSIX
    // A real file in a directory that was made read-only after opening
    char dir[512], path[600], err[256] = "";
    test_tmp_path(dir, sizeof(dir), "ui_readonly_dir");
    mkdir(dir, 0700);
    chmod(dir, 0700);
    snprintf(path, sizeof(path), "%s/ui_readonly.ini", dir);
    test_write_file(path, sample);
    SettingsModel *m = settings_model_open(path, err, sizeof(err));
    CHECK(m != NULL);
    ui_init(&s, m);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    chmod(dir, 0500);
    char probe_path[640];
    snprintf(probe_path, sizeof(probe_path), "%s/probe", dir);
    FILE *probe = fopen(probe_path, "w");
    if (probe != NULL) {
        // Running as a user that ignores permissions (root): the failure can't be provoked
        fclose(probe);
        remove(probe_path);
    }
    else {
        CHECK(ui_apply(&s, UI_SAVE) == UI_RESULT_NONE);
        CHECK(s.mode == MODE_MESSAGE);
        CHECK(strstr(s.message, "Could not write") != NULL);
        ui_apply(&s, UI_SELECT);
        CHECK(s.mode == MODE_ROWS);
        CHECK(settings_model_dirty(s.model));
    }
    chmod(dir, 0700);
    remove(path);
    rmdir(dir);
    ui_free(&s);
#endif
}

static void test_save_too_long_value(void)
{
    UiState s;
    open_file_model(&s, "ui_long.ini", sample);
    focus(&s, "General", "StartupCmd");
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_TEXT);
    char longtext[251];
    memset(longtext, 'x', 250);
    longtext[250] = '\0';
    ui_type(&s, longtext);
    ui_apply(&s, UI_ACCEPT);
    CHECK(s.mode == MODE_ROWS);
    CHECK(ui_apply(&s, UI_SAVE) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_MESSAGE);
    CHECK(strstr(s.message, "too long") != NULL);
    ui_free(&s);
}

static void test_overwrite_flow(void)
{
    UiState s;
    open_file_model(&s, "ui_overwrite.ini", sample);
    focus(&s, "General", "VSync");
    ui_apply(&s, UI_SELECT);
    char path[512];
    test_tmp_path(path, sizeof(path), "ui_overwrite.ini");
    test_write_file(path, "[General]\nVSync=true\nWrapEntries=true\n[Main]\nEntry1=A;a.png;cmd\n");
    CHECK(ui_apply(&s, UI_SAVE) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_CONFIRM_OVERWRITE);
    size_t count = 0;
    const char *const *buttons = ui_dialog_buttons(&s, &count);
    CHECK(count == 2);
    if (count == 2) {
        CHECK_STR(buttons[0], "Overwrite");
        CHECK_STR(buttons[1], "Cancel");
    }
    // Cancel returns to where we were
    ui_apply(&s, UI_RIGHT);
    CHECK(s.list_index == 1);
    ui_apply(&s, UI_SELECT);
    CHECK(s.mode == MODE_ROWS);
    CHECK(ui_apply(&s, UI_SAVE) == UI_RESULT_NONE);
    CHECK(s.mode == MODE_CONFIRM_OVERWRITE);
    CHECK(s.list_index == 0);
    CHECK(ui_apply(&s, UI_SELECT) == UI_RESULT_SAVED);
    ui_free(&s);
}

static void test_error_screen_closes(void)
{
    UiState s;
    ui_init_error(&s, "x");
    CHECK(s.mode == MODE_MESSAGE);
    CHECK_STR(s.message, "x");
    CHECK(ui_apply(&s, UI_SELECT) == UI_RESULT_CLOSE);
    ui_free(&s);
}

void suite_ui_controller(void)
{
    test_section_navigation();
    test_back_clean_closes();
    test_back_with_changes_asks();
    test_bool_select_toggles();
    test_left_right_step();
    test_choice_list();
    test_text_edit_done_and_invalid();
    test_text_cancel_and_keys();
    test_color_picker();
    test_reset();
    test_files_mode();
    test_save_success();
    test_save_from_exit_dialog();
    test_save_write_failure_keeps_screen();
    test_save_too_long_value();
    test_overwrite_flow();
    test_error_screen_closes();
}
