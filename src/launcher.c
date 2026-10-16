#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <SDL.h>
#include <SDL_syswm.h>
#include <SDL_image.h>
#include <SDL_ttf.h>
#include <SDL_thread.h>
#include "launcher.h"
#include <launcher_config.h>
#include "image.h"
#include "util.h"
#include "debug.h"
#include "clock.h"
#include "platform/platform.h"
#include "platform/fileops.h"
#include "ini_doc.h"

static void init_sdl(void);
static void init_sdl_image(void);
static void create_window(void);
static int load_title_font(void);
static int load_menu(Menu *menu, bool set_back_menu, bool reset_position);
static int load_menu_by_name(const char *menu_name, bool set_back_menu, bool reset_position);
static void update_slideshow(void);
static void resume_slideshow(void);
static void update_screensaver(void);
static void update_clock(bool block);
static void init_slideshow(void);
static void init_screensaver(void);
static void calculate_button_geometry(Entry *entry, int buttons);
static void render_buttons(Menu *menu);
static void move_left(void);
static void move_right(void);
static void load_submenu(const char *submenu);
static void load_back_menu(Menu *menu);
static void draw_screen(void);
static void handle_keypress(SDL_Keysym *key);
static void execute_command(const char *command);
static void poll_gamepad(void);
static void init_gamepad(Gamepad **gamepad, int device_index);
static void connect_gamepad(int device_index, bool open, bool raise_error);
static void disconnect_gamepad(int id, bool disconnect, bool remove);
static void open_controller(Gamepad *gamepad, bool raise_error);
static void cleanup(void);
static int create_renderer(void);
static int launcher_load(void);
static void launcher_unload(void);
static void free_config_lists(void);
static int restore_config_backup(const char *path);

Config config;

// Initialize default states
State state = { false };

// Global variables
SDL_Window *window                    = NULL;
SDL_Renderer *renderer                = NULL;
SDL_Texture *background_texture       = NULL;
SDL_Texture *background_overlay       = NULL;
Menu *default_menu                    = NULL;
Menu *current_menu                    = NULL;
Entry *current_entry                  = NULL;
Highlight *highlight                  = NULL;
Scroll *scroll                        = NULL;
Slideshow *slideshow                  = NULL;
Screensaver *screensaver              = NULL;
FILE *log_file                        = NULL;
Gamepad *gamepads                     = NULL;
GamepadControl *gamepad_controls      = NULL;
Hotkey *hotkeys                       = NULL;
Clock *clk                            = NULL;
TTF_Font *clock_font                  = NULL;
SDL_Thread *Slideshowhread            = NULL;
SDL_Thread *clock_thread              = NULL;
SDL_Event event;
SDL_SysWMinfo wm_info;
SDL_DisplayMode display_mode;
TextInfo title_info;
Ticks ticks;
Geometry geo;
Uint32 refresh_period;
Uint32 delay_period;
Uint32 repeat_period;


// A function to initialize SDL
static void init_sdl()
{
#ifdef PLATFORM_POSIX
    SDL_SetHint(SDL_HINT_VIDEO_X11_NET_WM_BYPASS_COMPOSITOR, "0");
#endif
    SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "1");
    if (SDL_Init(SDL_INIT_VIDEO) < 0)
        log_fatal("Could not initialize SDL\n%s", SDL_GetError());

    SDL_GetDesktopDisplayMode(0, &display_mode);
    if (display_mode.refresh_rate <= 0) {
        log_debug("Display reported refresh rate %i, assuming 60 Hz", display_mode.refresh_rate);
        display_mode.refresh_rate = 60;
    }
    geo.screen_width = display_mode.w;
    geo.screen_height = display_mode.h;
    refresh_period = 1000 / (Uint32) display_mode.refresh_rate;
    geo.screen_margin = (int) (SCREEN_MARGIN * (float) geo.screen_height);
}

// A function to create the window (once per process)
static void create_window()
{
    window = SDL_CreateWindow(PROJECT_NAME,
                 SDL_WINDOWPOS_UNDEFINED,
                 SDL_WINDOWPOS_UNDEFINED,
                 0,
                 0,
                 SDL_WINDOW_FULLSCREEN_DESKTOP
             );
    if (window == NULL)
        log_fatal("Could not create SDL Window\n%s", SDL_GetError());
    SDL_ShowCursor(SDL_DISABLE);
#ifdef _WIN32
    SDL_VERSION(&wm_info.version);
    SDL_GetWindowWMInfo(window, &wm_info);
#endif
}

