#include <stdlib.h>
#include "test.h"
#include "settings_schema.h"
#include "config_io.h"
#include "config_ui/settings_model.h"

static const char *sample =
    "[General]\nDefaultMenu=Main\nVSync=true\n#FPSLimit=\nOnLaunch=Blank\n"
    "[Layout]\nIconSize=256\nIconSpacing=5%\nVCenter=50%\n"
    "[Background]\nSlideshowTransitionTime=3\n"
    "[Clock]\nOpacity=lots\n"
    "[Main]\nEntry1=A;a.png;cmd\n[System]\nEntry1=S;s.png;:sleep\n";

static SettingsModel *open_sample(void)
{
    return settings_model_open_text(sample);
}

static const SettingDef *find(const char *section, const char *key)
{
    const SettingDef *def = schema_find(section, key);
    CHECK(def != NULL);
    return def;
}

// Display string of a setting, ignoring the state
static const char *shown(const SettingsModel *m, const SettingDef *def)
{
    static char buf[256];
    settings_model_display(m, def, buf, sizeof(buf));
    return buf;
}

// Step once and return the resulting display string ("<fail>" if the step failed)
static const char *stepped(SettingsModel *m, const SettingDef *def, int direction)
{
    char err[256] = "";
    if (!settings_model_step(m, def, direction, err, sizeof(err)))
        return "<fail>";
    return shown(m, def);
}

static void test_sections_and_rows(void)
{
    CHECK(settings_model_section_count() == 9);
    for (size_t i = 0; i < 9; i++)
        CHECK_STR(settings_model_section_name(i), schema_sections[i]);
    CHECK_STR(settings_model_section_name(6), "Clock");
    CHECK(settings_model_row_count(6) == 13);
    CHECK_STR(settings_model_row(0, 0)->key, "DefaultMenu");
}

static void test_display_states(void)
{
    SettingsModel *m = open_sample();
    char buf[256];
    CHECK(settings_model_display(m, find("General", "VSync"), buf, sizeof(buf)) == VALUE_SET);
    CHECK_STR(buf, "true");
    CHECK(settings_model_display(m, find("General", "FPSLimit"), buf, sizeof(buf)) == VALUE_NOT_SET);
    CHECK_STR(buf, "");
    CHECK(settings_model_display(m, find("Clock", "ShowDate"), buf, sizeof(buf)) == VALUE_DEFAULT);
    CHECK_STR(buf, "false");
    settings_model_close(m);
}

static void test_step_bool_choice_wraps(void)
{
    SettingsModel *m = open_sample();
    const SettingDef *vsync = find("General", "VSync");
    CHECK_STR(stepped(m, vsync, 1), "false");
    const SettingDef *launch = find("General", "OnLaunch");
    CHECK_STR(stepped(m, launch, 1), "None");
    CHECK_STR(stepped(m, launch, 1), "Quit");
    CHECK_STR(stepped(m, launch, 1), "Blank");
    CHECK_STR(stepped(m, launch, -1), "Quit");
    settings_model_close(m);
}

static void test_step_numbers(void)
{
    SettingsModel *m = open_sample();
    const SettingDef *size = find("Layout", "IconSize");
    CHECK_STR(stepped(m, size, 1), "266");
    char err[256];
    CHECK(settings_model_set(m, size, "1024", err, sizeof(err)));
    CHECK_STR(stepped(m, size, 1), "1024");

    CHECK_STR(stepped(m, find("General", "FPSLimit"), 1), "11");

    const SettingDef *vcenter = find("Layout", "VCenter");
    CHECK_STR(stepped(m, vcenter, -1), "45%");
    CHECK(settings_model_set(m, vcenter, "25%", err, sizeof(err)));
    CHECK_STR(stepped(m, vcenter, -1), "25%");

    const SettingDef *spacing = find("Layout", "IconSpacing");
    CHECK_STR(stepped(m, spacing, 1), "10%");
    CHECK(settings_model_set(m, spacing, "40", err, sizeof(err)));
    CHECK_STR(stepped(m, spacing, 1), "45");

    CHECK_STR(stepped(m, find("Background", "SlideshowTransitionTime"), -1), "2.5");
    settings_model_close(m);
}

