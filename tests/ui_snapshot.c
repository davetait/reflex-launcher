// Renders Settings screen states to PNG files with SDL's software renderer.
// Usage: reflex-ui-snapshot <config.ini> <outdir>
// No window or input device is needed. Fonts are found through config.exe_path,
// which points at the build directory that holds the copied assets/ folder.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <SDL.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#ifdef PLATFORM_POSIX
#include <sys/stat.h>
#include <unistd.h>
#endif
#include "launcher.h"
#include "config_ui/ui_controller.h"
#include "config_ui/ui_text.h"
#include "config_ui/ui_render.h"

// Globals image.c refers to; the launcher defines them in launcher.c
extern Config config;
State state;
SDL_Renderer *renderer = NULL;
SDL_Texture *background_texture = NULL;
void quit_slideshow(void) {}
void set_draw_color(void) {}

static const char *out_dir = NULL;

static void fail(const char *what);
static void snapshot(const UiState *s, int w, int h, const char *name);
static void act(UiState *s, UiAction a);
static size_t find_section(const char *name);
static size_t find_row(size_t section, const char *key);
static void go_to(UiState *s, const char *section, const char *key);
static void snapshot_read_only(const char *config_path);

int main(int argc, char *argv[])
{
    if (argc != 3) {
        fprintf(stderr, "Usage: %s <config.ini> <outdir>\n", argv[0]);
        return 2;
    }
    out_dir = argv[2];
    config.exe_path = SNAPSHOT_ASSET_ROOT;

    if (SDL_Init(0) != 0)
        fail("SDL_Init");
    if (TTF_Init() != 0)
        fail("TTF_Init");
    if ((IMG_Init(IMG_INIT_PNG) & IMG_INIT_PNG) == 0)
        fail("IMG_Init");

    char err[512] = "";
    SettingsModel *model = settings_model_open(argv[1], err, sizeof(err));
    if (model == NULL) {
        fprintf(stderr, "Could not open %s: %s\n", argv[1], err);
        return 1;
    }
    UiState s;
    ui_init(&s, model);

    snapshot(&s, 1920, 1080, "01-sections.png");

    go_to(&s, "General", "VSync");
    snapshot(&s, 1920, 1080, "02-general.png");

    go_to(&s, "Background", "Color");
    snapshot(&s, 1920, 1080, "03-background.png");

    go_to(&s, "Background", "Mode");
    act(&s, UI_SELECT);
    if (s.mode != MODE_CHOICE_LIST)
        fail("choice list did not open");
    snapshot(&s, 1920, 1080, "04-choice-list.png");
    act(&s, UI_BACK);

    go_to(&s, "General", "StartupCmd");
    act(&s, UI_SELECT);
    if (s.mode != MODE_TEXT)
        fail("text entry did not open");
    ui_type(&s, "/usr/bin/xset dpms force on");
    act(&s, UI_DOWN);
    act(&s, UI_RIGHT);
    snapshot(&s, 1920, 1080, "05-text.png");
    act(&s, UI_BACK);

    // A value far wider than the field, cursor at the end
    go_to(&s, "General", "StartupCmd");
    act(&s, UI_SELECT);
    if (s.mode != MODE_TEXT)
        fail("text entry did not open");
    char long_value[320] = "";
    while (strlen(long_value) + 30 < 300)
        strcat(long_value, "/media/library/movies/part-x/");
    strcat(long_value, "start.sh");
    ui_type(&s, long_value);
    snapshot(&s, 1920, 1080, "12-text-long.png");
    act(&s, UI_BACK);

    go_to(&s, "Layout", "IconSize");
    act(&s, UI_SELECT);
    if (s.mode != MODE_TEXT)
        fail("number entry did not open");
    snapshot(&s, 1920, 1080, "06-number.png");
    act(&s, UI_BACK);

    go_to(&s, "Background", "Color");
    act(&s, UI_SELECT);
    if (s.mode != MODE_COLOR)
        fail("color picker did not open");
    act(&s, UI_RIGHT);
    snapshot(&s, 1920, 1080, "07-color.png");
    act(&s, UI_BACK);

    go_to(&s, "Titles", "Font");
    act(&s, UI_SELECT);
    if (s.mode != MODE_FILES)
        fail("file browser did not open");
    act(&s, UI_DOWN);
    act(&s, UI_DOWN);
    snapshot(&s, 1920, 1080, "08-files.png");
    act(&s, UI_BACK);

    // An unsaved change, then Back from the section list
    go_to(&s, "General", "VSync");
    act(&s, UI_SELECT);
    act(&s, UI_BACK);
    act(&s, UI_BACK);
    if (s.mode != MODE_CONFIRM_EXIT)
        fail("exit confirmation did not open");
    snapshot(&s, 1920, 1080, "09-confirm-exit.png");
    act(&s, UI_RIGHT);
    act(&s, UI_RIGHT);
    act(&s, UI_SELECT);  // Keep editing

    // The error config_ui_open shows when the file can't be read
    char missing[1024];
    snprintf(missing, sizeof(missing), "%s/no-such-folder/config.ini", out_dir);
    SettingsModel *unreadable = settings_model_open(missing, err, sizeof(err));
    if (unreadable != NULL)
        fail("opening a missing config");
    UiState error_state;
    ui_init_error(&error_state, err);
    snapshot(&error_state, 1920, 1080, "10-message.png");
    ui_free(&error_state);

    go_to(&s, "General", "VSync");
    snapshot(&s, 1280, 720, "11-general-720p.png");

    // A home menu without the Settings entry
    go_to(&s, "General", "DefaultMenu");
    act(&s, UI_RIGHT);
    act(&s, UI_SAVE);
    if (s.mode != MODE_CONFIRM_RISK)
        fail("lockout confirmation did not open");
    snapshot(&s, 1920, 1080, "13-confirm-risk.png");
    act(&s, UI_BACK);

    ui_free(&s);

    snapshot_read_only(argv[1]);
    IMG_Quit();
    TTF_Quit();
    SDL_Quit();
    return 0;
}