// A function to create the renderer (on every config load; VSync is a renderer flag)
static int create_renderer()
{
    Uint32 renderer_flags = SDL_RENDERER_ACCELERATED;
    if (!config.vsync) {
        if (config.fps_limit > MIN_FPS_LIMIT && config.fps_limit <= display_mode.refresh_rate)
            refresh_period = 1000 / (Uint32) config.fps_limit;
        else
            config.vsync = true;
    }
    if (config.vsync) {
        refresh_period = 1000 / (Uint32) display_mode.refresh_rate;
        renderer_flags |= SDL_RENDERER_PRESENTVSYNC;
    }
    if (config.gamepad_enabled) {
        delay_period = GAMEPAD_REPEAT_DELAY / refresh_period;
        repeat_period = GAMEPAD_REPEAT_INTERVAL / refresh_period;
        if (!repeat_period)
            repeat_period = 1;
    }
    if (slideshow != NULL)
        slideshow->transition_change_rate = 255.0f / ((float) config.slideshow_transition_time / (float) refresh_period);

    renderer = SDL_CreateRenderer(window, -1, renderer_flags);
    if (renderer == NULL) {
        log_error("Could not initialize renderer\n%s", SDL_GetError());
        return 1;
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    set_draw_color();
#ifdef _WIN32
    if (config.background_mode == BACKGROUND_TRANSPARENT)
        make_window_transparent();
#endif
    return 0;
}

// A function to initialize the SDL_image library
static void init_sdl_image()
{
    int img_flags = IMG_INIT_PNG | IMG_INIT_JPG | IMG_INIT_WEBP;
    if (!(IMG_Init(img_flags) & img_flags))
        log_fatal("Could not initialize SDL_image\n%s", IMG_GetError());
}

// A function to set the color of the renderer
void set_draw_color()
{
    SDL_Color *color = NULL;
    if (config.background_mode == BACKGROUND_COLOR)
        color = &config.background_color;
    else if (config.background_mode == BACKGROUND_TRANSPARENT)
        color = &config.chroma_key_color;

    if (color == NULL)
        SDL_SetRenderDrawColor(renderer, 0xFF, 0xFF, 0xFF, 0xFF);
    else
        SDL_SetRenderDrawColor(renderer,
            color->r,
            color->g,
            color->b,
            color->a
        );
}

// A function to load the title font from the config
static int load_title_font()
{
    title_info = (TextInfo) {
        .font_size = (int) config.title_font_size,
        .shadow = config.title_shadows,
        .font_path = &config.title_font_path,
        .max_width = config.icon_size,
        .oversize_mode = config.title_oversize_mode,
        .color = &config.title_font_color
    };
    if (config.title_shadows) {
        title_info.shadow_color = &config.title_shadow_color;
        calculate_shadow_alpha(title_info);
    }
    else
        title_info.shadow_color = NULL;

    if (load_font(&title_info, FILENAME_DEFAULT_FONT)) {
        log_error("Could not load title font");
        return 1;
    }
    geo.font_height = config.titles_enabled ? TTF_FontHeight(title_info.font) : 0;
    return 0;
}

// A function to free the menu, hotkey, and gamepad control lists built by the parser
static void free_config_lists()
{
    Menu *next_menu;
    for (Menu *m = config.first_menu; m != NULL; m = next_menu) {
        next_menu = m->next;
        Entry *next_entry;
        for (Entry *e = m->first_entry; e != NULL; e = next_entry) {
            next_entry = e->next;
            free(e->title);
            free(e->icon_path);
            free(e->icon_selected_path);
            free(e->cmd);
            free(e);
        }
        free(m->name);
        free(m);
    }
    config.first_menu = NULL;
    config.num_menus = 0;

    Hotkey *next_hotkey;
    for (Hotkey *h = hotkeys; h != NULL; h = next_hotkey) {
        next_hotkey = h->next;
        free(h->cmd);
        free(h);
    }
    hotkeys = NULL;

    GamepadControl *next_control;
    for (GamepadControl *g = gamepad_controls; g != NULL; g = next_control) {
        next_control = g->next;
        free(g->cmd);
        free(g);
    }
    gamepad_controls = NULL;
}

// A function to free everything launcher_load() created, keeping the window and SDL itself
static void launcher_unload()
{
    // Worker threads render into clock/slideshow buffers, so stop them first
    SDL_WaitThread(Slideshowhread, NULL);
    Slideshowhread = NULL;
    SDL_WaitThread(clock_thread, NULL);
    clock_thread = NULL;

#ifdef _WIN32
    unregister_exit_hotkey();
#endif
    if (gamepads != NULL)
        disconnect_gamepad(-1, true, true);

    if (title_info.font != NULL) {
        TTF_CloseFont(title_info.font);
        title_info.font = NULL;
    }
    if (clk != NULL) {
        if (clk->text_info.font != NULL)
            TTF_CloseFont(clk->text_info.font);
        SDL_FreeSurface(clk->time_surface);
        SDL_FreeSurface(clk->date_surface);
        free(clk);
        clk = NULL;
    }
    if (slideshow != NULL) {
        SDL_FreeSurface(slideshow->transition_surface);
        quit_slideshow();
        slideshow = NULL;
    }

    // Destroying the renderer frees every texture created with it
    if (renderer != NULL) {
        SDL_DestroyRenderer(renderer);
        renderer = NULL;
    }
    background_texture = NULL;
    background_overlay = NULL;
    free(highlight);
    highlight = NULL;
    free(scroll);
    scroll = NULL;
    free(screensaver);
    screensaver = NULL;

    free_config_lists();
    default_menu = NULL;
    current_menu = NULL;
    current_entry = NULL;

    free(config.default_menu);
    free(config.background_image);
    free(config.title_font_path);
    free(config.slideshow_directory);
    free(config.clock_font_path);
    free(config.gamepad_mappings_file);
    free(config.startup_cmd);
    free(config.quit_cmd);
    config.default_menu = NULL;
    config.background_image = NULL;
    config.title_font_path = NULL;
    config.slideshow_directory = NULL;
    config.clock_font_path = NULL;
    config.gamepad_mappings_file = NULL;
    config.startup_cmd = NULL;
    config.quit_cmd = NULL;

    bool has_focus = state.has_focus;
    state = (State) { .has_focus = has_focus };
}

// A function to close subsystems and free memory before quitting
static void cleanup()
{
    launcher_unload();
    if (window != NULL) {
        SDL_DestroyWindow(window);
        window = NULL;
    }
    quit_svg();
    TTF_Quit();
    IMG_Quit();
    SDL_Quit();
    free(config.exe_path);
    free(config.config_path);
    if (log_file != NULL)
        fclose(log_file);
}

// A function to handle key presses from keyboard
static void handle_keypress(SDL_Keysym *key)
{
    if (config.debug)
        log_debug("Key %s (#%X) detected", SDL_GetKeyName(key->sym), key->sym);

    // Check default keys
    if (key->sym == SDLK_LEFT)
        move_left();
    else if (key->sym == SDLK_RIGHT)
        move_right();
    else if (key->sym == SDLK_RETURN) {
        log_debug("Selected Entry:\n"
            "Title: %s\n"
            "Icon Path: %s\n"
            "Command: %s", 
            current_entry->title, 
            current_entry->icon_path, 
            current_entry->cmd
        );
        
        execute_command(current_entry->cmd);
    }
    else if (key->sym == SDLK_BACKSPACE)
        load_back_menu(current_menu);

    //Check hotkeys
    else {
        for (Hotkey *i = hotkeys; i != NULL; i = i->next) {
            if (key->sym == i->keycode) {
                execute_command(i->cmd);
                break;
            }
        }
    }
}

// A function to quit the slideshow mode in case of error or program exit
void quit_slideshow()
{
    // Free allocated image paths
    for (int i = 0; i < slideshow->num_images; i++)
        free(slideshow->images[i]);
    free(slideshow->images);
    free(slideshow->order);
    free(slideshow);
}

// A function to initialize the slideshow background mode
static void init_slideshow()
{
    if (!directory_exists(config.slideshow_directory)) {
        log_error("Slideshow directory '%s' does not exist, "
            "Switching to color background mode",
            config.slideshow_directory
        );
        config.background_mode = BACKGROUND_COLOR;
        set_draw_color();
        return;
    }
    // Allocate and initialize slideshow struct
    slideshow = malloc(sizeof(Slideshow));
    *slideshow = (Slideshow) {
        .i = -1,
        .num_images = 0,
        .transition_surface = NULL,
        .transition_texture = NULL,
        .transition_alpha = 0.f,
        .transition_change_rate = 0.f,
        .images = NULL,
        .order = NULL
    };

    // Find background images from directory
    scan_slideshow_directory(slideshow, config.slideshow_directory);
    
    // Handle errors
    if (!slideshow->num_images) {
        log_error("No images found in slideshow directory '%s', "
            "Changing background mode to color", 
            config.slideshow_directory
        );
        config.background_mode = BACKGROUND_COLOR;
        quit_slideshow();
        slideshow = NULL;
    } 
    else if (slideshow->num_images == 1) {
        log_error("Only one image found in slideshow directory %s"
            "Changing background mode to single image", 
            config.slideshow_directory
        );
        free(config.background_image);
        config.background_image = strdup(slideshow->images[0]);
        config.background_mode = BACKGROUND_IMAGE;
        quit_slideshow();
        slideshow = NULL;
    }

    // Generate array of random numbers for image order, load first image
    else {
        slideshow->order = malloc(sizeof(int) * (size_t) slideshow->num_images);
        random_array(slideshow->order, slideshow->num_images);
        if (config.debug)
            debug_slideshow(slideshow);
    }
}

// A function to initialize the screensaver feature
static void init_screensaver()
{
    // Allocate memory for structure
    screensaver = malloc(sizeof(Screensaver));
    
    // Convert intensity string to float
    char intensity[PERCENT_MAX_CHARS];
    if (config.screensaver_intensity_str[0] != '\0')
        copy_string(intensity, config.screensaver_intensity_str, sizeof(intensity));
    else
        copy_string(intensity, DEFAULT_SCREENSAVER_INTENSITY, sizeof(intensity));
    size_t length = strlen(intensity);
    intensity[length - 1] = '\0';
    float percent = (float) atof(intensity);

    // Calculate alpha end value
    screensaver->alpha_end_value = 255.0f * percent / 100.0f;
    if (screensaver->alpha_end_value < 1.0f) {
        log_error("Invalid screensaver intensity value, disabling feature");
        config.screensaver_enabled = false;
        free(screensaver);
        screensaver = NULL;
        return;
    }
    else if (screensaver->alpha_end_value >= 255.0f)
        screensaver->alpha_end_value = 255.0f;

    screensaver->transition_change_rate = screensaver->alpha_end_value / ((float) SCREENSAVER_TRANSITION_TIME / (float) refresh_period);
    
    // Render texture
    SDL_Surface *surface = NULL;
    surface = SDL_CreateRGBSurfaceWithFormat(0, 
                  geo.screen_width, 
                  geo.screen_height, 
                  32,
                  SDL_PIXELFORMAT_ARGB8888
              );
    Uint32 color = SDL_MapRGBA(surface->format, 0, 0, 0, 0xFF);
    SDL_FillRect(surface, NULL, color);
    screensaver->texture = load_texture(surface);
    screensaver->alpha = 0.0f;
    SDL_SetTextureAlphaMod(screensaver->texture, 0.0f);
}

// A function to resume the slideshow after a launched application returns
static void resume_slideshow()
{
    ticks.slideshow_load = ticks.main;
}

// A function to load a menu
static int load_menu(Menu *menu, bool set_back_menu, bool reset_position)
{
    if (menu == NULL)
        return 1;

    unsigned int buttons;
    Menu *previous_menu = current_menu;

    current_menu = menu;
    log_debug("Loading menu '%s'", current_menu->name);

    // Return error if the menu doesn't contain entires
    if (current_menu->num_entries == 0) {
        log_error("No valid entries found for Menu '%s'", current_menu->name);
        current_menu = previous_menu;
        return 1;
    }

    // Render the menu if not already rendered
    if (current_menu->rendered == false)
        render_buttons(current_menu);

    // Set menu properties
    if (set_back_menu)
        current_menu->back = previous_menu;

    if (reset_position) {
        current_entry = current_menu->first_entry;
        current_menu->root_entry = current_entry;
        current_menu->highlight_position = 0;
        current_menu->page = 0;
    }
    else
        current_entry = current_menu->last_selected_entry;

    buttons = current_menu->num_entries - (current_menu->page)*config.max_buttons;
    if (buttons > config.max_buttons)
        buttons = config.max_buttons;
    
    // Recalculate the screen geometry
    calculate_button_geometry(current_menu->root_entry, (int) buttons);
    if (config.highlight) {
        highlight->rect.x = current_entry->icon_rect.x - config.highlight_hpadding;
        highlight->rect.y = current_entry->icon_rect.y - config.highlight_vpadding;
    }
    return 0;
}

// A function to load a menu by its name
static int load_menu_by_name(const char *menu_name, bool set_back_menu, bool reset_position)
{
    Menu *menu = get_menu(menu_name);
    return load_menu(menu, set_back_menu, reset_position);
}

// A function to calculate the layout of the buttons
static void calculate_button_geometry(Entry *entry, int buttons)
{
    // Calculate proper spacing
    geo.x_margin = (geo.screen_width - config.icon_size*buttons -
                   buttons*config.icon_spacing + config.icon_spacing) / 2;
    geo.x_advance = config.icon_size + config.icon_spacing;
    geo.num_buttons = buttons;

    // Assign values to entries
    for (int i = 0; i < geo.num_buttons; i++) {
            entry->icon_rect.x = geo.x_margin + i*geo.x_advance;
            entry->icon_rect.y = geo.y_margin;
            entry->icon_rect.w = config.icon_size;
            entry->icon_rect.h = config.icon_size;
            entry->text_rect.x = entry->icon_rect.x +
                                 (entry->icon_rect.w - entry->text_rect.w) / 2;
            entry->text_rect.y = entry->icon_rect.y + config.icon_size + entry->title_offset + 
                                 config.title_padding;
            entry = entry->next;
    }
}

// A function to render all buttons (icon and text) for a menu
static void render_buttons(Menu *menu)
{
    Entry *entry;
    int h;
    for (entry = menu->first_entry; entry != NULL; entry = entry->next) {
        entry->icon = load_texture_from_file(entry->icon_path);
        entry->icon_selected = (entry->icon_selected_path != NULL) ? load_texture_from_file(entry->icon_selected_path) : NULL;
        if (config.titles_enabled) {
            entry->title_texture = render_text_texture(entry->title, &title_info, &entry->text_rect, &h);
            if (config.title_oversize_mode == OVERSIZE_SHRINK && h != geo.font_height)
                entry->title_offset = (geo.font_height - h) / 2;
        }
    }
    menu->rendered = true;
}

// A function to move the selection left when clicked by user
static void move_left()
{
    // If we are not in leftmost position, move highlight left
    if (current_menu->highlight_position > 0) {
        if (config.highlight)
            highlight->rect.x -= geo.x_advance;
        current_menu->highlight_position--;
        current_entry = current_entry->previous;
    }

    // If we are in leftmost position...
    else if (current_menu->highlight_position == 0 && (current_menu->page > 0 || config.wrap_entries)) {
        unsigned int buttons;
        current_entry = current_entry->previous;

        // Load the previous page if there is a valid previous entry
        if (current_entry) {
            buttons = config.max_buttons;
            current_menu->root_entry = advance_entries(current_menu->root_entry, (int) buttons, DIRECTION_LEFT);
            current_menu->page--;
        }

        // If the user has the wrap entries setting, select the last entry in the menu
        else {
            current_entry = advance_entries(current_menu->first_entry, (int) current_menu->num_entries - 1, DIRECTION_RIGHT);
            unsigned int num_pages = DIV_ROUND_UP(current_menu->num_entries, config.max_buttons);
            current_menu->root_entry = advance_entries(current_menu->root_entry,
                (int) ((num_pages - 1 - current_menu->page) * config.max_buttons),
                DIRECTION_RIGHT
            );
            current_menu->page = num_pages - 1;
            buttons = current_menu->num_entries - current_menu->page * config.max_buttons;
        }

        calculate_button_geometry(current_menu->root_entry, (int) buttons);
        if (config.highlight)
            highlight->rect.x = current_entry->icon_rect.x - config.highlight_hpadding;
        current_menu->highlight_position = buttons - 1;
    }
}

// A function to move the selection right when clicked by the user
static void move_right()
{
    // If we are not in the rightmost position, move highlight right
    if ((int) current_menu->highlight_position < (geo.num_buttons - 1)) {
        if (config.highlight)
            highlight->rect.x += geo.x_advance;
        current_menu->highlight_position++;
        current_entry = current_entry->next;
    }

    // If we are in the rightmost postion, but there are more entries in the menu, load next page
    else if (current_menu->highlight_position + current_menu->page*config.max_buttons <
    (current_menu->num_entries - 1)) {
        unsigned int buttons = current_menu->num_entries - (current_menu->page + 1)*config.max_buttons;
        if (buttons > config.max_buttons)
            buttons = config.max_buttons;
        current_entry = current_entry->next;
        current_menu->root_entry = current_entry;
        calculate_button_geometry(current_menu->root_entry, (int) buttons);
        if (config.highlight)
            highlight->rect.x = current_entry->icon_rect.x - config.highlight_hpadding;
        current_menu->page++;
        current_menu->highlight_position = 0;
    }

    // If user has the wrap entries setting, reset menu to first entry
    else if (config.wrap_entries) {
        current_entry = current_menu->first_entry;
        current_menu->root_entry = current_entry;
        current_menu->highlight_position = 0;
        current_menu->page = 0;
        if (config.highlight)
            highlight->rect.x = current_entry->icon_rect.x - config.highlight_hpadding;
        calculate_button_geometry(current_menu->root_entry, (int) MIN(current_menu->num_entries, config.max_buttons));
    }
}

// A function to load a submenu
static void load_submenu(const char *submenu)
{
    current_menu->last_selected_entry = current_entry;
    load_menu_by_name(submenu, true, true);
}

// A function to load the previous menu
static void load_back_menu(Menu *menu)
{
    load_menu(menu->back, false, config.reset_on_back);
}

// A function to update the screen with all visible textures
static void draw_screen()
{
    // Draw background
    SDL_RenderClear(renderer);
    if (!(state.application_launching && config.on_launch == ON_LAUNCH_BLANK)) {
        if (config.background_mode == BACKGROUND_IMAGE || config.background_mode == BACKGROUND_SLIDESHOW)
            SDL_RenderCopy(renderer, background_texture, NULL, NULL);

        if (config.background_mode == BACKGROUND_SLIDESHOW && state.slideshow_transition)
            SDL_RenderCopy(renderer, slideshow->transition_texture, NULL, NULL);

        // Draw background overlay
        if (config.background_overlay)
            SDL_RenderCopy(renderer, background_overlay, NULL, NULL);

        // Draw scroll indicators
        if (config.scroll_indicators &&
        (current_menu->page*config.max_buttons + (unsigned int) geo.num_buttons) <= (current_menu->num_entries - 1))
            SDL_RenderCopy(renderer, scroll->texture, NULL, &scroll->rect_right);

        if (config.scroll_indicators && current_menu->page > 0)
            SDL_RenderCopyEx(renderer, scroll->texture, NULL, &scroll->rect_left, 0, NULL, SDL_FLIP_HORIZONTAL);

        // Draw clock
        if (config.clock_enabled) {
            SDL_RenderCopy(renderer, clk->time_texture, NULL, &clk->time_rect);
            if (config.clock_show_date)
                SDL_RenderCopy(renderer, clk->date_texture, NULL, &clk->date_rect);
        }

        // Draw highlight
        if (config.highlight)
            SDL_RenderCopy(renderer,
                highlight->texture,
                NULL,
                &highlight->rect
            );

        // Draw buttons
        Entry *entry = current_menu->root_entry;
        SDL_Texture *icon;
        for (int i = 0; i < geo.num_buttons; i++) {
            icon = (entry->icon_selected != NULL && i == (int) current_menu->highlight_position) ? entry->icon_selected : entry->icon;
            SDL_RenderCopy(renderer, icon, NULL, &entry->icon_rect);
            if (config.titles_enabled)
                SDL_RenderCopy(renderer, entry->title_texture, NULL, &entry->text_rect);
            entry = entry-> next;
        }

        // Draw screensaver
        if (state.screensaver_active)
            SDL_RenderCopy(renderer, screensaver->texture, NULL, NULL);
    }
    else
        SDL_RenderFillRect(renderer, NULL);

    // Output to screen
    SDL_RenderPresent(renderer);
    if (!config.vsync) {
        Uint32 elapsed = SDL_GetTicks() - ticks.main;
        if (elapsed < refresh_period)
            SDL_Delay(refresh_period - elapsed);
    }
}

// A function to execute the user's command
static void execute_command(const char *command)
{
    // Copy command into separate buffer
    char *cmd = strdup(command);

    // Parse special commands
    if (cmd[0] == ':') {
        char *delimiter = " ";
        char *special_command = strtok(cmd, delimiter);
        if (!strcmp(special_command, SCMD_SUBMENU)) {
            char *submenu = strtok(NULL, "");
            if (submenu != NULL)
                load_submenu(submenu);
        }
        else if (!strcmp(special_command, SCMD_FORK)) {
            char *fork_command = strtok(NULL, "");
            if (fork_command != NULL)
                start_process(fork_command, false);
        }
        else if (!strcmp(special_command, SCMD_LEFT))
            move_left();
        else if (!strcmp(special_command, SCMD_RIGHT))
            move_right();
        else if (!strcmp(special_command, SCMD_SELECT))
            execute_command(current_entry->cmd);
        else if (!strcmp(special_command, SCMD_HOME))
            load_menu(default_menu, false, true);
        else if (!strcmp(special_command, SCMD_BACK))
            load_back_menu(current_menu);
        else if (!strcmp(special_command, SCMD_QUIT))
            quit(EXIT_SUCCESS);
        else if (!strcmp(special_command, SCMD_SHUTDOWN))
            scmd_shutdown();
        else if (!strcmp(special_command, SCMD_RESTART))
            scmd_restart();
        else if (!strcmp(special_command, SCMD_SLEEP))
            scmd_sleep();
        else if (!strcmp(special_command, SCMD_RELOAD))
            state.reload_pending = true;
    }

    // Launch external application
    else {
        SDL_Delay(50);
        if (start_process(cmd, true)) {
            state.application_launching = true;
            ticks.application_launched = ticks.main;
            if (config.on_launch == ON_LAUNCH_BLANK)
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 0xFF);
            else if (config.on_launch == ON_LAUNCH_QUIT)
                quit(EXIT_SUCCESS);
        }
    }
    free(cmd);
}