static void test_step_menu_ref(void)
{
    SettingsModel *m = open_sample();
    const SettingDef *menu = find("General", "DefaultMenu");
    char **choices = NULL;
    size_t count = settings_model_choices(m, menu, &choices);
    CHECK(count == 2);
    if (count == 2) {
        CHECK_STR(choices[0], "Main");
        CHECK_STR(choices[1], "System");
    }
    for (size_t i = 0; i < count; i++)
        free(choices[i]);
    free(choices);
    CHECK_STR(stepped(m, menu, 1), "System");
    CHECK_STR(stepped(m, menu, 1), "Main");
    settings_model_close(m);
}

static void test_invalid_existing_value(void)
{
    SettingsModel *m = open_sample();
    const SettingDef *opacity = find("Clock", "Opacity");
    char buf[256];
    CHECK(settings_model_display(m, opacity, buf, sizeof(buf)) == VALUE_INVALID);
    CHECK_STR(buf, "lots");
    CHECK_STR(stepped(m, opacity, 1), "100%");
    settings_model_reset(m, opacity);
    CHECK(settings_model_display(m, opacity, buf, sizeof(buf)) == VALUE_DEFAULT);
    settings_model_close(m);
}

static void test_set_validates(void)
{
    SettingsModel *m = open_sample();
    char err[256] = "";
    const SettingDef *size = find("Layout", "IconSize");
    CHECK(!settings_model_set(m, size, "5000", err, sizeof(err)));
    CHECK(err[0] != '\0');
    CHECK_STR(shown(m, size), "256");
    CHECK(!settings_model_dirty(m));

    err[0] = '\0';
    CHECK(!settings_model_set(m, find("General", "StartupCmd"), "a ; b", err, sizeof(err)));
    CHECK(strstr(err, "can't be stored") != NULL);

    CHECK(!settings_model_set(m, find("General", "DefaultMenu"), "Layout", err, sizeof(err)));
    CHECK(settings_model_set(m, find("Background", "Color"), "#112233", err, sizeof(err)));
    CHECK(settings_model_dirty(m));
    settings_model_close(m);
}

static void test_step_unsupported_type(void)
{
    SettingsModel *m = open_sample();
    char err[256] = "";
    CHECK(!settings_model_step(m, find("Background", "Color"), 1, err, sizeof(err)));
    CHECK_STR(err, "Select this setting to edit it");
    settings_model_close(m);
}

static void test_save_round_trip(void)
{
    char path[512];
    test_tmp_path(path, sizeof(path), "settings_model.ini");
    test_write_file(path, sample);
    char err[256] = "";
    SettingsModel *m = settings_model_open(path, err, sizeof(err));
    CHECK(m != NULL);
    if (m == NULL)
        return;
    CHECK(settings_model_set(m, find("General", "VSync"), "false", err, sizeof(err)));
    CHECK(settings_model_dirty(m));
    CHECK(settings_model_save(m, false, err, sizeof(err)) == CONFIG_SAVE_OK);
    char *text = test_read_file(path);
    CHECK(text != NULL && strstr(text, "VSync=false") != NULL);
    free(text);
    CHECK(!settings_model_dirty(m));
    settings_model_close(m);

    SettingsModel *text_only = open_sample();
    CHECK(settings_model_path(text_only) == NULL);
    CHECK(settings_model_save(text_only, false, err, sizeof(err)) == CONFIG_SAVE_WRITE_FAILED);
    CHECK_STR(err, "No config file to save to");
    settings_model_close(text_only);
}

