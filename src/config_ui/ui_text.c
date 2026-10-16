#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL.h>
#include <SDL_ttf.h>
#include "../launcher.h"
#include <launcher_config.h>
#include "../image.h"
#include "../debug.h"
#include "ui_text.h"

#define CACHE_SIZE 256
#define FONT_COUNT 3
#define MAX_TEXT_BYTES 1024

typedef struct {
    char *text;             // the requested text (cache key with font and color)
    Uint32 hash;
    UiFont font;
    SDL_Color color;
    SDL_Texture *texture;
    int w, h;
    Uint32 last_used;
} CacheEntry;

struct ui_text_cache {
    SDL_Renderer *renderer;
    TTF_Font *fonts[FONT_COUNT];
    int sizes[FONT_COUNT];
    CacheEntry entries[CACHE_SIZE];
    Uint32 clock;
};

// Characters the keyboard and layout use that a font may lack, with ASCII stand-ins
static const struct {
    Uint32 code_point;
    const char *ascii;
} fallbacks[] = {
    {0x232B, "Del"},  // erase to the left
    {0x2190, "<"},
    {0x2192, ">"},
    {0x2039, "<"},
    {0x203A, ">"},
    {0x2026, "..."},
};

static Uint32 hash_text(const char *text);
static bool same_color(SDL_Color a, SDL_Color b);
static size_t decode_utf8(const char *s, Uint32 *code_point);
static bool glyph_provided(TTF_Font *font, Uint32 code_point);
static void substitute_missing(TTF_Font *font, const char *text, char *out, size_t out_len);
static void clear_entry(CacheEntry *e);

UiTextCache *ui_text_cache_new(SDL_Renderer *r, int screen_height)
{
    char *path = find_default_font(FILENAME_DEFAULT_FONT);
    if (path == NULL) {
        log_error("Settings: could not find the font %s", FILENAME_DEFAULT_FONT);
        return NULL;
    }
    UiTextCache *c = calloc(1, sizeof(*c));
    if (c == NULL) {
        free(path);
        return NULL;
    }
    c->renderer = r;
    c->sizes[UI_FONT_BODY] = screen_height / 27;
    c->sizes[UI_FONT_SMALL] = screen_height / 36;
    c->sizes[UI_FONT_HEADER] = screen_height / 18;
    for (int i = 0; i < FONT_COUNT; i++) {
        if (c->sizes[i] < 6)
            c->sizes[i] = 6;
        c->fonts[i] = TTF_OpenFont(path, c->sizes[i]);
        if (c->fonts[i] == NULL) {
            log_error("Settings: could not open %s: %s", path, TTF_GetError());
            free(path);
            ui_text_cache_free(c);
            return NULL;
        }
    }
    free(path);
    return c;
}

void ui_text_cache_free(UiTextCache *c)
{
    if (c == NULL)
        return;
    for (size_t i = 0; i < CACHE_SIZE; i++)
        clear_entry(&c->entries[i]);
    for (int i = 0; i < FONT_COUNT; i++) {
        if (c->fonts[i] != NULL)
            TTF_CloseFont(c->fonts[i]);
    }
    free(c);
}

int ui_text_size(const UiTextCache *c, UiFont f)
{
    return c->sizes[f];
}

int ui_text_width(UiTextCache *c, UiFont f, const char *text)
{
    if (text == NULL || text[0] == '\0')
        return 0;
    char drawn[MAX_TEXT_BYTES];
    substitute_missing(c->fonts[f], text, drawn, sizeof(drawn));
    int w = 0, h = 0;
    if (TTF_SizeUTF8(c->fonts[f], drawn, &w, &h) != 0)
        return 0;
    return w;
}