// A function to initialize the gamepad struct
static void init_gamepad(Gamepad **gamepad, int device_index)
{
    *gamepad = malloc(sizeof(Gamepad));
    **gamepad = (Gamepad) {
        .device_index = device_index,
        .id = (int) SDL_JoystickGetDeviceInstanceID(device_index),
        .controller = NULL,
        .next = NULL,
        .previous = NULL,
    };
}

// A function to open the SDL controller
static void open_controller(Gamepad *gamepad, bool raise_error)
{
    gamepad->controller = SDL_GameControllerOpen(gamepad->device_index);
    if (gamepad->controller == NULL) {
        if (raise_error)
            log_error("Could not open gamepad at device index %i", config.gamepad_device);
        return;
    }
    if (config.debug && raise_error) {
        char *mapping = SDL_GameControllerMapping(gamepad->controller);
        log_debug("Gamepad Mapping:\n%s", mapping);
        SDL_free(mapping);
    }
}

// A function to connect gamepad(s)
static void connect_gamepad(int device_index, bool open, bool raise_error)
{
    if (device_index >= 0) {
        Gamepad *gamepad = NULL;
        for (gamepad = gamepads; gamepad != NULL; gamepad = gamepad->next) {
            if (gamepad->id == device_index)
                break;
        }
        if (gamepad == NULL) {
            init_gamepad(&gamepad, device_index);
            if (gamepads == NULL)
                gamepads = gamepad;

            // Add to end of the linked list
            else {
                Gamepad *i;
                for (i = gamepads; i->next != NULL; i = i->next);
                i->next = gamepad;
                gamepad->previous = i;
            }
        }
        if (open)
            open_controller(gamepad, raise_error);
    }
    else if (open) {
        for (Gamepad *i = gamepads; i != NULL; i = i->next)
            open_controller(i, raise_error);
    }
}

