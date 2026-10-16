#ifndef CONFIG_UI_H
#define CONFIG_UI_H
#include <stdbool.h>
#include <SDL.h>

// The Settings screen as seen by the launcher: open/close, input and drawing.
// While it is open the launcher routes keyboard and gamepad input here and
// suspends its own menus, hotkeys, gamepad bindings and screensaver.

bool config_ui_is_open(void);
// Opens the model from config.config_path; on failure shows the error in the screen
void config_ui_open(void);
// Frees the state and text cache and stops text input. Must run before the renderer is destroyed.
void config_ui_close(void);
// Maps and applies one input event. A save closes the screen and requests a reload.
void config_ui_handle_event(const SDL_Event *e);
// Left-stick movement with repeat (400 ms initial delay, then every 80 ms)
void config_ui_update(Uint32 now);
// Draws the screen over the current frame
void config_ui_draw(void);

#endif