SDL_Texture *ui_text(UiTextCache *c, UiFont f, const char *text, SDL_Color color, int *w, int *h)
{
    *w = 0;
    *h = 0;
    if (text == NULL || text[0] == '\0')
        return NULL;
    c->clock++;
    Uint32 hash = hash_text(text);

    // Look up, remembering the least recently used slot in case of a miss
    CacheEntry *slot = &c->entries[0];
    for (size_t i = 0; i < CACHE_SIZE; i++) {
        CacheEntry *e = &c->entries[i];
        if (e->text != NULL && e->hash == hash && e->font == f && same_color(e->color, color) &&
            strcmp(e->text, text) == 0) {
            e->last_used = c->clock;
            *w = e->w;
            *h = e->h;
            return e->texture;
        }
        if (slot->text != NULL && (e->text == NULL || e->last_used < slot->last_used))
            slot = e;
    }

    char drawn[MAX_TEXT_BYTES];
    substitute_missing(c->fonts[f], text, drawn, sizeof(drawn));
    SDL_Surface *surface = TTF_RenderUTF8_Blended(c->fonts[f], drawn, color);
    if (surface == NULL)
        return NULL;
    SDL_Texture *texture = SDL_CreateTextureFromSurface(c->renderer, surface);
    int surface_w = surface->w, surface_h = surface->h;
    SDL_FreeSurface(surface);
    if (texture == NULL)
        return NULL;
    char *key = strdup(text);
    if (key == NULL) {
        SDL_DestroyTexture(texture);
        return NULL;
    }

    clear_entry(slot);
    slot->text = key;
    slot->hash = hash;
    slot->font = f;
    slot->color = color;
    slot->texture = texture;
    slot->w = surface_w;
    slot->h = surface_h;
    slot->last_used = c->clock;
    *w = surface_w;
    *h = surface_h;
    return texture;
}

static void clear_entry(CacheEntry *e)
{
    free(e->text);
    if (e->texture != NULL)
        SDL_DestroyTexture(e->texture);
    memset(e, 0, sizeof(*e));
}

// FNV-1a
static Uint32 hash_text(const char *text)
{
    Uint32 hash = 2166136261u;
    for (const unsigned char *p = (const unsigned char *) text; *p != '\0'; p++) {
        hash ^= *p;
        hash *= 16777619u;
    }
    return hash;
}

static bool same_color(SDL_Color a, SDL_Color b)
{
    return a.r == b.r && a.g == b.g && a.b == b.b && a.a == b.a;
}

// Decodes one UTF-8 character; returns its length in bytes (1 for a malformed byte)
static size_t decode_utf8(const char *s, Uint32 *code_point)
{
    const unsigned char *u = (const unsigned char *) s;
    size_t length = 1;
    Uint32 value = u[0];
    if ((u[0] & 0xE0) == 0xC0) {
        length = 2;
        value = u[0] & 0x1Fu;
    }
    else if ((u[0] & 0xF0) == 0xE0) {
        length = 3;
        value = u[0] & 0x0Fu;
    }
    else if ((u[0] & 0xF8) == 0xF0) {
        length = 4;
        value = u[0] & 0x07u;
    }
    for (size_t i = 1; i < length; i++) {
        if ((u[i] & 0xC0) != 0x80) {
            *code_point = u[0];
            return 1;
        }
        value = (value << 6) | (u[i] & 0x3Fu);
    }
    *code_point = value;
    return length;
}

static bool glyph_provided(TTF_Font *font, Uint32 code_point)
{
#ifdef SDL_TTF_VERSION_ATLEAST
#if SDL_TTF_VERSION_ATLEAST(2, 0, 18)
    return TTF_GlyphIsProvided32(font, code_point) != 0;
#else
    return code_point > 0xFFFF || TTF_GlyphIsProvided(font, (Uint16) code_point) != 0;
#endif
#else
    return code_point > 0xFFFF || TTF_GlyphIsProvided(font, (Uint16) code_point) != 0;
#endif
}

// Copies text to out, replacing known characters the font can't draw with ASCII
static void substitute_missing(TTF_Font *font, const char *text, char *out, size_t out_len)
{
    size_t used = 0;
    const char *p = text;
    while (*p != '\0') {
        Uint32 code_point;
        size_t length = decode_utf8(p, &code_point);
        const char *piece = NULL;
        size_t piece_len = length;
        if (code_point >= 0x80 && !glyph_provided(font, code_point)) {
            for (size_t i = 0; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
                if (fallbacks[i].code_point == code_point) {
                    piece = fallbacks[i].ascii;
                    piece_len = strlen(piece);
                    break;
                }
            }
        }
        if (piece == NULL)
            piece = p;
        if (used + piece_len + 1 > out_len)
            break;
        memcpy(out + used, piece, piece_len);
        used += piece_len;
        p += length;
    }
    out[used] = '\0';
}