// A function to disconnect gamepad(s)
static void disconnect_gamepad(int id, bool disconnect, bool remove)
{
    for (Gamepad *i = gamepads; i != NULL;) {
        if (id < 0 || i->id == id) {
            if (disconnect)
                SDL_GameControllerClose(i->controller);
            if (remove) {
                if (i->next != NULL)
                    i->next->previous = i->previous;
                if (i->previous != NULL)
                    i->previous->next = i->next;
                if (i == gamepads)
                    gamepads = i->next;
                Gamepad *tmp = i->next;
                free(i);
                i = tmp;
            }
            else {
                i->controller = NULL;
                i = i->next;
            }
        }
        else
            i = i->next;
    }
}

// A function to poll the connected gamepad for commands
static void poll_gamepad()
{
    int value_multiplier; // Handles positive or negative axis
    bool pressed;
    for (GamepadControl *i = gamepad_controls; i != NULL; i = i->next) {
        pressed = false;
        for (Gamepad *gamepad = gamepads; gamepad != NULL; gamepad = gamepad->next) {

            // Check if axis value exceeds dead zone
            if (i->type == TYPE_AXIS_POS || i->type == TYPE_AXIS_NEG) {
                if (i->type == TYPE_AXIS_POS)
                    value_multiplier = 1;
                else if (i->type == TYPE_AXIS_NEG)
                    value_multiplier = -1;
                if (value_multiplier*SDL_GameControllerGetAxis(gamepad->controller, i->index) > GAMEPAD_DEADZONE) {
                    i->repeat++;
                    pressed = true;
                    break;
                }
            }

            // Check buttons
            else if (i->type == TYPE_BUTTON) {
                if (SDL_GameControllerGetButton(gamepad->controller, i->index)) {
                    i->repeat++;
                    pressed = true;
                    break;
                }
            }
        }
        if (!pressed) {
            i->repeat = 0;
            continue;
        }

        // Execute command if first press or valid repeat
        if (i->repeat == 1) {
            log_debug("Gamepad %s detected", i->label);
            ticks.last_input = ticks.main;
            execute_command(i->cmd);
        }
        else if (i->repeat == delay_period) {
            ticks.last_input = ticks.main;
            execute_command(i->cmd);
            i->repeat -= repeat_period;
        }
    }
}

