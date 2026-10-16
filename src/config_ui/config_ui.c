#include <stdbool.h>
#include <stdlib.h>
#include <SDL.h>
#include "../launcher.h"
#include "../debug.h"
#include "config_ui.h"
#include "settings_model.h"
#include "ui_controller.h"
#include "ui_text.h"
#include "ui_render.h"

#define STICK_INITIAL_DELAY 400
#define STICK_REPEAT_PERIOD 80
#define STICK_NONE -1

extern Config config;
extern Geometry geo;
extern SDL_Renderer *renderer;

static bool screen_open = false;
static UiState ui;
static UiTextCache *text_cache = NULL;
static bool text_input_started = false;
static int stick_direction = STICK_NONE;
static Uint32 stick_next_repeat = 0;

static void apply(UiAction action);
static void sync_text_input(void);
static bool map_key(const SDL_KeyboardEvent *key, UiAction *action);
static bool map_button(Uint8 button, UiAction *action);
static int read_stick(void);

bool config_ui_is_open(void)
{
    return screen_open;
}

void config_ui_open(void)
{
    if (screen_open)
        return;
    text_cache = ui_text_cache_new(renderer, geo.screen_height);
    if (text_cache == NULL) {
        log_error("Could not open the Settings screen: the UI font was not found");
        return;
    }

    char err[512];
    SettingsModel *model = settings_model_open(config.config_path, err, sizeof(err));
    if (model == NULL) {
        log_error("Could not open %s for the Settings screen: %s", config.config_path, err);
        ui_init_error(&ui, err);
    }
    else
        ui_init(&ui, model);

    screen_open = true;
    text_input_started = false;
    stick_direction = STICK_NONE;
    stick_next_repeat = 0;
    log_debug("Opened the Settings screen");
}

void config_ui_close(void)
{
    if (!screen_open)
        return;
    if (text_input_started) {
        SDL_StopTextInput();
        text_input_started = false;
    }
    ui_free(&ui);
    ui_text_cache_free(text_cache);
    text_cache = NULL;
    screen_open = false;
    log_debug("Closed the Settings screen");
}

void config_ui_handle_event(const SDL_Event *e)
{
    if (!screen_open)
        return;
    UiAction action;
    switch (e->type) {
        case SDL_KEYDOWN:
            if (map_key(&e->key, &action)) {
                ui.input_from_gamepad = false;
                apply(action);
            }
            break;

        case SDL_TEXTINPUT:
            if (ui.mode == MODE_TEXT && !(SDL_GetModState() & KMOD_CTRL)) {
                ui.input_from_gamepad = false;
                ui_type(&ui, e->text.text);
            }
            break;

        case SDL_CONTROLLERBUTTONDOWN:
            if (map_button(e->cbutton.button, &action)) {
                ui.input_from_gamepad = true;
                apply(action);
            }
            break;

        // The left stick is read in config_ui_update, which also handles its repeat
        default:
            break;
    }
}

void config_ui_update(Uint32 now)
{
    if (!screen_open)
        return;
    int direction = read_stick();
    if (direction != stick_direction) {
        stick_direction = direction;
        if (direction != STICK_NONE) {
            stick_next_repeat = now + STICK_INITIAL_DELAY;
            ui.input_from_gamepad = true;
            apply((UiAction) direction);
        }
    }
    else if (direction != STICK_NONE && (Sint32) (now - stick_next_repeat) >= 0) {
        stick_next_repeat = now + STICK_REPEAT_PERIOD;
        ui.input_from_gamepad = true;
        apply((UiAction) direction);
    }
}

void config_ui_draw(void)
{
    if (!screen_open)
        return;
    ui_render(renderer, text_cache, &ui, geo.screen_width, geo.screen_height);
}

// Applies an action and acts on its result; may close the screen
static void apply(UiAction action)
{
    UiResult result = ui_apply(&ui, action);
    if (result == UI_RESULT_SAVED) {
        config_ui_close();
        launcher_request_reload();
    }
    else if (result == UI_RESULT_CLOSE)
        config_ui_close();
    else
        sync_text_input();
}

