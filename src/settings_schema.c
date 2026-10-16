#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <stdarg.h>
#include <ctype.h>
#include <errno.h>
#include <SDL.h>
#include "launcher.h"
#include <launcher_config.h>
#include "util.h"
#include "settings_schema.h"

static bool fail(char *err, size_t err_len, const char *format, ...);
static bool parse_int_strict(const char *s, long *out);
static bool percent_in_range(const char *value, int min, int max);

#define S(section, id, type, min, max, choices, def, label, help) \
    { #id, section, id, label, help, type, min, max, choices, def }
#define CHOICES(mode) ((const char *const *) mode_settings[mode])

const char *const schema_sections[] = {
    "General", "Background", "Layout", "Titles", "Highlight",
    "Scroll Indicators", "Clock", "Screensaver", "Gamepad", NULL
};

const SettingDef settings_schema[] = {
    // General
    S("General", SETTING_DEFAULT_MENU, SET_MENU_REF, 0, 0, NULL, "Main", "Home menu", "Menu shown at startup and by :home"),
    S("General", SETTING_VSYNC, SET_BOOL, 0, 0, NULL, "true", "VSync", "Sync drawing to the display refresh rate"),
    S("General", SETTING_FPS_LIMIT, SET_INT, MIN_FPS_LIMIT + 1, 1000, NULL, NULL, "FPS limit", "Frame rate cap when VSync is off"),
    S("General", SETTING_APPLICATION_TIMEOUT, SET_INT, MIN_APPLICATION_TIMEOUT, MAX_APPLICATION_TIMEOUT, NULL, "15", "Launch timeout (s)", "How long to wait for a launched app to take focus"),
    S("General", SETTING_ON_LAUNCH, SET_CHOICE, 0, 0, CHOICES(MODE_SETTING_ON_LAUNCH), "Blank", "When an app launches", "Blank the screen, do nothing, or quit the launcher"),
    S("General", SETTING_WRAP_ENTRIES, SET_BOOL, 0, 0, NULL, "false", "Wrap around", "Moving past the last entry returns to the first"),
    S("General", SETTING_RESET_ON_BACK, SET_BOOL, 0, 0, NULL, "false", "Reset on back", "Return to the first entry when going back a menu"),
    S("General", SETTING_MOUSE_SELECT, SET_BOOL, 0, 0, NULL, "false", "Mouse select", "Left click launches the highlighted entry"),
    S("General", SETTING_INHIBIT_OS_SCREENSAVER, SET_BOOL, 0, 0, NULL, "true", "Block OS screensaver", "Stop the system screensaver while the launcher is open"),
    S("General", SETTING_STARTUP_CMD, SET_COMMAND, 0, 0, NULL, NULL, "Startup command", "Runs once when the launcher starts"),
    S("General", SETTING_QUIT_CMD, SET_COMMAND, 0, 0, NULL, NULL, "Quit command", "Runs when the launcher quits"),

    // Background
    S("Background", SETTING_BACKGROUND_MODE, SET_CHOICE, 0, 0, CHOICES(MODE_SETTING_BACKGROUND), "Color", "Background", "Solid color, image, slideshow, or transparent"),
    S("Background", SETTING_BACKGROUND_COLOR, SET_COLOR, 0, 0, NULL, "#000000", "Color", "Background color in Color mode"),
    S("Background", SETTING_BACKGROUND_IMAGE, SET_FILE, 0, 0, NULL, NULL, "Image", "Background image in Image mode"),
    S("Background", SETTING_SLIDESHOW_DIRECTORY, SET_DIR, 0, 0, NULL, NULL, "Slideshow folder", "Folder of images for Slideshow mode"),
    S("Background", SETTING_SLIDESHOW_IMAGE_DURATION, SET_INT, MIN_SLIDESHOW_IMAGE_DURATION / 1000, MAX_SLIDESHOW_IMAGE_DURATION / 1000, NULL, "30", "Seconds per image", "How long each slideshow image shows"),
    S("Background", SETTING_SLIDESHOW_TRANSITION_TIME, SET_DECIMAL, 0, MAX_SLIDESHOW_TRANSITION_TIME / 1000, NULL, "3", "Transition (s)", "Crossfade time between slideshow images"),
    S("Background", SETTING_CHROMA_KEY_COLOR, SET_COLOR, 0, 0, NULL, "#010101", "Chroma key", "Color made see-through in Transparent mode (Windows)"),
    S("Background", SETTING_BACKGROUND_OVERLAY, SET_BOOL, 0, 0, NULL, "false", "Overlay", "Tint the background with a color"),
    S("Background", SETTING_BACKGROUND_OVERLAY_COLOR, SET_COLOR, 0, 0, NULL, "#000000", "Overlay color", "Tint color"),
    S("Background", SETTING_BACKGROUND_OVERLAY_OPACITY, SET_PERCENT, 0, 100, NULL, "50%", "Overlay opacity", "Tint strength"),

    // Layout
    S("Layout", SETTING_MAX_BUTTONS, SET_INT, 1, 50, NULL, "4", "Icons per page", "Maximum entries shown at once"),
    S("Layout", SETTING_ICON_SIZE, SET_INT, MIN_ICON_SIZE, MAX_ICON_SIZE, NULL, "256", "Icon size (px)", "Width and height of each icon"),
    S("Layout", SETTING_ICON_SPACING, SET_LENGTH, 0, 10000, NULL, "5%", "Icon spacing", "Gap between icons, in % of screen width or pixels"),
    S("Layout", SETTING_VCENTER, SET_PERCENT, (int) (MIN_VCENTER * 100), (int) (MAX_VCENTER * 100), NULL, "50%", "Vertical position", "Where the icon row sits, as % of screen height"),

    // Titles
    S("Titles", SETTING_TITLES_ENABLED, SET_BOOL, 0, 0, NULL, "true", "Show titles", "Show a title under each icon"),
    S("Titles", SETTING_TITLE_FONT, SET_FONT, 0, 0, NULL, NULL, "Font", "TTF font for titles"),
    S("Titles", SETTING_TITLE_FONT_SIZE, SET_INT, 8, 300, NULL, "36", "Font size", "Title text size in points"),
    S("Titles", SETTING_TITLE_FONT_COLOR, SET_COLOR, 0, 0, NULL, "#FFFFFF", "Color", "Title text color"),
    S("Titles", SETTING_TITLE_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Opacity", "Title text opacity"),
    S("Titles", SETTING_TITLE_SHADOWS, SET_BOOL, 0, 0, NULL, "false", "Shadows", "Drop shadow behind title text"),
    S("Titles", SETTING_TITLE_SHADOW_COLOR, SET_COLOR, 0, 0, NULL, "#000000", "Shadow color", "Title shadow color"),
    S("Titles", SETTING_TITLE_OVERSIZE_MODE, SET_CHOICE, 0, 0, CHOICES(MODE_SETTING_OVERSIZE), "Shrink", "Long titles", "Truncate, shrink, or leave titles wider than the icon"),
    S("Titles", SETTING_TITLE_PADDING, SET_INT, 0, 500, NULL, "20", "Padding (px)", "Space between icon and title"),

    // Highlight
    S("Highlight", SETTING_HIGHLIGHT_ENABLED, SET_BOOL, 0, 0, NULL, "true", "Show highlight", "Draw a box behind the selected entry"),
    S("Highlight", SETTING_HIGHLIGHT_FILL_COLOR, SET_COLOR, 0, 0, NULL, "#FFFFFF", "Fill color", "Highlight box color"),
    S("Highlight", SETTING_HIGHLIGHT_FILL_OPACITY, SET_PERCENT, 0, 100, NULL, "25%", "Fill opacity", "Highlight box opacity"),
    S("Highlight", SETTING_HIGHLIGHT_OUTLINE_SIZE, SET_INT, 0, 100, NULL, "0", "Outline (px)", "Highlight border width; 0 for none"),
    S("Highlight", SETTING_HIGHLIGHT_OUTLINE_COLOR, SET_COLOR, 0, 0, NULL, "#0000FF", "Outline color", "Highlight border color"),
    S("Highlight", SETTING_HIGHLIGHT_OUTLINE_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Outline opacity", "Highlight border opacity"),
    S("Highlight", SETTING_HIGHLIGHT_CORNER_RADIUS, SET_INT, MIN_RX_SIZE, MAX_RX_SIZE, NULL, "0", "Corner radius", "Rounded corners on the highlight box"),
    S("Highlight", SETTING_HIGHLIGHT_VPADDING, SET_INT, 0, 500, NULL, "30", "Vertical padding (px)", "Space above and below the entry"),
    S("Highlight", SETTING_HIGHLIGHT_HPADDING, SET_INT, 0, 500, NULL, "30", "Horizontal padding (px)", "Space left and right of the entry"),

    // Scroll Indicators
    S("Scroll Indicators", SETTING_SCROLL_INDICATORS, SET_BOOL, 0, 0, NULL, "true", "Show arrows", "Arrows when more entries are off screen"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_FILL_COLOR, SET_COLOR, 0, 0, NULL, "#FFFFFF", "Color", "Arrow color"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_OUTLINE_SIZE, SET_INT, 0, 100, NULL, "0", "Outline (px)", "Arrow border width; 0 for none"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_OUTLINE_COLOR, SET_COLOR, 0, 0, NULL, "#000000", "Outline color", "Arrow border color"),
    S("Scroll Indicators", SETTING_SCROLL_INDICATOR_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Opacity", "Arrow opacity"),

    // Clock
    S("Clock", SETTING_CLOCK_ENABLED, SET_BOOL, 0, 0, NULL, "false", "Show clock", "Show the time in a corner"),
    S("Clock", SETTING_CLOCK_SHOW_DATE, SET_BOOL, 0, 0, NULL, "false", "Show date", "Show the date under the time"),
    S("Clock", SETTING_CLOCK_ALIGNMENT, SET_CHOICE, 0, 0, CHOICES(MODE_SETTING_ALIGNMENT), "Left", "Corner", "Top-left or top-right"),
    S("Clock", SETTING_CLOCK_FONT, SET_FONT, 0, 0, NULL, NULL, "Font", "TTF font for the clock"),
    S("Clock", SETTING_CLOCK_FONT_SIZE, SET_INT, 8, 300, NULL, "50", "Font size", "Clock text size in points"),
    S("Clock", SETTING_CLOCK_FONT_COLOR, SET_COLOR, 0, 0, NULL, "#FFFFFF", "Color", "Clock text color"),
    S("Clock", SETTING_CLOCK_SHADOWS, SET_BOOL, 0, 0, NULL, "false", "Shadows", "Drop shadow behind the clock"),
    S("Clock", SETTING_CLOCK_SHADOW_COLOR, SET_COLOR, 0, 0, NULL, "#000000", "Shadow color", "Clock shadow color"),
    S("Clock", SETTING_CLOCK_MARGIN, SET_LENGTH, 0, 10000, NULL, "5%", "Margin", "Distance from the screen edge, in % or pixels"),
    S("Clock", SETTING_CLOCK_OPACITY, SET_PERCENT, 0, 100, NULL, "100%", "Opacity", "Clock text opacity"),
    S("Clock", SETTING_CLOCK_TIME_FORMAT, SET_CHOICE, 0, 0, CHOICES(MODE_SETTING_TIME_FORMAT), "Auto", "Time format", "24-hour, 12-hour, or from your region"),
    S("Clock", SETTING_CLOCK_DATE_FORMAT, SET_CHOICE, 0, 0, CHOICES(MODE_SETTING_DATE_FORMAT), "Auto", "Date format", "Month first, day first, or from your region"),
    S("Clock", SETTING_CLOCK_INCLUDE_WEEKDAY, SET_BOOL, 0, 0, NULL, "true", "Weekday", "Include the day of the week in the date"),

    // Screensaver
    S("Screensaver", SETTING_SCREENSAVER_ENABLED, SET_BOOL, 0, 0, NULL, "false", "Dim when idle", "Fade the screen after a period of no input"),
    S("Screensaver", SETTING_SCREENSAVER_IDLE_TIME, SET_INT, MIN_SCREENSAVER_IDLE_TIME, MAX_SCREENSAVER_IDLE_TIME, NULL, "300", "Idle time (s)", "Seconds of no input before dimming"),
    S("Screensaver", SETTING_SCREENSAVER_INTENSITY, SET_PERCENT, 1, 100, NULL, "70%", "Dim amount", "How dark the screen gets"),
    S("Screensaver", SETTING_SCREENSAVER_PAUSE_SLIDESHOW, SET_BOOL, 0, 0, NULL, "true", "Pause slideshow", "Stop changing images while dimmed"),

    // Gamepad
    S("Gamepad", SETTING_GAMEPAD_ENABLED, SET_BOOL, 0, 0, NULL, "false", "Gamepad", "Control the launcher with a gamepad"),
    S("Gamepad", SETTING_GAMEPAD_DEVICE, SET_INT, -1, 15, NULL, "-1", "Device", "Which gamepad to use; -1 for any"),
    S("Gamepad", SETTING_GAMEPAD_MAPPINGS_FILE, SET_FILE, 0, 0, NULL, NULL, "Mappings file", "SDL gamecontrollerdb.txt for unrecognized pads"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_XM, SET_COMMAND, 0, 0, NULL, ":left", "Left stick left", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_XP, SET_COMMAND, 0, 0, NULL, ":right", "Left stick right", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_YM, SET_COMMAND, 0, 0, NULL, NULL, "Left stick up", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LSTICK_YP, SET_COMMAND, 0, 0, NULL, NULL, "Left stick down", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_XM, SET_COMMAND, 0, 0, NULL, NULL, "Right stick left", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_XP, SET_COMMAND, 0, 0, NULL, NULL, "Right stick right", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_YM, SET_COMMAND, 0, 0, NULL, NULL, "Right stick up", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RSTICK_YP, SET_COMMAND, 0, 0, NULL, NULL, "Right stick down", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_LTRIGGER, SET_COMMAND, 0, 0, NULL, NULL, "Left trigger", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_RTRIGGER, SET_COMMAND, 0, 0, NULL, NULL, "Right trigger", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_A, SET_COMMAND, 0, 0, NULL, ":select", "A button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_B, SET_COMMAND, 0, 0, NULL, ":back", "B button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_X, SET_COMMAND, 0, 0, NULL, NULL, "X button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_Y, SET_COMMAND, 0, 0, NULL, NULL, "Y button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_BACK, SET_COMMAND, 0, 0, NULL, NULL, "Back button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_GUIDE, SET_COMMAND, 0, 0, NULL, NULL, "Guide button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_START, SET_COMMAND, 0, 0, NULL, NULL, "Start button", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_LEFT_STICK, SET_COMMAND, 0, 0, NULL, NULL, "Left stick press", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_RIGHT_STICK, SET_COMMAND, 0, 0, NULL, NULL, "Right stick press", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_LEFT_SHOULDER, SET_COMMAND, 0, 0, NULL, NULL, "Left shoulder", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_RIGHT_SHOULDER, SET_COMMAND, 0, 0, NULL, NULL, "Right shoulder", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_UP, SET_COMMAND, 0, 0, NULL, NULL, "D-pad up", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_DOWN, SET_COMMAND, 0, 0, NULL, NULL, "D-pad down", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_LEFT, SET_COMMAND, 0, 0, NULL, ":left", "D-pad left", "Command for this input"),
    S("Gamepad", SETTING_GAMEPAD_BUTTON_DPAD_RIGHT, SET_COMMAND, 0, 0, NULL, ":right", "D-pad right", "Command for this input"),
};

const size_t settings_schema_count = sizeof(settings_schema) / sizeof(settings_schema[0]);

static bool fail(char *err, size_t err_len, const char *format, ...)
{
    va_list args;
    va_start(args, format);
    vsnprintf(err, err_len, format, args);
    va_end(args);
    return false;
}

static bool parse_int_strict(const char *s, long *out)
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

static bool percent_in_range(const char *value, int min, int max)
{
    if (!is_percent(value))
        return false;
    char number[PERCENT_MAX_CHARS];
    copy_string(number, value, sizeof(number));
    number[strlen(number) - 1] = '\0';
    long n;
    return parse_int_strict(number, &n) && n >= min && n <= max;
}

const SettingDef *schema_find(const char *section, const char *key)
{
    for (size_t i = 0; i < settings_schema_count; i++) {
        if (strcmp(settings_schema[i].section, section) == 0 && strcmp(settings_schema[i].key, key) == 0)
            return &settings_schema[i];
    }
    return NULL;
}

size_t schema_section_settings(const char *section, const SettingDef **out, size_t max)
{
    size_t count = 0;
    for (size_t i = 0; i < settings_schema_count && count < max; i++) {
        if (strcmp(settings_schema[i].section, section) == 0)
            out[count++] = &settings_schema[i];
    }
    return count;
}

bool schema_validate(const SettingDef *def, const char *value, char *err, size_t err_len)
{
    long n;
    bool b;
    switch (def->type) {
        case SET_BOOL:
            return convert_bool(value, &b) ? true : fail(err, err_len, "%s must be true or false", def->label);

        case SET_CHOICE:
            for (size_t i = 0; def->choices[i] != NULL; i++) {
                if (strcmp(def->choices[i], value) == 0)
                    return true;
            }
            return fail(err, err_len, "%s has an unknown option \"%s\"", def->label, value);

        case SET_INT:
            if (parse_int_strict(value, &n) && n >= def->min && n <= def->max)
                return true;
            return fail(err, err_len, "%s must be a whole number from %d to %d", def->label, def->min, def->max);

        case SET_PERCENT:
            if (percent_in_range(value, def->min, def->max))
                return true;
            return fail(err, err_len, "%s must be a percentage from %d%% to %d%%", def->label, def->min, def->max);

        case SET_LENGTH:
            if (percent_in_range(value, 0, 100) || (parse_int_strict(value, &n) && n >= 0 && n <= def->max))
                return true;
            return fail(err, err_len, "%s must be a percentage or a pixel count", def->label);

        case SET_DECIMAL: {
            char *end;
            errno = 0;
            double d = strtod(value, &end);
            if (*value != '\0' && *end == '\0' && errno == 0 && d >= def->min && d <= def->max)
                return true;
            return fail(err, err_len, "%s must be a number from %d to %d", def->label, def->min, def->max);
        }

        case SET_COLOR: {
            bool hex = value[0] == '#' && strlen(value) == 7;
            for (size_t i = 1; hex && i < 7; i++)
                hex = isxdigit((unsigned char) value[i]) != 0;
            return hex ? true : fail(err, err_len, "%s must be a color like #1A2B3C", def->label);
        }

        case SET_TEXT:
        case SET_FILE:
        case SET_DIR:
        case SET_FONT:
        case SET_COMMAND:
        case SET_MENU_REF:
            return value[0] != '\0' ? true : fail(err, err_len, "%s can't be empty", def->label);
    }
    return fail(err, err_len, "%s has an unknown type", def->label);
}