// A function to update the slideshow
static void update_slideshow()
{
    // If image duration time has elapsed, load the next image and start the transition
    if (!state.slideshow_transition && (ticks.main - ticks.slideshow_load > config.slideshow_image_duration) &&
    !state.slideshow_paused) {
        
        // Render the new background image in a separate thread so we don't block the main thread
        if (!state.slideshow_background_rendering && !state.slideshow_background_ready) {
            Slideshowhread = SDL_CreateThread(load_next_slideshow_background_async, "Slideshow Thread", (void*) slideshow);
            state.slideshow_background_rendering = true;
        }

        // Convert background to texture after the rendering thread has completed
        else if (state.slideshow_background_ready) {
            SDL_WaitThread(Slideshowhread, NULL);
            Slideshowhread = NULL;
            if (config.slideshow_transition_time > 0) {
                slideshow->transition_texture = load_texture(slideshow->transition_surface);
                SDL_SetTextureAlphaMod(slideshow->transition_texture, 0);
                state.slideshow_transition = true;
            }
            else {
                SDL_DestroyTexture(background_texture);
                background_texture = load_texture(slideshow->transition_surface);
                ticks.slideshow_load = ticks.main;
            }
        slideshow->transition_surface = NULL;
        state.slideshow_background_ready = false;
        }
    }
    else if (state.slideshow_transition) {
        
        // Increase the transparency
        slideshow->transition_alpha += slideshow->transition_change_rate;
        
        // If transition is done, destroy old background and replace it with the new one
        if (slideshow->transition_alpha >= 255.0f) {
            SDL_SetTextureAlphaMod(slideshow->transition_texture, 0xFF);
            slideshow->transition_alpha = 0.0f;
            SDL_DestroyTexture(background_texture);
            background_texture = slideshow->transition_texture;
            slideshow->transition_texture = NULL;
            state.slideshow_transition = false;
            ticks.slideshow_load = ticks.main;
        }
        else
            SDL_SetTextureAlphaMod(slideshow->transition_texture, (Uint8) slideshow->transition_alpha);
    }
}