static void fail(const char *what)
{
    fprintf(stderr, "reflex-ui-snapshot: %s failed: %s\n", what, SDL_GetError());
    exit(1);
}

static void snapshot(const UiState *s, int w, int h, const char *name)
{
    SDL_Surface *surface = SDL_CreateRGBSurfaceWithFormat(0, w, h, 32, SDL_PIXELFORMAT_ARGB8888);
    if (surface == NULL)
        fail("SDL_CreateRGBSurfaceWithFormat");
    SDL_Renderer *r = SDL_CreateSoftwareRenderer(surface);
    if (r == NULL)
        fail("SDL_CreateSoftwareRenderer");
    UiTextCache *cache = ui_text_cache_new(r, h);
    if (cache == NULL)
        fail("ui_text_cache_new (font not found)");

    // A stand-in for the launcher behind the panel
    SDL_SetRenderDrawColor(r, 40, 70, 110, 255);
    SDL_RenderClear(r);
    ui_render(r, cache, s, w, h);
    // The launcher clears with a persistent draw color, so ui_render must restore it
    Uint8 cr, cg, cb, ca;
    SDL_BlendMode mode;
    SDL_GetRenderDrawColor(r, &cr, &cg, &cb, &ca);
    SDL_GetRenderDrawBlendMode(r, &mode);
    if (cr != 40 || cg != 70 || cb != 110 || ca != 255 || mode != SDL_BLENDMODE_NONE) {
        fprintf(stderr, "reflex-ui-snapshot: ui_render changed the draw color or blend mode\n");
        exit(1);
    }
    SDL_RenderPresent(r);

    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", out_dir, name);
    if (IMG_SavePNG(surface, path) != 0)
        fail("IMG_SavePNG");
    printf("%s\n", path);

    ui_text_cache_free(cache);
    SDL_DestroyRenderer(r);
    SDL_FreeSurface(surface);
}

static void act(UiState *s, UiAction a)
{
    UiResult result = ui_apply(s, a);
    if (result != UI_RESULT_NONE) {
        fprintf(stderr, "reflex-ui-snapshot: the screen closed unexpectedly\n");
        exit(1);
    }
}

static size_t find_section(const char *name)
{
    for (size_t i = 0; i < settings_model_section_count(); i++) {
        if (strcmp(settings_model_section_name(i), name) == 0)
            return i;
    }
    fprintf(stderr, "reflex-ui-snapshot: no section %s\n", name);
    exit(1);
}

static size_t find_row(size_t section, const char *key)
{
    for (size_t i = 0; i < settings_model_row_count(section); i++) {
        if (strcmp(settings_model_row(section, i)->key, key) == 0)
            return i;
    }
    fprintf(stderr, "reflex-ui-snapshot: no setting %s\n", key);
    exit(1);
}

// Moves focus to a setting the way a user would: back to the section list, then down
static void go_to(UiState *s, const char *section, const char *key)
{
    size_t target_section = find_section(section);
    size_t target_row = find_row(target_section, key);
    if (s->mode != MODE_SECTIONS && s->mode != MODE_ROWS)
        fail("go_to from an overlay");
    if (s->mode == MODE_ROWS)
        act(s, UI_BACK);
    while (s->section > target_section)
        act(s, UI_UP);
    while (s->section < target_section)
        act(s, UI_DOWN);
    act(s, UI_SELECT);
    while (s->row < target_row)
        act(s, UI_DOWN);
}

// Save on a config in a read-only folder offers to save a copy (Linux and macOS)
static void snapshot_read_only(const char *config_path)
{
#ifdef PLATFORM_POSIX
    char dir[1024], path[1100];
    snprintf(dir, sizeof(dir), "%s/read-only", out_dir);
    snprintf(path, sizeof(path), "%s/config.ini", dir);
    mkdir(dir, 0700);
    chmod(dir, 0700);
    size_t length = 0;
    char *text = SDL_LoadFile(config_path, &length);
    FILE *file = fopen(path, "wb");
    if (text == NULL || file == NULL || fwrite(text, 1, length, file) != length)
        fail("copying the config");
    fclose(file);
    SDL_free(text);
    chmod(dir, 0500);

    char err[512] = "";
    SettingsModel *model = settings_model_open(path, err, sizeof(err));
    if (model == NULL)
        fail("opening the read-only config");
    settings_model_set_user_config_path(model, "/home/pi/.config/flex-launcher/config.ini");
    UiState s;
    ui_init(&s, model);
    go_to(&s, "General", "VSync");
    act(&s, UI_SELECT);
    act(&s, UI_SAVE);
    if (s.mode == MODE_CONFIRM_COPY)
        snapshot(&s, 1920, 1080, "14-confirm-copy.png");
    else
        fprintf(stderr, "reflex-ui-snapshot: skipped 14-confirm-copy.png (the folder is writable, e.g. as root)\n");
    ui_free(&s);

    chmod(dir, 0700);
    remove(path);
    remove(dir);
#else
    (void) config_path;
#endif
}
