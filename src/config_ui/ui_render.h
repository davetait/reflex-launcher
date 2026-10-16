#ifndef UI_RENDER_H
#define UI_RENDER_H
#include <SDL.h>
#include "ui_controller.h"
#include "ui_text.h"

// Draws the Settings screen for the given state over whatever is on the renderer
void ui_render(SDL_Renderer *r, UiTextCache *c, const UiState *s, int w, int h);

#endif