// A function to update the screensaver
static void update_screensaver()
{
    // Activate the screensaver if the launcher has been idle for the required time
    if (!state.screensaver_active && ticks.main - ticks.last_input > config.screensaver_idle_time) {
        state.screensaver_active = true;
        state.screensaver_transition = true;
        if (config.background_mode == BACKGROUND_SLIDESHOW && config.screensaver_pause_slideshow)
            state.slideshow_paused = true;
    }
    else {

        // Transition the screen to dark
        if (state.screensaver_transition) {
            screensaver->alpha += screensaver->transition_change_rate;
            if (screensaver->alpha >= screensaver->alpha_end_value) {
                SDL_SetTextureAlphaMod(screensaver->texture, (Uint8) screensaver->alpha_end_value);
                state.screensaver_transition = false;
            }
            else
                SDL_SetTextureAlphaMod(screensaver->texture, (Uint8) screensaver->alpha);
        }

        // User has pressed input, deactivate the screensaver
        if (state.screensaver_active && ticks.last_input == ticks.main) {
            SDL_SetTextureAlphaMod(screensaver->texture, 0);
            screensaver->alpha = 0.0f;
            state.screensaver_active = false;
            state.screensaver_transition = false;
            if (config.background_mode == BACKGROUND_SLIDESHOW) {
                state.slideshow_paused = false;
                
                // Reset the slideshow time so we don't have a transition immediately 
                // after coming out of screensaver mode
                ticks.slideshow_load = ticks.main;
            }
        }
    }
}

// A function to update the clock display
static void update_clock(bool block)
{
    if (ticks.main - ticks.clock_update > CLOCK_UPDATE_PERIOD) {
        if (!state.clock_rendering) {

            // Check to see if the time has changed
            get_time(clk);
            if (clk->render_time) {
                state.clock_rendering = true;
                if (block)
                    render_clock(clk);
                else
                    clock_thread = SDL_CreateThread(render_clock_async, "Clock Thread", (void*) clk); 
            }
            else
                ticks.clock_update = ticks.main;
        }

        // Render texture
        if (state.clock_ready) {
            SDL_WaitThread(clock_thread, NULL);
            clock_thread = NULL;
            SDL_DestroyTexture(clk->time_texture);
            clk->time_texture = load_texture(clk->time_surface);
            clk->time_surface = NULL;
            if (clk->render_date) {
                SDL_DestroyTexture(clk->date_texture);
                clk->date_texture = load_texture(clk->date_surface);
                clk->date_surface = NULL;
            }
            ticks.clock_update = ticks.main;
            clk->render_time = false;
            clk->render_date = false;
            state.clock_rendering = false;
            state.clock_ready = false;
        }
    }
}

static inline void pre_launch()
{
    if (gamepads != NULL)
        disconnect_gamepad(-1, true, false);

// Initialize exit hotkey for Windows
#ifdef _WIN32
    if (has_exit_hotkey())
        SDL_EventState(SDL_SYSWMEVENT, SDL_ENABLE);
#endif
}

