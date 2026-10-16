#ifndef UI_TEXT_H
#define UI_TEXT_H
#include <SDL.h>

// Text textures for the Settings screen, in the bundled UI font at sizes taken
// from the screen height. Rendered strings are cached (least recently used, 256).

typedef enum { UI_FONT_BODY, UI_FONT_SMALL, UI_FONT_HEADER } UiFont;
typedef struct ui_text_cache UiTextCache;

// Opens OpenSans at h/27 (body), h/36 (small) and h/18 (header); NULL if no font
UiTextCache *ui_text_cache_new(SDL_Renderer *r, int screen_height);
void ui_text_cache_free(UiTextCache *c);
// The texture stays owned by the cache and is valid until the next ui_text call.
// Returns NULL (w and h set to 0) for empty text or on failure. Characters the
// font lacks (such as the keyboard's arrows) are replaced with ASCII.
SDL_Texture *ui_text(UiTextCache *c, UiFont f, const char *text, SDL_Color color, int *w, int *h);
// Width in pixels of text as ui_text would draw it, without rendering it
int ui_text_width(UiTextCache *c, UiFont f, const char *text);
// The point size the font was opened at: the "line" unit for layout
int ui_text_size(const UiTextCache *c, UiFont f);

#endif