static void test_config_reachable(void)
{
    // Directly on the home menu
    SettingsModel *m = settings_model_open_text(
        "[General]\nDefaultMenu=Main\n[Main]\nEntry1=Settings;s.svg;:config\n[System]\nEntry1=S;s.png;:sleep\n");
    CHECK(settings_model_config_reachable(m));
    settings_model_close(m);

    // Through a submenu, and not from a menu that can't reach it
    m = settings_model_open_text(
        "[General]\nDefaultMenu=Main\n[Main]\nEntry1=Sys;s.png;:submenu System\n"
        "[System]\nEntry1=Back;b.png;:submenu Main\nEntry2=Settings;s.svg;:config\n[Other]\nEntry1=A;a.png;cmd\n");
    CHECK(settings_model_config_reachable(m));
    char err[256] = "";
    CHECK(settings_model_set(m, find("General", "DefaultMenu"), "Other", err, sizeof(err)));
    CHECK(!settings_model_config_reachable(m));
    settings_model_close(m);

    // Not an exact :config command, or a menu nobody opens
    m = settings_model_open_text(
        "[General]\nDefaultMenu=Main\n[Main]\nEntry1=A;a.png;:configure\n[Hidden]\nEntry1=S;s.svg;:config\n");
    CHECK(!settings_model_config_reachable(m));
    settings_model_close(m);

    // A hotkey or a gamepad binding
    m = settings_model_open_text(
        "[General]\nDefaultMenu=Main\n[Hotkeys]\nHotkey1=#73;:config\n[Main]\nEntry1=A;a.png;cmd\n");
    CHECK(settings_model_config_reachable(m));
    settings_model_close(m);
    m = settings_model_open_text(
        "[General]\nDefaultMenu=Main\n[Gamepad]\nButtonBack=:config\n[Main]\nEntry1=A;a.png;cmd\n");
    CHECK(settings_model_config_reachable(m));
    settings_model_close(m);
}

static void test_save_as(void)
{
    char path[512], copy[600];
    test_tmp_path(path, sizeof(path), "settings_model_src.ini");
    test_tmp_path(copy, sizeof(copy), "save_as_dir/nested/config.ini");
    remove(copy);
    test_write_file(path, sample);
    char err[256] = "";
    SettingsModel *m = settings_model_open(path, err, sizeof(err));
    CHECK(m != NULL);
    if (m == NULL)
        return;
    // An invalid change is refused before anything is written
    CHECK(settings_model_set(m, find("General", "DefaultMenu"), "System", err, sizeof(err)));
    settings_model_reset(m, find("General", "DefaultMenu"));
    CHECK(settings_model_save_as(m, copy, err, sizeof(err)) == CONFIG_SAVE_INVALID);
    CHECK(test_read_file(copy) == NULL);
    CHECK(settings_model_set(m, find("General", "DefaultMenu"), "Main", err, sizeof(err)));
    CHECK(settings_model_set(m, find("General", "VSync"), "false", err, sizeof(err)));
    CHECK(settings_model_save_as(m, copy, err, sizeof(err)) == CONFIG_SAVE_OK);
    CHECK_STR(settings_model_path(m), copy);
    CHECK(!settings_model_dirty(m));
    char *text = test_read_file(copy);
    CHECK(text != NULL && strstr(text, "VSync=false") != NULL && strstr(text, "[System]") != NULL);
    free(text);
    text = test_read_file(path);
    CHECK(text != NULL && strstr(text, "VSync=true") != NULL);
    free(text);
    CHECK(settings_model_writable(m));
    settings_model_set_user_config_path(m, "/x/config.ini");
    CHECK_STR(settings_model_user_config_path(m), "/x/config.ini");
    settings_model_close(m);
}

void suite_settings_model(void)
{
    test_sections_and_rows();
    test_display_states();
    test_step_bool_choice_wraps();
    test_step_numbers();
    test_step_menu_ref();
    test_invalid_existing_value();
    test_set_validates();
    test_step_unsupported_type();
    test_save_round_trip();
    test_config_reachable();
    test_save_as();
}