static inline void post_launch()
{
    // Rebaseline the timing after the program is done
    ticks.main = SDL_GetTicks();
    ticks.last_input = ticks.main;

    // Post-application updates
    if (config.gamepad_enabled)
        connect_gamepad(-1, true, false);
    if (config.clock_enabled)
        update_clock(true);
    if (config.background_mode == BACKGROUND_SLIDESHOW)
        resume_slideshow();
    if (config.on_launch == ON_LAUNCH_BLANK)
        set_draw_color();

#ifdef _WIN32
    SDL_EventState(SDL_SYSWMEVENT, SDL_DISABLE);
    if (config.background_mode == BACKGROUND_TRANSPARENT)
        hide_cursor(current_entry);
#endif
}

// A function to quit the launcher
void quit(int status)
{
    log_debug("Quitting program");
    if (status != EXIT_SUCCESS)
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, 
            PROJECT_NAME, 
            "A critical error occurred. Check the log file for details.", 
            NULL
        );
    if (config.quit_cmd != NULL)
        execute_command(config.quit_cmd);
    cleanup();
    exit(status);
}

// A function to print the version and other info to command line
void print_version(FILE *stream)
{
    SDL_version sdl_version;
    SDL_GetVersion(&sdl_version);
    const SDL_version *img_version = IMG_Linked_Version();
    const SDL_version *ttf_version = TTF_Linked_Version();
    fprintf(stream, PROJECT_NAME " version " PROJECT_VERSION ", using:" endline);
    fprintf(stream, "  SDL       %u.%u.%u" endline, sdl_version.major, sdl_version.minor, sdl_version.patch);
    fprintf(stream, "  SDL_image %u.%u.%u" endline, img_version->major, img_version->minor, img_version->patch);
    fprintf(stream, "  SDL_ttf   %u.%u.%u" endline, ttf_version->major, ttf_version->minor, ttf_version->patch);
}

// A function to load the config file and build everything that depends on it
static int launcher_load()
{
    // Start from built-in defaults, keeping what came from the command line
    char *exe_path = config.exe_path;
    char *config_path = config.config_path;
    bool debug = config.debug;
    memcpy(&config, &default_config, sizeof(Config));
    config.exe_path = exe_path;
    config.config_path = config_path;
    config.debug = debug;

    if (parse_config_file(config.config_path))
        return 1;
    if (config.default_menu == NULL) {
        log_error("No default menu defined in config file");
        return 1;
    }
    default_menu = get_menu(config.default_menu);
    if (default_menu == NULL) {
        log_error("Default menu %s not found in config file", config.default_menu);
        return 1;
    }

    if (config.inhibit_os_screensaver)
        SDL_DisableScreenSaver();
    else
        SDL_EnableScreenSaver();

    if (load_title_font())
        return 1;
    validate_settings(&geo);
    if (config.background_mode == BACKGROUND_SLIDESHOW)
        init_slideshow();
    if (create_renderer())
        return 1;
    ticks.main = SDL_GetTicks();
    ticks.last_input = ticks.main;

    // Gamepad subsystem follows the setting. SDL only announces already-attached
    // devices when the subsystem starts, so on reload connect them here.
    if (config.gamepad_enabled) {
        bool already_running = SDL_WasInit(SDL_INIT_GAMECONTROLLER) != 0;
        if (!already_running && SDL_InitSubSystem(SDL_INIT_GAMECONTROLLER) < 0)
            log_error("Could not initialize gamepad subsystem\n%s", SDL_GetError());
        if (config.gamepad_mappings_file != NULL &&
        SDL_GameControllerAddMappingsFromFile(config.gamepad_mappings_file) < 0)
            log_error("Could not load gamepad mappings from %s\n%s", config.gamepad_mappings_file, SDL_GetError());
        if (already_running) {
            for (int i = 0; i < SDL_NumJoysticks(); i++) {
                if (SDL_IsGameController(i) && (config.gamepad_device < 0 || config.gamepad_device == i))
                    connect_gamepad(i, true, false);
            }
        }
    }
    else if (SDL_WasInit(SDL_INIT_GAMECONTROLLER))
        SDL_QuitSubSystem(SDL_INIT_GAMECONTROLLER);

    // Render background
    if (config.background_mode == BACKGROUND_IMAGE) {
        if (config.background_image == NULL)
            log_error("Background 'Image' setting not specified in config file");
        else
            background_texture = load_texture_from_file(config.background_image);

        // Switch to color mode if loading background image failed
        if (background_texture == NULL) {
            config.background_mode = BACKGROUND_COLOR;
            log_error("Couldn't load background image, defaulting to color background");
            set_draw_color();
        }
    }

    // Render first slideshow image
    else if (config.background_mode == BACKGROUND_SLIDESHOW) {
        SDL_Surface *surface = load_next_slideshow_background(slideshow, false);
        background_texture = load_texture(surface);
    }

    // Initialize screensaver
    if (config.screensaver_enabled)
        init_screensaver();

    // Initialize clock
    if (config.clock_enabled) {
        clk = calloc(1, sizeof(Clock));
        init_clock(clk);
        ticks.clock_update = ticks.main;
    }
    
    // Render highlight
    if (config.highlight) {
        int button_height = config.icon_size + config.title_padding + geo.font_height;
        highlight = malloc(sizeof(Highlight));
        highlight->texture = render_highlight(config.icon_size + 2*config.highlight_hpadding,
                                button_height + 2*config.highlight_vpadding,
                                &highlight->rect
                            );
    }

    // Render scroll indicators
    if (config.scroll_indicators) {
        scroll = malloc(sizeof(Scroll));
        scroll->texture = NULL;
        int scroll_indicator_height = (int) ((float) geo.screen_height * SCROLL_INDICATOR_HEIGHT);
        render_scroll_indicators(scroll, scroll_indicator_height, &geo);
    }

    // Render background overlay
    if (config.background_overlay) {
        SDL_Surface *overlay_surface = NULL;
        overlay_surface = SDL_CreateRGBSurfaceWithFormat(0, 
                              geo.screen_width, 
                              geo.screen_height, 
                              32,
                              SDL_PIXELFORMAT_ARGB8888
                          );
        Uint32 overlay_color = SDL_MapRGBA(overlay_surface->format, 
                                   config.background_overlay_color.r, 
                                   config.background_overlay_color.g, 
                                   config.background_overlay_color.b, 
                                   config.background_overlay_color.a
                               );
        SDL_FillRect(overlay_surface, NULL, overlay_color);
        background_overlay = load_texture(overlay_surface);
    }

#ifdef _WIN32
    if (has_exit_hotkey())
        register_exit_hotkey();
#endif

    if (config.debug) {
        debug_video(renderer, &display_mode);
        debug_settings();
        debug_gamepad(gamepad_controls);
        debug_hotkeys(hotkeys);
        debug_menu_entries(config.first_menu, config.num_menus);
    }

    if (load_menu(default_menu, false, true)) {
        log_error("Could not load default menu %s", config.default_menu);
        return 1;
    }
    return 0;
}