// Starts text input on entering text entry and stops it on leaving
static void sync_text_input(void)
{
    bool want = ui.mode == MODE_TEXT;
    if (want && !text_input_started) {
        SDL_StartTextInput();
        text_input_started = true;
    }
    else if (!want && text_input_started) {
        SDL_StopTextInput();
        text_input_started = false;
    }
}

static bool map_key(const SDL_KeyboardEvent *key, UiAction *action)
{
    SDL_Keycode sym = key->keysym.sym;
    bool text_mode = ui.mode == MODE_TEXT;

    // Held keys repeat moves and erasing only, so a held Enter or Escape
    // (such as the one that opened this screen) can't select or leave twice
    switch (sym) {
        case SDLK_UP: *action = UI_UP; return true;
        case SDLK_DOWN: *action = UI_DOWN; return true;
        case SDLK_LEFT: *action = UI_LEFT; return true;
        case SDLK_RIGHT: *action = UI_RIGHT; return true;
        case SDLK_BACKSPACE:
            if (text_mode) {
                *action = UI_ERASE;
                return true;
            }
            *action = UI_BACK;
            return !key->repeat;
        default:
            break;
    }
    if (key->repeat)
        return false;

    switch (sym) {
        case SDLK_RETURN:
        case SDLK_KP_ENTER:
            *action = text_mode ? UI_ACCEPT : UI_SELECT;
            return true;
        case SDLK_ESCAPE:
            *action = UI_BACK;
            return true;
        case SDLK_DELETE:
            *action = UI_RESET;
            return true;
        case SDLK_F2:
            *action = UI_SAVE;
            return true;
        case SDLK_s:
            if (key->keysym.mod & KMOD_CTRL) {
                *action = UI_SAVE;
                return true;
            }
            return false;
        default:
            return false;
    }
}

static bool map_button(Uint8 button, UiAction *action)
{
    switch (button) {
        case SDL_CONTROLLER_BUTTON_DPAD_UP: *action = UI_UP; return true;
        case SDL_CONTROLLER_BUTTON_DPAD_DOWN: *action = UI_DOWN; return true;
        case SDL_CONTROLLER_BUTTON_DPAD_LEFT: *action = UI_LEFT; return true;
        case SDL_CONTROLLER_BUTTON_DPAD_RIGHT: *action = UI_RIGHT; return true;
        case SDL_CONTROLLER_BUTTON_A: *action = UI_SELECT; return true;
        case SDL_CONTROLLER_BUTTON_B: *action = UI_BACK; return true;
        case SDL_CONTROLLER_BUTTON_X: *action = UI_ERASE; return true;
        case SDL_CONTROLLER_BUTTON_Y: *action = UI_RESET; return true;
        case SDL_CONTROLLER_BUTTON_START: *action = UI_SAVE; return true;
        default: return false;
    }
}

// The left stick direction (a UiAction move) across the open gamepads, or STICK_NONE.
// Polling the open controllers, rather than remembering axis events, means an
// unplugged gamepad can't leave a direction stuck on.
static int read_stick(void)
{
    int best = STICK_NONE;
    int best_magnitude = GAMEPAD_DEADZONE;
    for (int i = 0; i < SDL_NumJoysticks(); i++) {
        SDL_GameController *pad = SDL_GameControllerFromInstanceID(SDL_JoystickGetDeviceInstanceID(i));
        if (pad == NULL)
            continue;
        int x = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTX);
        int y = SDL_GameControllerGetAxis(pad, SDL_CONTROLLER_AXIS_LEFTY);
        int ax = abs(x);
        int ay = abs(y);
        if (ay > best_magnitude && ay >= ax) {
            best_magnitude = ay;
            best = y < 0 ? UI_UP : UI_DOWN;
        }
        else if (ax > best_magnitude) {
            best_magnitude = ax;
            best = x < 0 ? UI_LEFT : UI_RIGHT;
        }
    }
    return best;
}