// A function to put config.ini.bak back in place, keeping the bad file as config.ini.failed
static int restore_config_backup(const char *path)
{
    char backup_path[MAX_PATH_CHARS + 1];
    char failed_path[MAX_PATH_CHARS + 1];
    snprintf(backup_path, sizeof(backup_path), "%s.bak", path);
    snprintf(failed_path, sizeof(failed_path), "%s.failed", path);
    IniDoc *backup = ini_doc_load(backup_path);
    if (backup == NULL)
        return 1;
    remove(failed_path);
    replace_file(path, failed_path);
    int error = ini_doc_save(backup, path);
    ini_doc_free(backup);
    return error;
}

// A function to re-read config.ini and rebuild the launcher without closing the window
int launcher_reload()
{
    log_debug("Reloading configuration from %s", config.config_path);
    launcher_unload();
    int error = launcher_load();
    if (error) {
        log_error("Reload failed, restoring the previous configuration");
        launcher_unload();
        if (restore_config_backup(config.config_path) || launcher_load())
            log_fatal("Could not load %s or its backup %s.bak", config.config_path, config.config_path);
    }
    return error;
}

int main(int argc, char *argv[])
{
    memcpy(&config, &default_config, sizeof(Config));
    config.exe_path = SDL_GetBasePath();
    handle_arguments(argc, argv, &config.config_path);

    // One-time setup: SDL, image/font libraries, SVG rasterizer, window
    init_sdl();
    init_sdl_image();
    if (TTF_Init() == -1)
        log_fatal("Could not initialize SDL_ttf\n%s", TTF_GetError());
    init_svg();
    create_window();
    ticks.program_start = SDL_GetTicks();

    if (launcher_load())
        log_fatal("Could not load configuration from %s", config.config_path);

    // Execute startup command
    if (config.startup_cmd != NULL)
        execute_command(config.startup_cmd);
    
    // Main program loop
    log_debug("Begin program loop");
    while (1) {
        ticks.main = SDL_GetTicks();

        // Apply a :reload requested during the previous frame
        if (state.reload_pending) {
            state.reload_pending = false;
            launcher_reload();
            SDL_FlushEvents(SDL_KEYDOWN, SDL_MOUSEWHEEL);
            ticks.main = SDL_GetTicks();
        }
        while (SDL_PollEvent(&event)) {
            switch(event.type) {
                case SDL_QUIT:
                    quit(EXIT_SUCCESS);
                    break;

                case SDL_KEYDOWN:
                    ticks.last_input = ticks.main;
                    handle_keypress(&event.key.keysym);
                    break;
                
                case SDL_MOUSEBUTTONDOWN:
                    if (config.mouse_select && event.button.button == SDL_BUTTON_LEFT) {
                        ticks.last_input = ticks.main;
                        execute_command(current_entry->cmd);
                    }
                    break;

                case SDL_JOYDEVICEADDED:
                    if (SDL_IsGameController(event.jdevice.which) == SDL_TRUE) {
                        log_debug("Gamepad connected with device index %i", event.jdevice.which);
                        if (config.gamepad_device < 0 || config.gamepad_device == event.jdevice.which)
                            connect_gamepad(event.jdevice.which, !state.application_running, true);
                    }
                    break;

                case SDL_JOYDEVICEREMOVED:
                    if (SDL_IsGameController(event.jdevice.which) == SDL_TRUE || 1) {
                        log_debug("Gamepad disconnected");
                        if (config.gamepad_device < 0 || config.gamepad_device == event.jdevice.which)
                            disconnect_gamepad(event.jdevice.which, true, true);
                    }
                    break;

                case SDL_WINDOWEVENT:
                    if (event.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                        log_debug("Lost keyboard focus");
                        state.has_focus = false;
                        if (state.application_launching) {
                            log_debug("Application detected");
                            state.application_launching = false;
                            state.application_running = true;
                            pre_launch();
                        }
#ifdef _WIN32
                        // Sometimes the launcher will lose focus on Windows when autostarting
                        // So if we lose the window focus within 10 seconds of the launcher starting
                        // we will grab back th Window focus. This is a bit of a hack
                        else if (ticks.main - ticks.program_start < 10000)
                            set_foreground_window();
#endif
                    }
                    else if (event.window.event == SDL_WINDOWEVENT_FOCUS_GAINED) {
                        log_debug("Gained keyboard focus");
                        state.has_focus = true;
                    }
                    else if (event.window.event == SDL_WINDOWEVENT_LEAVE)
                        log_debug("Lost mouse focus");
                    break;
#ifdef _WIN32
                case SDL_SYSWMEVENT:
                    check_exit_hotkey(event.syswm.msg);
                    break;
#endif
            }
        }

        // Update application state
        if (state.application_running && state.has_focus) {
            state.application_running = false;
            post_launch();
            log_debug("Application finished");
        }

        // Post-event loop updates
        if (!(state.application_running || state.application_launching)) {
            if (gamepads != NULL)
                poll_gamepad();
            if (config.background_mode == BACKGROUND_SLIDESHOW)
                update_slideshow();
            if (config.screensaver_enabled)
                update_screensaver();
            if (config.clock_enabled)
                update_clock(false);
        }
        if (state.application_launching &&
        ticks.main - ticks.application_launched > config.application_timeout) {
            state.application_launching = false;
            if (config.on_launch == ON_LAUNCH_BLANK)
                set_draw_color();
        }
        if (state.application_running)
            SDL_Delay(APPLICATION_WAIT_PERIOD);
        else
            draw_screen();
    }
    quit(EXIT_SUCCESS);
}
